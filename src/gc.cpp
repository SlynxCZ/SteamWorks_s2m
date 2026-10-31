#include "gc.h"

#include <algorithm>
#include <cstring>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#undef SendMessage
#else
#include <dlfcn.h>
#endif

using namespace SourcePawn;

SteamGC* g_pSteamGC = nullptr;

namespace
{
    constexpr uint32 kProtoBufFlag = 0x80000000u;

    // The game's GC client's callback object: its vtable is in the server module.
    bool IsFromServerModule(const void* object)
    {
        const void* vtable = *static_cast<void* const*>(object);
#ifdef _WIN32
        HMODULE module = nullptr;
        char path[MAX_PATH] = "";
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCSTR>(vtable), &module)
            || !GetModuleFileNameA(module, path, sizeof(path)))
            return false;
        return strstr(path, "server.dll") != nullptr;
#else
        Dl_info info{};
        return dladdr(vtable, &info) && info.dli_fname && strstr(info.dli_fname, "libserver") != nullptr;
#endif
    }

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
}

SteamGC::SteamGC()
{
    m_hRegister = new KHook::Function<void, CCallbackBase*, int>(this, &SteamGC::RegisterCallbackPre, nullptr);
    m_hUnregister = new KHook::Function<void, CCallbackBase*>(this, &SteamGC::UnregisterCallbackPre, nullptr);
    m_hSend = new KHook::Virtual<ISteamGameCoordinator, EGCResults, uint32, const void*, uint32>(&ISteamGameCoordinator::SendMessage, this, &SteamGC::SendPre, nullptr);
    m_hAvailable = new KHook::Virtual<ISteamGameCoordinator, bool, uint32*>(&ISteamGameCoordinator::IsMessageAvailable, this, &SteamGC::AvailablePre, nullptr);
    m_hRetrieve = new KHook::Virtual<ISteamGameCoordinator, EGCResults, uint32*, void*, uint32, uint32*>(&ISteamGameCoordinator::RetrieveMessage, this, &SteamGC::RetrievePre, nullptr);

    // Before the Steam logon, when CGCClient registers its callback.
    m_hRegister->Configure(reinterpret_cast<void*>(&::SteamAPI_RegisterCallback));
    m_hUnregister->Configure(reinterpret_cast<void*>(&::SteamAPI_UnregisterCallback));
}

SteamGC::~SteamGC()
{
    Detach();
    delete m_hRegister;
    delete m_hUnregister;
    delete m_hSend;
    delete m_hAvailable;
    delete m_hRetrieve;
}

ISteamGameCoordinator* SteamGC::Interface()
{
    if (m_pGC)
        return m_pGC;
    ISteamClient* client = SteamGameServerClient();
    if (!client || !SteamGameServer_GetHSteamPipe())
        return nullptr;
    return static_cast<ISteamGameCoordinator*>(client->GetISteamGenericInterface(SteamGameServer_GetHSteamUser(), SteamGameServer_GetHSteamPipe(), STEAMGAMECOORDINATOR_INTERFACE_VERSION));
}

bool SteamGC::Wanted() const
{
    return m_nListeners > 0 || !m_Queue.empty() || HasForward("SteamWorks_GCSendMessage") || HasForward("SteamWorks_GCMsgAvailable") || HasForward("SteamWorks_GCRetrieveMessage");
}

void SteamGC::Attach()
{
    if (m_pGC)
        return;
    ISteamGameCoordinator* gc = Interface();
    if (!gc)
        return;
    m_pGC = gc;
    m_hSend->Add(gc);
    m_hAvailable->Add(gc);
    m_hRetrieve->Add(gc);
}

void SteamGC::Detach()
{
    if (!m_pGC)
        return;
    m_hSend->Remove(m_pGC);
    m_hAvailable->Remove(m_pGC);
    m_hRetrieve->Remove(m_pGC);
    m_pGC = nullptr;
}

void SteamGC::OnDeactivated()
{
    Detach();
    m_pAvailable = nullptr;
    m_Queue.clear();
    m_Delivered.clear();
    m_DeliveredLastFrame.clear();
}

void SteamGC::AddListener()
{
    m_nListeners++;
}

void SteamGC::RemoveListener()
{
    m_nListeners--;
}

