/**
 * ISteamWorks: the Steamworks API for other Source2Mod plugins, the C++ side
 * of SteamWorks.inc. One place for the game server's Steam interfaces and for
 * Steam's callbacks, so a plugin needs no steam_api of its own.
 *
 * Fetch it in OnAllSource2ModPluginsLoaded():
 *
 *     GET_SOURCE2MOD_IFACE(STEAMWORKS_INTERFACE_VERSION, g_pSteamWorks, ISteamWorks);
 */

#ifndef _INCLUDE_ISTEAMWORKS_H
#define _INCLUDE_ISTEAMWORKS_H

#pragma once

#include <steam/steam_gameserver.h>
#include <steam/isteamgamecoordinator.h>

#define STEAMWORKS_INTERFACE_VERSION "ISteamWorks001"

class ISteamWorksListener
{
public:
    virtual ~ISteamWorksListener() = default;

    virtual void OnSteamServersConnected() {}
    virtual void OnSteamServersConnectFailure(EResult result) {}
    virtual void OnSteamServersDisconnected(EResult result) {}

    /// Steam validated a client; owner differs from user on a shared game.
    virtual void OnValidateClient(CSteamID owner, CSteamID user) {}
    /// The answer to a GetUserGroupStatus.
    virtual void OnClientGroupStatus(CSteamID user, CSteamID group, bool member, bool officer) {}
};

class ISteamWorks
{
public:
    virtual ~ISteamWorks() = default;

    /// The game server's Steam API is up.
    virtual bool IsLoaded() const = 0;
    /// Logged on to Steam.
    virtual bool IsConnected() const = 0;

    /// nullptr while the Steam API is down.
    virtual ISteamGameServer* GameServer() = 0;
    virtual ISteamGameServerStats* Stats() = 0;
    virtual ISteamHTTP* HTTP() = 0;
    virtual ISteamUGC* UGC() = 0;
    virtual ISteamUtils* Utils() = 0;
    virtual ISteamGameCoordinator* GameCoordinator() = 0;

    /**
     * @brief A message the game reads as if the game coordinator had sent it,
     * bytes as RetrieveMessage gives them.
     * @return false when refused: core.json's FollowCS2ServerGuidelines is on,
     *         or the game's GC client has not registered yet
     */
    virtual bool InjectGCMessage(uint32 msgType, const void* data, uint32 size) = 0;

    /// Dropped by hand; remove it before the listener's plugin unloads.
    virtual void AddListener(ISteamWorksListener* listener) = 0;
    virtual void RemoveListener(ISteamWorksListener* listener) = 0;
};

#endif
