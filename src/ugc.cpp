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

    // DownloadItem(item, highPriority)
    cell_t N_DownloadItem(IPluginContext* ctx, const cell_t* params)
    {
        ISteamUGC* ugc = SteamGameServerUGC();
        return ugc && ugc->DownloadItem(ItemId(ctx, params[1]), params[2] != 0);
    }

    cell_t N_GetItemState(IPluginContext* ctx, const cell_t* params)
    {
        ISteamUGC* ugc = SteamGameServerUGC();
        return ugc ? static_cast<cell_t>(ugc->GetItemState(ItemId(ctx, params[1]))) : 0;
    }

    // GetItemInstallInfo(item, folder, maxlen, int size[2], &timestamp)
    cell_t N_GetItemInstallInfo(IPluginContext* ctx, const cell_t* params)
    {
        ISteamUGC* ugc = SteamGameServerUGC();
        char* folder = nullptr;
        cell_t* size = nullptr;
        cell_t* stamp = nullptr;
        if (!ugc || ctx->LocalToString(params[2], &folder) != SP_ERROR_NONE || ctx->LocalToPhysAddr(params[4], &size) != SP_ERROR_NONE || ctx->LocalToPhysAddr(params[5], &stamp) != SP_ERROR_NONE)
            return 0;
        uint64 bytes = 0;
        uint32 timestamp = 0;
        if (!ugc->GetItemInstallInfo(ItemId(ctx, params[1]), &bytes, folder, static_cast<uint32>(params[3]), &timestamp))
            return 0;
        size[0] = static_cast<cell_t>(bytes & 0xFFFFFFFFu);
        size[1] = static_cast<cell_t>(bytes >> 32);
        *stamp = static_cast<cell_t>(timestamp);
        return 1;
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
        { "SteamWorks_DownloadItem", N_DownloadItem },
        { "SteamWorks_GetItemState", N_GetItemState },
        { "SteamWorks_GetItemInstallInfo", N_GetItemInstallInfo },
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
