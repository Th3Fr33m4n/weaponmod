/*
 * Half-Life Weapon Mod
 * Copyright (c) 2012 AGHL.RU Dev Team
 * 
 * http://aghl.ru/forum/ - Russian Half-Life and Adrenaline Gamer Community
 *
 *
 *    This program is free software; you can redistribute it and/or modify it
 *    under the terms of the GNU General Public License as published by the
 *    Free Software Foundation; either version 2 of the License, or (at
 *    your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful, but
 *    WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *    General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software Foundation,
 *    Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *    In addition, as a special exception, the author gives permission to
 *    link the code of this program with the Half-Life Game Engine ("HL
 *    Engine") and Modified Game Libraries ("MODs") developed by Valve,
 *    L.L.C ("Valve").  You must obey the GNU General Public License in all
 *    respects for all of the code used other than the HL Engine and MODs
 *    from Valve.  If you modify this file, you may extend this exception
 *    to your version of the file, but you are not obligated to do so.  If
 *    you do not wish to do so, delete this exception statement from your
 *    version.
 *
 */

#include "weaponmod.h"
#include "hooks.h"
#include "utils.h"


int g_SpawnedWpns = 0;
int g_SpawnedAmmo = 0;

EntData *g_Ents = NULL;

cvar_t *cvar_aghlru = NULL;
cvar_t *cvar_sv_cheats = NULL;
cvar_t *cvar_mp_weaponstay = NULL;

CVector <VirtHookData *> g_BlockedItems;

edict_t* g_EquipEnt = NULL;


void OnAmxxAttach()
{
	BOOL bAddNatives = TRUE; 

	cvar_aghlru = CVAR_GET_POINTER("aghl.ru");
	cvar_sv_cheats = CVAR_GET_POINTER("sv_cheats");
	cvar_mp_weaponstay = CVAR_GET_POINTER("mp_weaponstay");
	
	if (!FindModuleByAddr((void*)MDLL_FUNC->pfnGetGameDescription(), &hl_dll))
	{
		print_srvconsole("[WEAPONMOD] Failed to locate %s\n", GET_GAME_INFO(PLID, GINFO_DLL_FILENAME));
		bAddNatives = FALSE;
	}
	else
	{
		for (int i = 0; i < Func_End; i++)
		{
			if (cvar_aghlru)
			{
				g_dllFuncs[i].sig = g_dllFuncs[i].sigCustom;
			}

			if (CreateFunctionHook(&g_dllFuncs[i]))
			{
				SetHook(&g_dllFuncs[i]);
			}
		}
	}

	for (int i = 0; i < Func_End; i++)
	{
		if (!g_dllFuncs[i].address)
		{
			print_srvconsole("[WEAPONMOD] Failed to find \"%s\" function.\n", g_dllFuncs[i].name);
			bAddNatives = FALSE;
		}
	}
	
	if (!bAddNatives)
	{
		print_srvconsole("[WEAPONMOD] Cannot register natives.\n");
	}
	else
	{
		MF_AddNatives(Natives);
		SetHookVirt(&g_WorldPrecache_Hook);

		print_srvconsole("[WEAPONMOD] Found %s at %p\n", GET_GAME_INFO(PLID, GINFO_DLL_FILENAME), hl_dll.base);

		print_srvconsole("\n   Half-Life Weapon Mod version %s Copyright (c) 2012 AGHL.RU Dev Team. \n"
						"   Weapon Mod comes with ABSOLUTELY NO WARRANTY; for details type `wpnmod gpl'.\n", Plugin_info.version);
		print_srvconsole("   This is free software and you are welcome to redistribute it under \n"
						"   certain conditions; type 'wpnmod gpl' for details.\n  \n");
	}

	g_Ents = new EntData[gpGlobals->maxEntities];

	cvar_t version = {"hl_wpnmod_version", Plugin_info.version, FCVAR_SERVER};
	
	REG_SVR_COMMAND("wpnmod", WpnModCommand);
	CVAR_REGISTER (&version);
}



void OnAmxxDetach()
{
	int i;

	for (i = 0; i < Func_End; i++)
	{
		if (g_dllFuncs[i].done)
		{
			UnsetHook(&g_dllFuncs[i]);
		}
	}
	
	for (i = 0; i < CrowbarHook_End; i++)
	{
		UnsetHookVirt(&g_CrowbarHooks[i]);
	}

	UnsetHookVirt(&g_WorldPrecache_Hook);
	UnsetHookVirt(&g_RpgAddAmmo_Hook);
	
	delete [] g_Ents;
}



void ServerDeactivate()
{
	g_EquipEnt = NULL;

	g_SpawnedWpns = 0;
	g_SpawnedAmmo = 0;

	g_iWeaponsCount = 0;
	g_iWeaponInitID = 0;
	g_iAmmoBoxIndex = 0;
	
	for (int i = 0; i < (int)g_BlockedItems.size(); i++)
	{
		UnsetHookVirt(g_BlockedItems[i]);
		delete g_BlockedItems[i];
	}

	g_BlockedItems.clear();

	memset(g_iCurrentSlots, 0, sizeof(g_iCurrentSlots));
	memset(WeaponInfoArray, 0, sizeof(WeaponInfoArray));
	memset(AmmoBoxInfoArray, 0, sizeof(AmmoBoxInfoArray));

	UnsetHookVirt(&g_PlayerSpawn_Hook);
	RETURN_META(MRES_IGNORED);
}



int AmxxCheckGame(const char *game)
{
	if (!strcasecmp(game, "valve"))
	{
		return AMXX_GAME_OK;
	}
	
	return AMXX_GAME_BAD;
}

