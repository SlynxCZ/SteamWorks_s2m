#include "gc.h"

#include "sdk/game/shared/gc_clientsystem.h"

#include <gcsdk/gcmsg.h>
#include <gcsdk_gcmessages.pb.h>
#include <gcsystemmsgs.pb.h>

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#undef SendMessage
#endif

GCManager* g_pGCManager = nullptr;

namespace
{
    template <typename... A>
    cell_t FireForResult(const char* name, A&&... args)
    {
        ISource2ModScriptForward* forward = g_pSource2ModNatives->FindScriptForward(name);
        if (!forward || forward->GetFunctionCount() <= 0)
            return 0;
        return forward->Call<int>(std::forward<A>(args)...);
    }

    bool HasForward(const char* name)
    {
        ISource2ModScriptForward* forward = g_pSource2ModNatives->FindScriptForward(name);
        return forward && forward->GetFunctionCount() > 0;
    }

    // What a listener gets: the frame of one protobuf message, its body replaceable.
    class FramedMessage final : public ISteamWorksGCMessage
    {
    public:
        FramedMessage(const std::string& data) : m_Data(data)
        {
            GCSDK::ProtoBufMsgHeader_t hdr;
            if (data.size() < sizeof(hdr))
                return;
            std::memcpy(&hdr, data.data(), sizeof(hdr));
            if (data.size() < sizeof(hdr) + hdr.m_cubProtoBufExtHdr)
                return;
            m_nBodyOffset = sizeof(hdr) + hdr.m_cubProtoBufExtHdr;
            m_bValid = true;
        }

        bool IsValid() const { return m_bValid; }
        bool IsChanged() const { return m_bChanged; }
        const std::string& Rebuilt() const { return m_Rebuilt; }

        const void* GetBody() const override { return m_bChanged ? m_Rebuilt.data() + m_nBodyOffset : m_Data.data() + m_nBodyOffset; }
        size_t GetBodySize() const override { return (m_bChanged ? m_Rebuilt.size() : m_Data.size()) - m_nBodyOffset; }

        void SetBody(const void* body, size_t size) override
        {
            std::string rebuilt(m_Data.data(), m_nBodyOffset);
            rebuilt.append(static_cast<const char*>(body), size);
            m_Rebuilt = std::move(rebuilt);
            m_bChanged = true;
        }

    private:
        const std::string& m_Data;
        std::string m_Rebuilt;
        size_t m_nBodyOffset = 0;
        bool m_bValid = false;
        bool m_bChanged = false;
    };
}

GCManager::GCManager()
{
}

GCManager::~GCManager()
{
    Detach();

    // The core drops the hooks of a plugin that unloads; the functions go with them.
    m_pGCMessageAvailable = nullptr;
    m_bSession = false;
    m_Queue.clear();
}

bool GCManager::Init(char* error, size_t maxlength)
{
    // CCSGCServerSystem* g_pCSGCServerSystem
    g_pCSGCServerSystem = reinterpret_cast<CCSGCServerSystem*>(g_pSource2ModGameSystems->FindByName("CCSGCServerSystem"));
    if (!g_pCSGCServerSystem)
    {
        g_pSource2ModAPI->Format(error, maxlength, "Failed to get g_pCSGCServerSystem");
        return false;
    }

    // Inline Hooks
    {
        const Source2ModType registerParams[] = { Source2ModType::Pointer, Source2ModType::Int32 };
        const Source2ModType unregisterParams[] = { Source2ModType::Pointer };

        // void SteamAPI_RegisterCallback(CCallbackBase* pCallback, int iCallback)
        m_pfnRegisterCallback = g_pSource2ModFunctions->Create(reinterpret_cast<void*>(&::SteamAPI_RegisterCallback), nullptr, -1, false, Source2ModType::Void, registerParams, 2);
        // void SteamAPI_UnregisterCallback(CCallbackBase* pCallback)
        m_pfnUnregisterCallback = g_pSource2ModFunctions->Create(reinterpret_cast<void*>(&::SteamAPI_UnregisterCallback), nullptr, -1, false, Source2ModType::Void, unregisterParams, 1);

        if (!m_pfnRegisterCallback || !m_pfnUnregisterCallback
            || !g_pSource2ModHooks->Hook(HookPoint::Function(m_pfnRegisterCallback), HookTarget::All(), { this, &GCManager::SteamAPI_RegisterCallback })
            || !g_pSource2ModHooks->Hook(HookPoint::Function(m_pfnUnregisterCallback), HookTarget::All(), { this, &GCManager::SteamAPI_UnregisterCallback }))
        {
            g_pSource2ModAPI->Format(error, maxlength, "Failed to hook SteamAPI_RegisterCallback");
            return false;
        }
    }

    // VirtualTable Hooks
    {
        m_pfnSendMessage = g_pSource2ModFunctions->Find("ISteamGameCoordinator::SendMessage");
        m_pfnIsMessageAvailable = g_pSource2ModFunctions->Find("ISteamGameCoordinator::IsMessageAvailable");
        m_pfnRetrieveMessage = g_pSource2ModFunctions->Find("ISteamGameCoordinator::RetrieveMessage");
        if (!m_pfnSendMessage || !m_pfnIsMessageAvailable || !m_pfnRetrieveMessage)
        {
            g_pSource2ModAPI->Format(error, maxlength, "gamedata/steamworks.json has no ISteamGameCoordinator entries");
            return false;
        }
    }

    return true;
}

