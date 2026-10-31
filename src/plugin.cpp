#include "plugin.h"
#include "version_gen.h"
#include "gc.h"

#include <algorithm>
#include <cstring>

SteamWorksPlugin g_Plugin;
SOURCE2MOD_EXPOSE(PLUGIN_NAME, g_Plugin);

namespace
{
    // A forward fired only when somebody listens: finding one is a map lookup.
    template <typename... A>
    void Fire(const char* name, A&&... args)
    {
        ISource2ModScriptForward* forward = g_pSource2ModNatives->FindScriptForward(name);
        if (forward && forward->GetFunctionCount() > 0)
            forward->Call<void>(std::forward<A>(args)...);
    }
}

bool SteamWorksPlugin::Load(ISource2ModAPI* api, char* error, size_t maxlen, bool late)
{
    SOURCE2MOD_SAVEVARS();

    if (!g_pSource2ModNatives || !g_pSource2ModHooks || !g_pSource2ModClients)
    {
        if (error && maxlen)
            g_pSource2ModAPI->Format(error, maxlen, "the core is missing an interface this plugin needs (natives, hooks, clients): update Source2Mod");
        return false;
    }

    api->AddListener(this);

    g_pSource2ModHooks->Hook(HookPoint::GameServerSteamAPIActivated, HookTarget::All(), { this, &SteamWorksPlugin::OnActivated }, true);
    g_pSource2ModHooks->Hook(HookPoint::GameServerSteamAPIDeactivated, HookTarget::All(), { this, &SteamWorksPlugin::OnDeactivated });

    g_pSource2ModHooks->Hook(HookPoint::GameFrame, HookTarget::All(), { this, &SteamWorksPlugin::OnGameFrame }, true);

    g_pGCManager = new GCManager();
    if (!g_pGCManager->Init(error, maxlen))
    {
        delete g_pGCManager;
        g_pGCManager = nullptr;
        return false;
    }

    const Source2ModType gameServer[] = { Source2ModType::Pointer };
    m_pfnLogOnAnonymous = g_pSource2ModFunctions->FromVirtual(KHook::GetVtableIndex(&ISteamGameServer::LogOnAnonymous), { Source2ModType::Void, gameServer, 1, true });
    m_pfnWasRestartRequested = g_pSource2ModFunctions->FromVirtual(KHook::GetVtableIndex(&ISteamGameServer::WasRestartRequested), { Source2ModType::Bool, gameServer, 1, true });

    RegisterSteamWorksNatives();
    RegisterHTTPNatives();
    RegisterUGCNatives();
    RegisterGeneratedNatives();
    RegisterGCNatives();
    g_pSource2ModNatives->RegisterLibrary("SteamWorks");

    // Loaded into a running server: the activation is not coming again.
    if (SteamGameServer())
        RegisterCallbacks();

    return true;
}

bool SteamWorksPlugin::Unload(char* error, size_t maxlen)
{
    UnregisterCallbacks();
    m_listeners.clear();
    delete g_pGCManager;
    g_pGCManager = nullptr;
    return true;
}

void* SteamWorksPlugin::OnSource2ModQuery(const char* iface, int* ret)
{
    if (!iface)
        return nullptr;

    if (!strcmp(iface, STEAMWORKS_INTERFACE_VERSION))
    {
        if (ret) *ret = SOURCE2MOD_IFACE_OK;
        return static_cast<ISteamWorks*>(this);
    }

    if (ret) *ret = SOURCE2MOD_IFACE_FAILED;
    return nullptr;
}

void SteamWorksPlugin::OnPluginUnload(PluginId id, const char* name)
{
    ForgetScript(id);
    ForgetGeneratedCalls(id);
}

HookReturn<void> SteamWorksPlugin::OnGameFrame(GameFrameContext& ctx, bool post)
{
    g_pGCManager->OnFrame();
    return Action::Ignore;
}

HookReturn<void> SteamWorksPlugin::LogOnAnonymousPre(FunctionContext& ctx, bool post)
{
    ISource2ModScriptForward* forward = g_pSource2ModNatives->FindScriptForward("SteamWorks_TokenRequested");
    if (!forward || forward->GetFunctionCount() <= 0)
        return Action::Ignore;

    char token[256] = "";
    forward->Call<void>(token, static_cast<int>(sizeof(token)));
    if (!token[0])
        return Action::Ignore;

    static_cast<ISteamGameServer*>(ctx.This())->LogOn(token);
    return Action::Supersede;
}

