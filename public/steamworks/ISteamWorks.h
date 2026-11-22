/**
 * vim: set ts=4 sw=4 tw=99 noet:
 * =============================================================================
 * SteamWorks_s2m
 * Copyright (C) 2026 Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl.
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
 *
 * As a special exception, Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl gives
 * you permission to link the code of this program (as well as its derivative
 * works) to "Counter-Strike 2," "Source 2," "Steam," and any Game MODs or
 * server software running on software by Valve Corporation. You must obey the
 * GNU General Public License in all respects for all other code used.
 *
 * Additionally, this exception applies to all derivative works unless
 * otherwise stated in LICENSE.txt.
 *
 * Authors:
 *   - Michal "Slynx (˙·٠● S l y n x ●٠·˙)" Přikryl
 *
 * Project: SteamWorks_s2m
 */

/**
 * ISteamWorks: the Steamworks API for other Source2Mod plugins, the C++ side
 * of SteamWorks.inc. One place for the game server's Steam interfaces and for
 * Steam's callbacks, so a plugin needs no steam_api of its own.
 *
 * Fetch it in OnAllSource2ModPluginsLoaded():
 *
 *     GET_SOURCE2MOD_IFACE(g_pSteamWorks, ISteamWorks, STEAMWORKS_INTERFACE_VERSION, ret, nullptr);
 */

#ifndef _INCLUDE_ISTEAMWORKS_H
#define _INCLUDE_ISTEAMWORKS_H

#pragma once

#include <steam/steam_gameserver.h>
#include <steam/isteamgamecoordinator.h>
#include <steam/isteamnetworkingmessages.h>
#include <steam/isteamnetworkingsockets.h>


#define STEAMWORKS_INTERFACE_VERSION "ISteamWorks001"

/// One message of the game coordinator on its way to the game.
class ISteamWorksGCMessage
{
public:
    virtual const void* GetBody() const = 0;
    virtual size_t GetBodySize() const = 0;
    /// What the game gets instead; ignored under FollowCS2ServerGuidelines.
    virtual void SetBody(const void* body, size_t size) = 0;

protected:
    ~ISteamWorksGCMessage() = default;
};

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

    /// A game coordinator protobuf message before the game gets it, eMsg without the protobuf flag.
    /// false drops it (ignored under FollowCS2ServerGuidelines).
    virtual bool OnGCMessage(uint32_t eMsg, ISteamWorksGCMessage& msg) { return true; }
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
    virtual ISteamInventory* Inventory() = 0;
    virtual ISteamNetworking* Networking() = 0;
    virtual ISteamNetworkingSockets* NetworkingSockets() = 0;
    virtual ISteamNetworkingMessages* NetworkingMessages() = 0;
    virtual ISteamGameCoordinator* GameCoordinator() = 0;

    /**
     * @brief A message the game reads as if the game coordinator had sent it,
     * bytes as RetrieveMessage gives them.
     * @return false when refused: core.json's FollowCS2ServerGuidelines is on,
     *         or the game's GC client has not registered yet
     */
    virtual bool InjectGCMessage(uint32 msgType, const void* data, uint32 size) = 0;

    /// A protobuf message by its id and serialized body;
    /// onDelivered runs the frame after the game read it. Refused as InjectGCMessage is.
    virtual bool SendGCMessage(uint32_t eMsg, const void* body, size_t size, void (*onDelivered)(void*), void* context) = 0;
    /// Runs the frame after the game read every message queued so far.
    virtual void AfterGCQueued(void (*fn)(void*), void* context) = 0;
    /// From the GC's welcome until a connection status without a session.
    virtual bool HasGCSession() const = 0;

    /// Dropped by hand; remove it before the listener's plugin unloads.
    virtual void AddListener(ISteamWorksListener* listener) = 0;
    virtual void RemoveListener(ISteamWorksListener* listener) = 0;
};

#endif