ISteamGameCoordinator* GCManager::GetGC()
{
    if (m_pGC)
        return m_pGC;
    return g_pCSGCServerSystem ? g_pCSGCServerSystem->GetGCClient()->GetSteamGameCoordinator() : nullptr;
}

bool GCManager::CanSend()
{
    return !g_pSource2ModAPI->FollowsServerGuidelines() && GetGC() && m_pGCMessageAvailable;
}

bool GCManager::Wanted() const
{
    return !m_Listeners.empty() || !m_Queue.empty() || HasForward("SteamWorks_GCSendMessage") || HasForward("SteamWorks_GCMsgAvailable") || HasForward("SteamWorks_GCRetrieveMessage");
}

// GCSDK::CGCClient::BInit registers GCMessageAvailable_t: on the Steam logon (SteamServersConnected),
// after GameServerSteamAPIActivated and before the hello, so we see the GC's welcome
void GCManager::Attach()
{
    if (m_pGC)
        return;

    // BInit sets it before it registers the callback
    ISteamGameCoordinator* pGC = GetGC();
    if (!pGC)
        return;

    m_pGC = pGC;
    m_hSendMessage = g_pSource2ModHooks->Hook(HookPoint::Function(m_pfnSendMessage), HookTarget::Instance(pGC), { this, &GCManager::ISteamGameCoordinator_SendMessage });
    m_hIsMessageAvailable = g_pSource2ModHooks->Hook(HookPoint::Function(m_pfnIsMessageAvailable), HookTarget::Instance(pGC), { this, &GCManager::ISteamGameCoordinator_IsMessageAvailable });
    m_hRetrieveMessage = g_pSource2ModHooks->Hook(HookPoint::Function(m_pfnRetrieveMessage), HookTarget::Instance(pGC), { this, &GCManager::ISteamGameCoordinator_RetrieveMessage });
}

void GCManager::Detach()
{
    if (!m_pGC)
        return;
    g_pSource2ModHooks->Unhook(m_hSendMessage);
    g_pSource2ModHooks->Unhook(m_hIsMessageAvailable);
    g_pSource2ModHooks->Unhook(m_hRetrieveMessage);
    m_hSendMessage = m_hIsMessageAvailable = m_hRetrieveMessage = 0;
    m_pGC = nullptr;
}

void GCManager::OnDeactivated()
{
    Detach();
    m_pGCMessageAvailable = nullptr;
    m_bSession = false;
    m_Queue.clear();
    m_Delivered.clear();
    m_DeliveredLastFrame.clear();
}

void GCManager::AddListener(ISteamWorksListener* pListener)
{
    if (pListener && std::find(m_Listeners.begin(), m_Listeners.end(), pListener) == m_Listeners.end())
        m_Listeners.push_back(pListener);
}

void GCManager::RemoveListener(ISteamWorksListener* pListener)
{
    std::erase(m_Listeners, pListener);
}

bool GCManager::SendRaw(uint32_t eMsgFlagged, std::string data, std::function<void()> onDelivered)
{
    if (!CanSend())
    {
        if (g_pSource2ModAPI->FollowsServerGuidelines())
        {
            static bool s_said = false;
            if (!s_said)
                Warning("[SteamWorks] A game coordinator message was not injected: core.json's FollowCS2ServerGuidelines is on\n");
            s_said = true;
        }
        return false;
    }

    Message& msg = m_Queue.emplace_back();
    msg.m_eMsg = eMsgFlagged;
    msg.m_Data = std::move(data);
    msg.m_OnDelivered = std::move(onDelivered);
    return true;
}