HookReturn<void> SteamWorksPlugin::WasRestartRequestedPre(FunctionContext& ctx, bool post)
{
    if (!ctx.CallOriginal<bool>())
    {
        ctx.SetReturn<bool>(false);
        return Action::Supersede;
    }

    ISource2ModScriptForward* forward = g_pSource2ModNatives->FindScriptForward("SteamWorks_RestartRequested");
    const bool handled = forward && forward->GetFunctionCount() > 0 && forward->Call<int>() != 0;
    ctx.SetReturn<bool>(!handled);
    return Action::Supersede;
}

HookReturn<void> SteamWorksPlugin::OnActivated(EmptyContext& ctx, bool post)
{
    RegisterCallbacks();
    return Action::Ignore;
}

HookReturn<void> SteamWorksPlugin::OnDeactivated(EmptyContext& ctx, bool post)
{
    UnregisterCallbacks();
    return Action::Ignore;
}

void SteamWorksPlugin::RegisterCallbacks()
{
    if (m_bRegistered)
        return;
    m_bRegistered = true;

    RegisterUGCCallbacks();
    if (ISteamGameServer* gameServer = SteamGameServer())
    {
        m_hLogOnAnonymous = g_pSource2ModHooks->Hook(HookPoint::Function(m_pfnLogOnAnonymous), HookTarget::Instance(gameServer), { this, &SteamWorksPlugin::LogOnAnonymousPre });
        m_hWasRestartRequested = g_pSource2ModHooks->Hook(HookPoint::Function(m_pfnWasRestartRequested), HookTarget::Instance(gameServer), { this, &SteamWorksPlugin::WasRestartRequestedPre });
    }

    m_cbConnected.Register(this, &SteamWorksPlugin::OnServersConnected);
    m_cbConnectFailure.Register(this, &SteamWorksPlugin::OnServersConnectFailure);
    m_cbDisconnected.Register(this, &SteamWorksPlugin::OnServersDisconnected);
    m_cbValidate.Register(this, &SteamWorksPlugin::OnValidateTicket);
    m_cbGroupStatus.Register(this, &SteamWorksPlugin::OnGroupStatus);
    m_cbStatsReceived.Register(this, &SteamWorksPlugin::OnStatsReceived);
    m_cbStatsStored.Register(this, &SteamWorksPlugin::OnStatsStored);
    m_cbStatsUnloaded.Register(this, &SteamWorksPlugin::OnStatsUnloaded);
}

void SteamWorksPlugin::UnregisterCallbacks()
{
    if (!m_bRegistered)
        return;
    m_bRegistered = false;

    UnregisterUGCCallbacks();
    ClearGeneratedCalls();
    if (g_pGCManager)
        g_pGCManager->OnDeactivated();
    g_pSource2ModHooks->Unhook(m_hLogOnAnonymous);
    g_pSource2ModHooks->Unhook(m_hWasRestartRequested);
    m_hLogOnAnonymous = m_hWasRestartRequested = 0;

    m_cbConnected.Unregister();
    m_cbConnectFailure.Unregister();
    m_cbDisconnected.Unregister();
    m_cbValidate.Unregister();
    m_cbGroupStatus.Unregister();
    m_cbStatsReceived.Unregister();
    m_cbStatsStored.Unregister();
    m_cbStatsUnloaded.Unregister();
}

void SteamWorksPlugin::OnServersConnected(SteamServersConnected_t*)
{
    for (ISteamWorksListener* listener : m_listeners)
        listener->OnSteamServersConnected();
    Fire("SteamWorks_SteamServersConnected");
}

void SteamWorksPlugin::OnServersConnectFailure(SteamServerConnectFailure_t* param)
{
    for (ISteamWorksListener* listener : m_listeners)
        listener->OnSteamServersConnectFailure(param->m_eResult);
    Fire("SteamWorks_SteamServersConnectFailure", static_cast<int>(param->m_eResult));
}

void SteamWorksPlugin::OnServersDisconnected(SteamServersDisconnected_t* param)
{
    for (ISteamWorksListener* listener : m_listeners)
        listener->OnSteamServersDisconnected(param->m_eResult);
    Fire("SteamWorks_SteamServersDisconnected", static_cast<int>(param->m_eResult));
}

void SteamWorksPlugin::OnValidateTicket(ValidateAuthTicketResponse_t* param)
{
    if (param->m_eAuthSessionResponse != k_EAuthSessionResponseOK)
        return;

    m_owners[param->m_SteamID.GetAccountID()] = param->m_OwnerSteamID.GetAccountID();

    for (ISteamWorksListener* listener : m_listeners)
        listener->OnValidateClient(param->m_OwnerSteamID, param->m_SteamID);

    const int owner = static_cast<int>(param->m_OwnerSteamID.GetAccountID());
    const int user = static_cast<int>(param->m_SteamID.GetAccountID());
    Fire("SW_OnValidateClient", owner, user);
    Fire("SteamWorks_OnValidateClient", owner, user);
}