bool SteamGC::Inject(uint32 msgType, std::string data, std::function<void()> onDelivered)
{
    if (g_pSource2ModAPI->FollowsServerGuidelines())
    {
        static bool s_said = false;
        if (!s_said)
            Warning("[SteamWorks] A game coordinator message was not injected: core.json's FollowCS2ServerGuidelines is on\n");
        s_said = true;
        return false;
    }
    if (!Interface() || !m_pAvailable)
        return false;
    m_Queue.push_back({ msgType, std::move(data), std::move(onDelivered) });
    return true;
}

void SteamGC::Frame()
{
    auto due = std::move(m_DeliveredLastFrame);
    m_DeliveredLastFrame.clear();

    if (!m_pGC && Wanted())
        Attach();

    // CGCClient's callback reads until the queue is empty.
    if (!m_Queue.empty() && m_pGC && m_pAvailable)
    {
        GCMessageAvailable_t msg;
        msg.m_nMessageSize = static_cast<uint32>(m_Queue.front().data.size());
        m_pAvailable->Run(&msg);
    }

    // The game handled them by the next frame.
    m_DeliveredLastFrame = std::move(m_Delivered);
    m_Delivered.clear();

    for (auto& fn : due)
        fn();
}

void SteamGC::PullReal(ISteamGameCoordinator* gc)
{
    if (m_bPulling)
        return;
    m_bPulling = true;

    uint32 size = 0;
    while (m_hAvailable->CallOriginal(gc, &size))
    {
        FireForResult("SteamWorks_GCMsgAvailable", static_cast<int>(size));

        std::string data(size, '\0');
        uint32 type = 0;
        uint32 read = 0;
        if (m_hRetrieve->CallOriginal(gc, &type, data.data(), size, &read) != k_EGCResultOK)
            break;
        data.resize(read);

        m_pCurrent = &data;
        const cell_t result = FireForResult("SteamWorks_GCRetrieveMessage", static_cast<int>(type), data.c_str(), static_cast<int>(read), static_cast<int>(read));
        m_pCurrent = nullptr;

        // Dropping what the game reads is the same fake as adding to it.
        if (result == k_EGCResultOK || g_pSource2ModAPI->FollowsServerGuidelines())
            m_Queue.push_back({ type, std::move(data), nullptr });
    }

    m_bPulling = false;
}

KHook::Return<void> SteamGC::RegisterCallbackPre(CCallbackBase* callback, int id)
{
    if (id == GCMessageAvailable_t::k_iCallback && IsFromServerModule(callback))
        m_pAvailable = callback;
    return { KHook::Action::Ignore };
}

KHook::Return<void> SteamGC::UnregisterCallbackPre(CCallbackBase* callback)
{
    if (callback == m_pAvailable)
        m_pAvailable = nullptr;
    return { KHook::Action::Ignore };
}

KHook::Return<EGCResults> SteamGC::SendPre(ISteamGameCoordinator*, uint32 type, const void* data, uint32 size)
{
    const std::string copy(static_cast<const char*>(data), size);
    m_pCurrent = &copy;
    const cell_t result = FireForResult("SteamWorks_GCSendMessage", static_cast<int>(type), copy.c_str(), static_cast<int>(size));
    m_pCurrent = nullptr;

    if (result == k_EGCResultOK || g_pSource2ModAPI->FollowsServerGuidelines())
        return { KHook::Action::Ignore };

    // -1 as in SteamWorks: not sent, and the game hears it was.
    return { KHook::Action::Supersede, result == -1 ? k_EGCResultOK : static_cast<EGCResults>(result) };
}

KHook::Return<bool> SteamGC::AvailablePre(ISteamGameCoordinator* self, uint32* size)
{
    if (m_Queue.empty())
        PullReal(self);

    if (size)
        *size = m_Queue.empty() ? 0 : static_cast<uint32>(m_Queue.front().data.size());
    return { KHook::Action::Supersede, !m_Queue.empty() };
}

KHook::Return<EGCResults> SteamGC::RetrievePre(ISteamGameCoordinator* self, uint32* type, void* dest, uint32 destSize, uint32* size)
{
    if (m_Queue.empty())
        PullReal(self);

    if (m_Queue.empty())
        return { KHook::Action::Supersede, k_EGCResultNoMessage };

    Message& msg = m_Queue.front();
    const uint32 length = static_cast<uint32>(msg.data.size());
    if (type)
        *type = msg.type;
    if (size)
        *size = length;
    if (destSize < length)
        return { KHook::Action::Supersede, k_EGCResultBufferTooSmall };

    std::memcpy(dest, msg.data.data(), length);
    if (msg.onDelivered)
        m_Delivered.push_back(std::move(msg.onDelivered));
    m_Queue.pop_front();
    return { KHook::Action::Supersede, k_EGCResultOK };
}

