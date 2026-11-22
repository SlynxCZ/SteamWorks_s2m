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

#pragma once

#include <source2mod/ISource2ModApi.h>
#include <source2mod/ISource2ModPlugin.h>
#include "source2mod/ISource2ModCommands.h"
#include "source2mod/ISource2ModConVars.h"
#include "source2mod/ISource2ModCustomHud.h"
#include "source2mod/ISource2ModEntities.h"
#include "source2mod/ISource2ModEvents.h"
#include "source2mod/ISource2ModGameConfig.h"
#include "source2mod/ISource2ModGameSystems.h"
#include "source2mod/ISource2ModHTTP.h"
#include "source2mod/ISource2ModJSON.h"
#include "source2mod/ISource2ModMemory.h"
#include "source2mod/ISource2ModMenus.h"
#include "source2mod/ISource2ModModule.h"
#include "source2mod/ISource2ModSQL.h"
#include "source2mod/ISource2ModNetworkMessages.h"
#include "source2mod/ISource2ModScheduler.h"
#include "source2mod/ISource2ModSounds.h"
#include "source2mod/ISource2ModTrace.h"
#include "source2mod/ISource2ModClients.h"
#include "source2mod/ISource2ModHooks.h"
#include "source2mod/ISource2ModNatives.h"

#include "source2mod/ISource2ModPaths.h"
#include "source2mod/ISource2ModFunctions.h"
#include "source2mod/ISource2ModGameSystems.h"
#include "steamworks/ISteamWorks.h"


#include <unordered_map>
#include <vector>

SOURCE2MOD_GLOBALVARS();

/**
 * Serves SteamWorks.inc to scripts and ISteamWorks to other plugins: Steam's
 * callbacks become forwards and listener calls here, plugins never touch a
 * Steam object.
 */
class SteamWorksPlugin final : public ISource2ModPlugin,
                               public ISource2ModListener,
                               public ISteamWorks
{
public:
    bool Load(ISource2ModAPI* api, char* error, size_t maxlen, bool late) override;
    bool Unload(char* error, size_t maxlen) override;

    // ISource2ModListener
    void* OnSource2ModQuery(const char* iface, int* ret) override;
    void OnPluginUnload(PluginId id, const char* name) override;

    // ISteamWorks
    bool IsLoaded() const override;
    bool IsConnected() const override;
    ISteamGameServer* GameServer() override;
    ISteamGameServerStats* Stats() override;
    ISteamHTTP* HTTP() override;
    ISteamUGC* UGC() override;
    ISteamUtils* Utils() override;
    ISteamInventory* Inventory() override;
    ISteamNetworking* Networking() override;
    ISteamNetworkingSockets* NetworkingSockets() override;
    ISteamNetworkingMessages* NetworkingMessages() override;
    ISteamGameCoordinator* GameCoordinator() override;
    bool InjectGCMessage(uint32 msgType, const void* data, uint32 size) override;
    bool SendGCMessage(uint32_t eMsg, const void* body, size_t size, void (*onDelivered)(void*), void* context) override;
    void AfterGCQueued(void (*fn)(void*), void* context) override;
    bool HasGCSession() const override;
    void AddListener(ISteamWorksListener* listener) override;
    void RemoveListener(ISteamWorksListener* listener) override;

    /// Who owns the game a client plays, by the client's account id; 0 before Steam said.
    uint32 OwnerOf(uint32 accountId) const;

    const char* GetAuthor() override;
    const char* GetName() override;
    const char* GetDescription() override;
    const char* GetVersion() override;

private:
    HookReturn<void> OnActivated(EmptyContext& ctx, bool post);
    HookReturn<void> OnDeactivated(EmptyContext& ctx, bool post);

    HookReturn<void> LogOnAnonymousPre(FunctionContext& ctx, bool post);
    HookReturn<void> WasRestartRequestedPre(FunctionContext& ctx, bool post);

    void RegisterCallbacks();
    void UnregisterCallbacks();

    void OnServersConnected(SteamServersConnected_t* param);
    void OnServersConnectFailure(SteamServerConnectFailure_t* param);
    void OnServersDisconnected(SteamServersDisconnected_t* param);
    void OnValidateTicket(ValidateAuthTicketResponse_t* param);
    void OnGroupStatus(GSClientGroupStatus_t* param);
    void OnStatsReceived(GSStatsReceived_t* param);
    void OnStatsStored(GSStatsStored_t* param);
    void OnStatsUnloaded(GSStatsUnloaded_t* param);

    CCallbackManual<SteamWorksPlugin, SteamServersConnected_t, true> m_cbConnected;
    CCallbackManual<SteamWorksPlugin, SteamServerConnectFailure_t, true> m_cbConnectFailure;
    CCallbackManual<SteamWorksPlugin, SteamServersDisconnected_t, true> m_cbDisconnected;
    CCallbackManual<SteamWorksPlugin, ValidateAuthTicketResponse_t, true> m_cbValidate;
    CCallbackManual<SteamWorksPlugin, GSClientGroupStatus_t, true> m_cbGroupStatus;
    CCallbackManual<SteamWorksPlugin, GSStatsReceived_t, true> m_cbStatsReceived;
    CCallbackManual<SteamWorksPlugin, GSStatsStored_t, true> m_cbStatsStored;
    CCallbackManual<SteamWorksPlugin, GSStatsUnloaded_t, true> m_cbStatsUnloaded;
    bool m_bRegistered = false;

    ISource2ModFunction* m_pfnLogOnAnonymous = nullptr;
    ISource2ModFunction* m_pfnWasRestartRequested = nullptr;
    Source2ModHookId m_hLogOnAnonymous = 0;
    Source2ModHookId m_hWasRestartRequested = 0;

    std::vector<ISteamWorksListener*> m_listeners;
    std::unordered_map<uint32, uint32> m_owners;
};

extern SteamWorksPlugin g_Plugin;

void RegisterSteamWorksNatives();
void RegisterGCNatives();
void RegisterHTTPNatives();
void RegisterUGCNatives();
void RegisterGeneratedNatives();
void ForgetGeneratedCalls(PluginId id);
void ClearGeneratedCalls();
void RegisterUGCCallbacks();
void UnregisterUGCCallbacks();

/// Callbacks of a script that goes must not be called any more.
void ForgetScript(PluginId id);