bool GCManager::Send(uint32_t eMsg, const void* body, size_t bodySize, std::function<void()> onDelivered)
{
    GCSDK::ProtoBufMsgHeader_t hdr;
    hdr.m_EMsgFlagged = eMsg | GCSDK::k_EMsgProtoBufFlag;
    hdr.m_cubProtoBufExtHdr = 0;

    std::string data(sizeof(hdr) + bodySize, '\0');
    std::memcpy(data.data(), &hdr, sizeof(hdr));
    std::memcpy(data.data() + sizeof(hdr), body, bodySize);

    return SendRaw(hdr.m_EMsgFlagged, std::move(data), std::move(onDelivered));
}

void GCManager::AfterQueued(std::function<void()> fn)
{
    if (m_Queue.empty())
    {
        m_Delivered.push_back(std::move(fn));
        return;
    }

    std::function<void()>& onDelivered = m_Queue.back().m_OnDelivered;
    if (!onDelivered)
    {
        onDelivered = std::move(fn);
        return;
    }

    onDelivered = [first = std::move(onDelivered), then = std::move(fn)]()
    {
        first();
        then();
    };
}

void GCManager::OnFrame()
{
    auto due = std::move(m_DeliveredLastFrame);
    m_DeliveredLastFrame.clear();

    if (!m_pGC && Wanted())
        Attach();

    // CGCClient::OnGCMessageAvailable reads until the queue is empty
    if (!m_Queue.empty() && m_pGC && m_pGCMessageAvailable)
    {
        GCMessageAvailable_t msg;
        msg.m_nMessageSize = static_cast<uint32>(m_Queue.front().m_Data.size());
        m_pGCMessageAvailable->Run(&msg);
    }

    // Handled by the game by the next frame
    m_DeliveredLastFrame = std::move(m_Delivered);
    m_Delivered.clear();

    for (auto& fn : due)
        fn();
}

void GCManager::PullRealMessages(ISteamGameCoordinator* pGC)
{
    if (m_bPulling)
        return;
    m_bPulling = true;

    using IsAvailableFn = bool (*)(ISteamGameCoordinator*, uint32*);
    using RetrieveFn = EGCResults (*)(ISteamGameCoordinator*, uint32*, void*, uint32, uint32*);
    const auto pfnAvailable = reinterpret_cast<IsAvailableFn>(m_pfnIsMessageAvailable->GetAddress(pGC, true));
    const auto pfnRetrieve = reinterpret_cast<RetrieveFn>(m_pfnRetrieveMessage->GetAddress(pGC, true));

    uint32 cubMsg = 0;
    while (pfnAvailable(pGC, &cubMsg))
    {
        FireForResult("SteamWorks_GCMsgAvailable", static_cast<int>(cubMsg));

        std::string data(cubMsg, '\0');
        uint32 eMsg = 0;
        uint32 cubRead = 0;

        EGCResults eResult = pfnRetrieve(pGC, &eMsg, data.data(), cubMsg, &cubRead);
        if (eResult != k_EGCResultOK)
            break;

        data.resize(cubRead);

        if (ProcessRealMessage(eMsg, data))
        {
            m_Queue.push_back({ eMsg, std::move(data), nullptr });
        }
    }

    m_bPulling = false;
}

bool GCManager::ProcessRealMessage(uint32_t eMsg, std::string& data)
{
    // Dropping or rewriting what the game reads is the same fake as adding to it
    const bool bFake = !g_pSource2ModAPI->FollowsServerGuidelines();

    m_pCurrent = &data;
    const cell_t result = FireForResult("SteamWorks_GCRetrieveMessage", static_cast<int>(eMsg), data.c_str(), static_cast<int>(data.size()), static_cast<int>(data.size()));
    m_pCurrent = nullptr;
    if (result != k_EGCResultOK && bFake)
        return false;

    if ((eMsg & GCSDK::k_EMsgProtoBufFlag) == 0)
        return true;

    const uint32_t eMsgId = eMsg & ~GCSDK::k_EMsgProtoBufFlag;

    FramedMessage msg(data);
    if (!msg.IsValid())
        return true;

    switch (eMsgId)
    {
        // CGCClient: the welcome's caches are subscribed (outofdate in full, uptodate kept or refreshed),
        // every other cache it has is unsubscribed; the CGCClientJobServerWelcome job reads it after that
        case k_EMsgGCServerWelcome:
        {
            m_bSession = true;
            break;
        }
        case k_EMsgGCClientConnectionStatus:
        case k_EMsgGCServerConnectionStatus:
        {
            CMsgConnectionStatus status;
            if (status.ParseFromArray(msg.GetBody(), static_cast<int>(msg.GetBodySize())) && status.status() != GCConnectionStatus_HAVE_SESSION)
                m_bSession = false;
            break;
        }
    }

    const std::vector<ISteamWorksListener*> listeners = m_Listeners;
    for (ISteamWorksListener* pListener : listeners)
    {
        if (!pListener->OnGCMessage(eMsgId, msg) && bFake)
            return false;
    }

    if (msg.IsChanged() && bFake)
    {
        std::string rebuilt = msg.Rebuilt();
        data = std::move(rebuilt);
    }

    return true;
}