// ---- natives ----

namespace
{
    struct DeliveredCallback
    {
        PluginId owner;
        IPluginFunction* fn;
        cell_t value;
    };

    std::vector<std::shared_ptr<DeliveredCallback>> g_delivered;

    const char* Str(IPluginContext* ctx, cell_t addr)
    {
        char* s = nullptr;
        return ctx->LocalToString(addr, &s) == SP_ERROR_NONE && s ? s : "";
    }

    // SendMessageToGC(msgType, data, length)
    cell_t N_SendMessageToGC(IPluginContext* ctx, const cell_t* params)
    {
        ISteamGameCoordinator* gc = g_pSteamGC->Interface();
        if (!gc)
            return k_EGCResultNotLoggedOn;
        return gc->SendMessage(static_cast<uint32>(params[1]), Str(ctx, params[2]), static_cast<uint32>(params[3]));
    }

    std::function<void()> DeliveredFn(IPluginContext* ctx, cell_t callback, cell_t value)
    {
        IPluginFunction* fn = callback && callback != -1 ? ctx->GetFunctionById(static_cast<funcid_t>(callback)) : nullptr;
        if (!fn)
            return nullptr;
        auto entry = std::make_shared<DeliveredCallback>(DeliveredCallback{ g_pSource2ModNatives->GetScriptPlugin(ctx), fn, value });
        g_delivered.push_back(entry);
        return [entry]()
        {
            std::erase(g_delivered, entry);
            if (entry->fn)
            {
                entry->fn->PushCell(entry->value);
                entry->fn->Execute(nullptr);
            }
        };
    }

    // InjectGCMessage(msgType, data, length, onDelivered, value)
    cell_t N_InjectGCMessage(IPluginContext* ctx, const cell_t* params)
    {
        std::string data(Str(ctx, params[2]), static_cast<size_t>(params[3]));
        return g_pSteamGC->Inject(static_cast<uint32>(params[1]), std::move(data), DeliveredFn(ctx, params[4], params[5]));
    }

    // InjectGCProtoMessage(msgId, body, length, onDelivered, value): the protobuf frame around it.
    cell_t N_InjectGCProtoMessage(IPluginContext* ctx, const cell_t* params)
    {
        const uint32 flagged = static_cast<uint32>(params[1]) | kProtoBufFlag;
        const uint32 headerSize = 0;
        std::string data(sizeof(flagged) + sizeof(headerSize) + static_cast<size_t>(params[3]), '\0');
        std::memcpy(data.data(), &flagged, sizeof(flagged));
        std::memcpy(data.data() + sizeof(flagged), &headerSize, sizeof(headerSize));
        std::memcpy(data.data() + sizeof(flagged) + sizeof(headerSize), Str(ctx, params[2]), static_cast<size_t>(params[3]));
        return g_pSteamGC->Inject(flagged, std::move(data), DeliveredFn(ctx, params[4], params[5]));
    }

    // GetGCMessage(buffer, maxlen): the message of the forward running now, bytes as they are.
    cell_t N_GetGCMessage(IPluginContext* ctx, const cell_t* params)
    {
        const std::string* current = g_pSteamGC->Current();
        char* out = nullptr;
        if (!current || ctx->LocalToString(params[1], &out) != SP_ERROR_NONE || !out)
            return 0;
        const size_t length = (std::min)(current->size(), static_cast<size_t>(params[2]));
        std::memcpy(out, current->data(), length);
        return static_cast<cell_t>(length);
    }

    cell_t N_CanInjectGC(IPluginContext*, const cell_t*)
    {
        return !g_pSource2ModAPI->FollowsServerGuidelines() && g_pSteamGC->Interface() && g_pSteamGC->HasCallback();
    }

    const sp_nativeinfo_t g_natives[] = {
        { "SteamWorks_SendMessageToGC", N_SendMessageToGC },
        { "SteamWorks_InjectGCMessage", N_InjectGCMessage },
        { "SteamWorks_InjectGCProtoMessage", N_InjectGCProtoMessage },
        { "SteamWorks_GetGCMessage", N_GetGCMessage },
        { "SteamWorks_CanInjectGC", N_CanInjectGC },
        { nullptr, nullptr },
    };
}

void RegisterGCNatives()
{
    g_pSource2ModNatives->AddNatives(g_natives);
}

void ForgetGCScript(PluginId id)
{
    for (auto& entry : g_delivered)
    {
        if (entry->owner == id)
            entry->fn = nullptr;
    }
}
