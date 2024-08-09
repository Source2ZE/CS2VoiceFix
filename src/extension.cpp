/**
 * =============================================================================
 * CS2VoiceFix
 * Copyright (C) 2024 Poggu
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include "extension.h"
#include <iserver.h>
#include "utils/module.h"
#include <schemasystem/schemasystem.h>
#include <entity2/entitysystem.h>
#include "interfaces.h"
#include "networkstringtabledefs.h"
#include "../protobufs/generated/netmessages.pb.h"
#include "networksystem/inetworkmessages.h"
#include "cs2_sdk/serversideclient.h"

#ifdef _WIN32
#define ROOTBIN "/bin/win64/"
#define GAMEBIN "/csgo/bin/win64/"
#else
#define ROOTBIN "/bin/linuxsteamrt64/"
#define GAMEBIN "/csgo/bin/linuxsteamrt64/"
#endif

#undef max

CS2VoiceFix g_CS2VoiceFix;
int g_iSendNetMessage;
uint64_t g_randomSeed = 0;

SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0, CPlayerSlot, const char*, uint64, const char*, const char*, bool);

#ifdef WIN32
// WINDOWS
SH_DECL_MANUALHOOK3(SendNetMessage, 15, 0, 0, bool, CNetMessage*, NetChannelBufType_t);
#else
// LINUX
SH_DECL_MANUALHOOK3(SendNetMessage, 16, 0, 0, bool, CNetMessage*, NetChannelBufType_t);
#endif

CGameEntitySystem* GameEntitySystem()
{
#ifdef WIN32
	static int offset = 88;
#else
	static int offset = 80;
#endif
	return *reinterpret_cast<CGameEntitySystem**>((uintptr_t)(g_pGameResourceServiceServer)+offset);
}

// Should only be called within the active game loop (i e map should be loaded and active)
// otherwise that'll be nullptr!
CGlobalVars* GetGameGlobals()
{
	INetworkGameServer* server = g_pNetworkServerService->GetIGameServer();

	if (!server)
		return nullptr;

	return g_pNetworkServerService->GetIGameServer()->GetGlobals();
}

void SetupHook(CS2VoiceFix* plugin)
{
	CModule engineModule(ROOTBIN, "engine2");

	auto serverSideClientVTable = engineModule.FindVirtualTable("CServerSideClient");
	g_iSendNetMessage = SH_ADD_MANUALDVPHOOK(SendNetMessage, serverSideClientVTable, SH_MEMBER(plugin, &CS2VoiceFix::Hook_SendNetMessage), false);
}

PLUGIN_EXPOSE(CS2VoiceFix, g_CS2VoiceFix);
bool CS2VoiceFix::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, Interfaces::engine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, Interfaces::icvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, Interfaces::server, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, Interfaces::gameclients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkServerService, INetworkServerService, NETWORKSERVERSERVICE_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pGameResourceServiceServer, IGameResourceService, GAMERESOURCESERVICESERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, Interfaces::networkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
	g_SMAPI->AddListener( this, this );

	SH_ADD_HOOK(IServerGameClients, OnClientConnected, Interfaces::gameclients, SH_MEMBER(this, &CS2VoiceFix::Hook_OnClientConnected), false);

	SetupHook(this);

	g_pCVar = Interfaces::icvar;
	ConVar_Register( FCVAR_RELEASE | FCVAR_GAMEDLL );

	srand(time(NULL));
	// just random numbers to create bogus "steam ids" in voice data
	g_randomSeed = 800 + rand() % ((0x00FFFFFFFFFFFFFF) - 800);

	return true;
}

uint64_t g_playerIds[64];

bool CS2VoiceFix::Hook_SendNetMessage(CNetMessage* pData, NetChannelBufType_t bufType)
{
	CServerSideClient* client = META_IFACEPTR(CServerSideClient);

	NetMessageInfo_t* info = pData->GetNetMessage()->GetNetMessageInfo();
	if (info)
	{
		if (info->m_MessageId == SVC_Messages::svc_VoiceData)
		{
			auto msg = pData->ToPB<CSVCMsg_VoiceData>();

			auto playerSlot = msg->client();
			//ConMsg("%i -> %i (%llu)\n", playerSlot, client->GetPlayerSlot().Get(), g_playerIds[client->GetPlayerSlot().Get()] + playerSlot);

			msg->set_xuid(g_playerIds[client->GetPlayerSlot().Get()] + playerSlot);
		}
	}


	RETURN_META_VALUE(MRES_IGNORED, true);
}

void CS2VoiceFix::Hook_OnClientConnected(CPlayerSlot slot, const char* pszName, uint64 xuid, const char* pszNetworkID, const char* pszAddress, bool bFakePlayer)
{
	g_playerIds[slot.Get()] = g_randomSeed;
	g_randomSeed += 66;
}

bool CS2VoiceFix::Unload(char *error, size_t maxlen)
{
	SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, Interfaces::gameclients, SH_MEMBER(this, &CS2VoiceFix::Hook_OnClientConnected), false);
	SH_REMOVE_HOOK_ID(g_iSendNetMessage);
	return true;
}

void CS2VoiceFix::AllPluginsLoaded()
{
}

void CS2VoiceFix::OnLevelInit( char const *pMapName,
									 char const *pMapEntities,
									 char const *pOldLevel,
									 char const *pLandmarkName,
									 bool loadGame,
									 bool background )
{
}

void CS2VoiceFix::OnLevelShutdown()
{
}

bool CS2VoiceFix::Pause(char *error, size_t maxlen)
{
	return true;
}

bool CS2VoiceFix::Unpause(char *error, size_t maxlen)
{
	return true;
}

const char *CS2VoiceFix::GetLicense()
{
	return "GPLv3";
}

const char *CS2VoiceFix::GetVersion()
{
	return "1.0.0";
}

const char *CS2VoiceFix::GetDate()
{
	return __DATE__;
}

const char *CS2VoiceFix::GetLogTag()
{
	return "CS2VoiceFix";
}

const char *CS2VoiceFix::GetAuthor()
{
	return "Poggu";
}

const char *CS2VoiceFix::GetDescription()
{
	return "Fixes voice chat breaking on addon unload";
}

const char *CS2VoiceFix::GetName()
{
	return "CS2VoiceFix";
}

const char *CS2VoiceFix::GetURL()
{
	return "https://poggu.me";
}
