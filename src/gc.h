#pragma once

#include "plugin.h"

#include <khook.hpp>
#include <steam/isteamgamecoordinator.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

/**
 * The game coordinator: the game's GC client reads through
 * ISteamGameCoordinator, so messages can be watched, dropped or added on that
 * road. IsMessageAvailable and RetrieveMessage serve one queue with the real
 * messages and the injected ones; the game hears of an injected message
 * through its own GCMessageAvailable_t callback. The vtable hooks go in only
 * when somebody listens or injects.
 */
class SteamGC
{
public:
    SteamGC();
    ~SteamGC();

    void Frame();
    void OnDeactivated();

    ISteamGameCoordinator* Interface();
    bool HasCallback() const { return m_pAvailable != nullptr; }

    /// Refused under FollowCS2ServerGuidelines: a faked GC message can fake an inventory.
    bool Inject(uint32 msgType, std::string data, std::function<void()> onDelivered);

    /// The message a SteamWorks_GCRetrieveMessage / SteamWorks_GCSendMessage forward is about.
    const std::string* Current() const { return m_pCurrent; }

    void AddListener();
    void RemoveListener();

private:
    struct Message
    {
        uint32 type;
        std::string data;
        std::function<void()> onDelivered;
    };

    bool Wanted() const;
    void Attach();
    void Detach();
    void PullReal(ISteamGameCoordinator* gc);

    KHook::Return<void> RegisterCallbackPre(CCallbackBase* callback, int id);
    KHook::Return<void> UnregisterCallbackPre(CCallbackBase* callback);
    KHook::Return<EGCResults> SendPre(ISteamGameCoordinator* self, uint32 type, const void* data, uint32 size);
    KHook::Return<bool> AvailablePre(ISteamGameCoordinator* self, uint32* size);
    KHook::Return<EGCResults> RetrievePre(ISteamGameCoordinator* self, uint32* type, void* dest, uint32 destSize, uint32* size);

    KHook::Function<void, CCallbackBase*, int>* m_hRegister = nullptr;
    KHook::Function<void, CCallbackBase*>* m_hUnregister = nullptr;
    KHook::Virtual<ISteamGameCoordinator, EGCResults, uint32, const void*, uint32>* m_hSend = nullptr;
    KHook::Virtual<ISteamGameCoordinator, bool, uint32*>* m_hAvailable = nullptr;
    KHook::Virtual<ISteamGameCoordinator, EGCResults, uint32*, void*, uint32, uint32*>* m_hRetrieve = nullptr;

    ISteamGameCoordinator* m_pGC = nullptr;
    CCallbackBase* m_pAvailable = nullptr;
    std::deque<Message> m_Queue;
    std::vector<std::function<void()>> m_Delivered;
    std::vector<std::function<void()>> m_DeliveredLastFrame;
    const std::string* m_pCurrent = nullptr;
    bool m_bPulling = false;
    int m_nListeners = 0;
};

extern SteamGC* g_pSteamGC;

void RegisterGCNatives();
void RegisterHTTPNatives();
void RegisterUGCNatives();

/// Callbacks of a script that goes must not be called any more.
void ForgetScript(PluginId id);
void RegisterUGCCallbacks();
void UnregisterUGCCallbacks();
