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
#include <steam/isteamnetworkingmessages.h>
#include <steam/isteamnetworkingsockets.h>

#include <gcsdk/soid.h>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>

#include <string>

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

/// The raw form; IGCSOListener below parses the shared object messages for you.
class ISteamWorksGCListener
{
public:
    virtual ~ISteamWorksGCListener() = default;

    /// A protobuf message, eMsg without the protobuf flag. false drops it (ignored under FollowCS2ServerGuidelines).
    virtual bool OnGCMessage(uint32_t eMsg, ISteamWorksGCMessage& msg) = 0;
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

    /// A protobuf message by its id and serialized body (the SendGCMessage template below);
    /// onDelivered runs the frame after the game read it. Refused as InjectGCMessage is.
    virtual bool SendGCMessage(uint32_t eMsg, const void* body, size_t size, void (*onDelivered)(void*), void* context) = 0;
    /// Runs the frame after the game read every message queued so far.
    virtual void AfterGCQueued(void (*fn)(void*), void* context) = 0;
    /// From the GC's welcome until a connection status without a session.
    virtual bool HasGCSession() const = 0;

    /// Remove it before the listener's plugin unloads.
    virtual void AddGCListener(ISteamWorksGCListener* listener) = 0;
    virtual void RemoveGCListener(ISteamWorksGCListener* listener) = 0;

    /// Dropped by hand; remove it before the listener's plugin unloads.
    virtual void AddListener(ISteamWorksListener* listener) = 0;
    virtual void RemoveListener(ISteamWorksListener* listener) = 0;
};

/**
 * The real game coordinator's shared object messages before the game gets them.
 * Register with AddGCListener; remove it before the plugin unloads.
 *
 * Parsed here, in the listener's own library: a protobuf object must not cross
 * from one plugin into another. Changing a message writes it back, and false
 * drops it; under core.json's FollowCS2ServerGuidelines both are ignored and
 * this only watches.
 */
class IGCSOListener : public ISteamWorksGCListener
{
public:
    virtual ~IGCSOListener() = default;

    // The GC session starts, the game unsubscribes every cache the welcome leaves out
    virtual void OnGCWelcome(CMsgClientWelcome& msg) {}
    virtual bool OnGCCacheSubscribed(CMsgSOCacheSubscribed& msg) { return true; }
    virtual void OnGCCacheUnsubscribed(const GCSDK::SOID_t& owner) {}
    virtual bool OnGCSingleObject(uint32_t eMsg, CMsgSOSingleObject& msg) { return true; }
    virtual bool OnGCMultipleObjects(CMsgSOMultipleObjects& msg) { return true; }

    bool OnGCMessage(uint32_t eMsg, ISteamWorksGCMessage& raw) final
    {
        switch (eMsg)
        {
            case k_EMsgGCServerWelcome:
            {
                CMsgClientWelcome msg;
                if (Parse(raw, msg))
                {
                    OnGCWelcome(msg);
                    Write(raw, msg);
                }
                return true;
            }
            case k_ESOMsg_CacheSubscribed:
            {
                CMsgSOCacheSubscribed msg;
                if (!Parse(raw, msg))
                    return true;
                if (!OnGCCacheSubscribed(msg))
                    return false;
                Write(raw, msg);
                return true;
            }
            case k_ESOMsg_CacheUnsubscribed:
            {
                CMsgSOCacheUnsubscribed msg;
                if (Parse(raw, msg))
                {
                    GCSDK::SOID_t owner{ msg.owner_soid().id(), msg.owner_soid().type() };
                    OnGCCacheUnsubscribed(owner);
                }
                return true;
            }
            case k_ESOMsg_Create:
            case k_ESOMsg_Update:
            case k_ESOMsg_Destroy:
            {
                CMsgSOSingleObject msg;
                if (!Parse(raw, msg))
                    return true;
                if (!OnGCSingleObject(eMsg, msg))
                    return false;
                Write(raw, msg);
                return true;
            }
            case k_ESOMsg_UpdateMultiple:
            {
                CMsgSOMultipleObjects msg;
                if (!Parse(raw, msg))
                    return true;
                if (!OnGCMultipleObjects(msg))
                    return false;
                Write(raw, msg);
                return true;
            }
        }
        return true;
    }

private:
    template <typename T>
    static bool Parse(const ISteamWorksGCMessage& raw, T& msg)
    {
        return msg.ParseFromArray(raw.GetBody(), static_cast<int>(raw.GetBodySize()));
    }

    template <typename T>
    static void Write(ISteamWorksGCMessage& raw, const T& msg)
    {
        std::string body;
        if (msg.SerializeToString(&body))
            raw.SetBody(body.data(), body.size());
    }
};

/// Sends a protobuf message to the game as if the GC had (ISteamWorks::SendGCMessage).
template <typename T>
inline bool SendGCMessage(ISteamWorks* steamworks, uint32_t eMsg, const T& msg, void (*onDelivered)(void*) = nullptr, void* context = nullptr)
{
    std::string body;
    return msg.SerializeToString(&body) && steamworks->SendGCMessage(eMsg, body.data(), body.size(), onDelivered, context);
}

#endif