/*
void Player_SendAmmoUpdate(edict_t* pPlayer)
{
	for (int i = 0; i < MAX_AMMO_SLOTS; i++)
	{
		// send "Ammo" update message
		MESSAGE_BEGIN( MSG_ONE, REG_USER_MSG("AmmoX", 2), NULL, pPlayer);
			WRITE_BYTE( i );
			WRITE_BYTE( max( min( (int)*((int *)pPlayer->pvPrivateData + m_rgAmmo + i - 1), 254 ), 0 ) );  // clamp the value to one byte
		MESSAGE_END();
	}
}
*/
void ClientCommand(edict_t *pEntity)
{
	const char* cmd = CMD_ARGV(0);

	if (cmd && !_stricmp(cmd, "give") && cvar_sv_cheats->value)
	{
		const char* item = CMD_ARGV(1);

		if (item)
		{
			GiveNamedItem(pEntity, item);
		}
	}
	else if (cmd && !_stricmp(cmd, "wpnmod"))
	{
		int i = 0;
		int ammo = 0;
		int items = 0;
		int weapons = 0;
		
		static char buf[1024];
		size_t len = 0;
			
		sprintf(buf, "\n%s %s\n", Plugin_info.name, Plugin_info.version);
		CLIENT_PRINT(pEntity, print_console, buf);
		len = sprintf(buf, "Author: \n         KORD_12.7 (AGHL.RU Dev Team)\n");
		len += sprintf(&buf[len], "Credits: \n         AMXX Dev team, 6a6kin, GordonFreeman, Koshak, Lev, noo00oob\n");
		len += sprintf(&buf[len], "Compiled: %s\nURL: http://www.aghl.ru/ - Russian Half-Life and Adrenaline Gamer Community.\n\n", __DATE__ ", " __TIME__);
		CLIENT_PRINT(pEntity, print_console, buf);

		CLIENT_PRINT(pEntity, print_console, "Currently loaded weapons:\n");

		for (i = 1; i <= g_iWeaponsCount; i++)
		{
			if (WeaponInfoArray[i].iType == Wpn_Custom)
			{
				items++;
				sprintf(buf, " [%2d] %-23.22s\n", ++weapons, GetWeapon_pszName(i));
				CLIENT_PRINT(pEntity, print_console, buf);
			}
		}

		CLIENT_PRINT(pEntity, print_console, "\nCurrently loaded ammo:\n");

		for (i = 0; i < g_iAmmoBoxIndex; i++)
		{
			items++;
			sprintf(buf, " [%2d] %-23.22s\n", ++ammo, AmmoBoxInfoArray[i].classname.c_str());
			CLIENT_PRINT(pEntity, print_console, buf);
		}

		CLIENT_PRINT(pEntity, print_console, "\nTotal:\n");
		sprintf(buf, "%4d items (%d weapons, %d ammo).\n\n", items, weapons, ammo);
		CLIENT_PRINT(pEntity, print_console, buf);

		RETURN_META(MRES_SUPERCEDE);
	}
	/*else if(cmd && _stricmp(cmd, "test1") == 0)
	{
		MESSAGE_BEGIN( MSG_ONE, REG_USER_MSG("ResetHUD", 1), NULL, pEntity );
			WRITE_BYTE( 0 );
		MESSAGE_END();


		//WeaponInfoArray[5].ItemData.iSlot = -1;
		//WeaponInfoArray[5].ItemData.iPosition = -1;

		//if (msgWeaponList || (msgWeaponList = REG_USER_MSG( "WeaponList", -1)))		
		{
			MESSAGE_BEGIN(MSG_ONE, REG_USER_MSG( "WeaponList", -1), NULL, pEntity);
			WRITE_STRING("weapon_rpg7");
			WRITE_BYTE(-1);
			WRITE_BYTE(-1);
			WRITE_BYTE(-1);
			WRITE_BYTE(-1);
			WRITE_BYTE(0);
			WRITE_BYTE(2);
			WRITE_BYTE(5);
			WRITE_BYTE(0);
			MESSAGE_END();
		}

		{
			MESSAGE_BEGIN(MSG_ONE, REG_USER_MSG( "WeaponList", -1), NULL, pEntity);
			WRITE_STRING("weapon_sniperrifle");
			WRITE_BYTE(-1);
			WRITE_BYTE(-1);
			WRITE_BYTE(-1);
			WRITE_BYTE(-1);
			WRITE_BYTE(0);
			WRITE_BYTE(1);
			WRITE_BYTE(16);
			WRITE_BYTE(0);
			MESSAGE_END();
		}

		Player_SendAmmoUpdate(pEntity);

		
	}*/

	RETURN_META(MRES_IGNORED);
}



void ServerActivate_Post(edict_t *pEdictList, int edictCount, int clientMax)
{
	// Get spawn point and create item from map's bsp file.
	ParseBSP();

	// Parse and create default equipment
	ParseConfigSection("[equipment]", ParseEquipment_Handler);

	// Remove blocked items
	for (int i = 0; i < (int)g_BlockedItems.size(); i++)
	{
		edict_t *pFind = FIND_ENTITY_BY_CLASSNAME(NULL, g_BlockedItems[i]->classname);

		while (!FNullEnt(pFind))
		{
			pFind->v.flags |= FL_KILLME;
			pFind = FIND_ENTITY_BY_CLASSNAME(pFind, g_BlockedItems[i]->classname);
		}
	}

	// Spawn items from ini file.
	if (ParseConfigSection("[spawns]", ParseSpawnPoints_Handler))
	{
		print_srvconsole("[WEAPONMOD] spawn %d weapons and %d ammoboxes from config.\n", g_SpawnedWpns, g_SpawnedAmmo);
	}

	RETURN_META(MRES_IGNORED);
}
