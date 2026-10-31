/**
 * IGCSOListener: the real game coordinator's shared object messages before the
 * game gets them, for a plugin that links the game's protobufs (the SDK's
 * AddProtobufs). Register with ISteamWorks::AddGCListener; remove it before the
 * plugin unloads.
 *
 * Parsed here, in the listener's own library: a protobuf object must not cross
 * from one plugin into another. Changing a message writes it back, and false
 * drops it; under core.json's FollowCS2ServerGuidelines both are ignored and
 * this only watches.
 */

#ifndef _INCLUDE_IGCSOLISTENER_H
#define _INCLUDE_IGCSOLISTENER_H

#pragma once

#include "ISteamWorks.h"

#include <gcsdk/soid.h>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>

#include <string>

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