HookReturn<void> GCManager::SteamAPI_RegisterCallback(FunctionContext& ctx, bool post)
{
    auto* pCallback = ctx.GetArg<CCallbackBase*>(0);
    const int iCallback = ctx.GetArg<int>(1);

    // Only the game's GC client's own (its m_CallbackGCMessageAvailable), not someone else's listener
    const auto* pGCClient = reinterpret_cast<const uint8*>(g_pCSGCServerSystem->GetGCClient());
    const auto* pAddress = reinterpret_cast<const uint8*>(pCallback);
    const bool bGCClient = pAddress >= pGCClient && pAddress < pGCClient + sizeof(GCSDK::CGCClient);

    if (iCallback == GCMessageAvailable_t::k_iCallback && bGCClient)
    {
        m_pGCMessageAvailable = pCallback;
        if (Wanted())
            Attach();
    }

    return Action::Ignore;
}

HookReturn<void> GCManager::SteamAPI_UnregisterCallback(FunctionContext& ctx, bool post)
{
    if (ctx.GetArg<CCallbackBase*>(0) == m_pGCMessageAvailable)
    {
        m_pGCMessageAvailable = nullptr;
    }

    return Action::Ignore;
}

HookReturn<void> GCManager::ISteamGameCoordinator_SendMessage(FunctionContext& ctx, bool post)
{
    const auto type = ctx.GetArg<uint32>(1);
    const auto* pData = ctx.GetArg<const char*>(2);
    const auto size = ctx.GetArg<uint32>(3);

    const std::string data(pData, size);
    m_pCurrent = &data;
    const cell_t result = FireForResult("SteamWorks_GCSendMessage", static_cast<int>(type), data.c_str(), static_cast<int>(size));
    m_pCurrent = nullptr;

    if (result == k_EGCResultOK || g_pSource2ModAPI->FollowsServerGuidelines())
        return Action::Ignore;

    // -1 as in SteamWorks: not sent, and the game hears it was
    ctx.SetReturn<EGCResults>(result == -1 ? k_EGCResultOK : static_cast<EGCResults>(result));
    return Action::Supersede;
}

HookReturn<void> GCManager::ISteamGameCoordinator_IsMessageAvailable(FunctionContext& ctx, bool post)
{
    auto* pThis = static_cast<ISteamGameCoordinator*>(ctx.This());
    auto* pcubMsgSize = ctx.GetArg<uint32*>(1);

    if (m_Queue.empty())
    {
        PullRealMessages(pThis);
    }

    if (m_Queue.empty())
    {
        if (pcubMsgSize)
            *pcubMsgSize = 0;

        ctx.SetReturn<bool>(false);
        return Action::Supersede;
    }

    if (pcubMsgSize)
        *pcubMsgSize = static_cast<uint32>(m_Queue.front().m_Data.size());

    ctx.SetReturn<bool>(true);
    return Action::Supersede;
}

HookReturn<void> GCManager::ISteamGameCoordinator_RetrieveMessage(FunctionContext& ctx, bool post)
{
    auto* pThis = static_cast<ISteamGameCoordinator*>(ctx.This());
    auto* punMsgType = ctx.GetArg<uint32*>(1);
    auto* pubDest = ctx.GetArg<void*>(2);
    const auto cubDest = ctx.GetArg<uint32>(3);
    auto* pcubMsgSize = ctx.GetArg<uint32*>(4);

    if (m_Queue.empty())
    {
        PullRealMessages(pThis);
    }

    if (m_Queue.empty())
    {
        ctx.SetReturn<EGCResults>(k_EGCResultNoMessage);
        return Action::Supersede;
    }

    Message& msg = m_Queue.front();
    uint32 cubMsg = static_cast<uint32>(msg.m_Data.size());

    if (punMsgType)
        *punMsgType = msg.m_eMsg;

    if (pcubMsgSize)
        *pcubMsgSize = cubMsg;

    if (cubDest < cubMsg)
    {
        ctx.SetReturn<EGCResults>(k_EGCResultBufferTooSmall);
        return Action::Supersede;
    }

    std::memcpy(pubDest, msg.m_Data.data(), cubMsg);

    if (msg.m_OnDelivered)
    {
        m_Delivered.push_back(std::move(msg.m_OnDelivered));
    }

    m_Queue.pop_front();

    ctx.SetReturn<EGCResults>(k_EGCResultOK);
    return Action::Supersede;
}
