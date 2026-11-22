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

/*
 * Workshop items on the game server's ISteamUGC. Item ids are decimal text.
 */
#include "plugin.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace SourcePawn;

void ForgetGCScript(PluginId id);

namespace
{
    PublishedFileId_t ItemId(IPluginContext* ctx, cell_t addr)
    {
        char* s = nullptr;
        return ctx->LocalToString(addr, &s) == SP_ERROR_NONE && s ? std::strtoull(s, nullptr, 10) : 0;
    }

    struct DetailsQuery
    {
        PluginId owner;
        IPluginFunction* fn;
        cell_t data;
        PublishedFileId_t item;
        CCallResult<DetailsQuery, SteamUGCQueryCompleted_t> result;

        void OnResult(SteamUGCQueryCompleted_t* param, bool failure);
    };

    std::vector<std::unique_ptr<DetailsQuery>> g_queries;

    void DetailsQuery::OnResult(SteamUGCQueryCompleted_t* param, bool failure)
    {
        ISteamUGC* ugc = SteamGameServerUGC();
        SteamUGCDetails_t details{};
        const bool found = !failure && ugc && param->m_eResult == k_EResultOK && param->m_unNumResultsReturned > 0 && ugc->GetQueryUGCResult(param->m_handle, 0, &details);
        if (ugc)
            ugc->ReleaseQueryUGCRequest(param->m_handle);

        const std::string id = std::to_string(item);
        const EResult code = failure ? k_EResultFail : found ? details.m_eResult : param->m_eResult;
        fn->PushString(id.c_str());
        fn->PushCell(code);
        fn->PushString(found ? details.m_rgchTitle : "");
        fn->PushCell(found ? static_cast<cell_t>(details.m_rtimeUpdated) : 0);
        fn->PushCell(found ? static_cast<cell_t>(details.m_nFileSize) : 0);
        fn->PushCell(data);
        fn->Execute(nullptr);

        std::erase_if(g_queries, [this](const auto& q) { return q.get() == this; });
    }

    // QueryItemDetails(item, callback, data)
    cell_t N_QueryItemDetails(IPluginContext* ctx, const cell_t* params)
    {
        ISteamUGC* ugc = SteamGameServerUGC();
        IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[2]));
        if (!ugc || !fn)
            return 0;
        PublishedFileId_t item = ItemId(ctx, params[1]);
        const UGCQueryHandle_t query = ugc->CreateQueryUGCDetailsRequest(&item, 1);
        if (query == k_UGCQueryHandleInvalid)
            return 0;
        const SteamAPICall_t call = ugc->SendQueryUGCRequest(query);
        if (call == k_uAPICallInvalid)
        {
            ugc->ReleaseQueryUGCRequest(query);
            return 0;
        }
        auto entry = std::make_unique<DetailsQuery>();
        entry->owner = g_pSource2ModNatives->GetScriptPlugin(ctx);
        entry->fn = fn;
        entry->data = params[0] >= 3 ? params[3] : 0;
        entry->item = item;
        entry->result.SetGameserverFlag();
        entry->result.Set(call, entry.get(), &DetailsQuery::OnResult);
        g_queries.push_back(std::move(entry));
        return 1;
    }

    const sp_nativeinfo_t g_natives[] = {
        { "SteamWorks_QueryItemDetails", N_QueryItemDetails },
        { nullptr, nullptr },
    };

    struct Downloads
    {
        CCallbackManual<Downloads, DownloadItemResult_t, true> callback;

        void OnDownloaded(DownloadItemResult_t* param)
        {
            ISource2ModScriptForward* forward = g_pSource2ModNatives->FindScriptForward("SteamWorks_OnWorkshopItemDownloaded");
            if (forward && forward->GetFunctionCount() > 0)
                forward->Call<void>(std::to_string(param->m_nPublishedFileId), static_cast<int>(param->m_eResult));
        }
    };

    Downloads g_downloads;
    bool g_registered = false;
}

void RegisterUGCNatives()
{
    g_pSource2ModNatives->AddNatives(g_natives);
}

void RegisterUGCCallbacks()
{
    if (g_registered)
        return;
    g_registered = true;
    g_downloads.callback.Register(&g_downloads, &Downloads::OnDownloaded);
}

void UnregisterUGCCallbacks()
{
    if (!g_registered)
        return;
    g_registered = false;
    g_downloads.callback.Unregister();
    g_queries.clear();
}

void ForgetScript(PluginId id)
{
    ForgetGCScript(id);
    std::erase_if(g_queries, [id](const auto& q) { return q->owner == id; });
}