void SteamWorksPlugin::OnGroupStatus(GSClientGroupStatus_t* param)
{
    for (ISteamWorksListener* listener : m_listeners)
        listener->OnClientGroupStatus(param->m_SteamIDUser, param->m_SteamIDGroup, param->m_bMember, param->m_bOfficer);
    Fire("SteamWorks_OnClientGroupStatus", static_cast<int>(param->m_SteamIDUser.GetAccountID()), static_cast<int>(param->m_SteamIDGroup.GetAccountID()),
         param->m_bMember, param->m_bOfficer);
}

void SteamWorksPlugin::OnStatsReceived(GSStatsReceived_t* param)
{
    Fire("SteamWorks_OnUserStatsReceived", static_cast<int>(param->m_steamIDUser.GetAccountID()), static_cast<int>(param->m_eResult));
}

void SteamWorksPlugin::OnStatsStored(GSStatsStored_t* param)
{
    Fire("SteamWorks_OnUserStatsStored", static_cast<int>(param->m_steamIDUser.GetAccountID()), static_cast<int>(param->m_eResult));
}

void SteamWorksPlugin::OnStatsUnloaded(GSStatsUnloaded_t* param)
{
    Fire("SteamWorks_OnUserStatsUnloaded", static_cast<int>(param->m_steamIDUser.GetAccountID()));
}

bool SteamWorksPlugin::IsLoaded() const
{
    return SteamGameServer() != nullptr;
}

bool SteamWorksPlugin::IsConnected() const
{
    return SteamGameServer() && SteamGameServer()->BLoggedOn();
}

ISteamGameServer* SteamWorksPlugin::GameServer()
{
    return SteamGameServer();
}

ISteamGameServerStats* SteamWorksPlugin::Stats()
{
    return SteamGameServerStats();
}

ISteamHTTP* SteamWorksPlugin::HTTP()
{
    return SteamGameServerHTTP();
}

ISteamUGC* SteamWorksPlugin::UGC()
{
    return SteamGameServerUGC();
}

ISteamUtils* SteamWorksPlugin::Utils()
{
    return SteamGameServerUtils();
}

ISteamInventory* SteamWorksPlugin::Inventory()
{
    return SteamGameServerInventory();
}

ISteamNetworking* SteamWorksPlugin::Networking()
{
    return SteamGameServerNetworking();
}

ISteamNetworkingSockets* SteamWorksPlugin::NetworkingSockets()
{
    return SteamGameServerNetworkingSockets_SteamAPI();
}

ISteamNetworkingMessages* SteamWorksPlugin::NetworkingMessages()
{
    return SteamGameServerNetworkingMessages_SteamAPI();
}

ISteamGameCoordinator* SteamWorksPlugin::GameCoordinator()
{
    return g_pGCManager->GetGC();
}

bool SteamWorksPlugin::InjectGCMessage(uint32 msgType, const void* data, uint32 size)
{
    return g_pGCManager->SendRaw(msgType, std::string(static_cast<const char*>(data), size));
}

bool SteamWorksPlugin::SendGCMessage(uint32_t eMsg, const void* body, size_t size, void (*onDelivered)(void*), void* context)
{
    std::function<void()> done;
    if (onDelivered)
        done = [onDelivered, context]() { onDelivered(context); };
    return g_pGCManager->Send(eMsg, body, size, std::move(done));
}

void SteamWorksPlugin::AfterGCQueued(void (*fn)(void*), void* context)
{
    g_pGCManager->AfterQueued([fn, context]() { fn(context); });
}

bool SteamWorksPlugin::HasGCSession() const
{
    return g_pGCManager->HasSession();
}

void SteamWorksPlugin::AddListener(ISteamWorksListener* listener)
{
    if (listener && std::find(m_listeners.begin(), m_listeners.end(), listener) == m_listeners.end())
    {
        m_listeners.push_back(listener);
        g_pGCManager->AddListener(listener);
    }
}

void SteamWorksPlugin::RemoveListener(ISteamWorksListener* listener)
{
    std::erase(m_listeners, listener);
    g_pGCManager->RemoveListener(listener);
}

uint32 SteamWorksPlugin::OwnerOf(uint32 accountId) const
{
    const auto it = m_owners.find(accountId);
    return it != m_owners.end() ? it->second : 0;
}

const char* SteamWorksPlugin::GetAuthor()
{
    return PLUGIN_AUTHOR;
}

const char* SteamWorksPlugin::GetName()
{
    return PLUGIN_DISPLAY_NAME;
}

const char* SteamWorksPlugin::GetDescription()
{
    return PLUGIN_DESCRIPTION;
}

const char* SteamWorksPlugin::GetVersion()
{
    return PLUGIN_FULL_VERSION;
}
