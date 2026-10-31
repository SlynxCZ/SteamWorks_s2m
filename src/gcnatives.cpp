/*
 * SourcePawn natives for the game coordinator: SteamWorks_SendMessageToGC, and
 * messages the game reads as if the GC had sent them.
 */
#include "gc.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#ifdef _WIN32
#undef SendMessage
#endif

using namespace SourcePawn;

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
        ISteamGameCoordinator* gc = g_pGCManager->GetGC();
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
        return g_pGCManager->SendRaw(static_cast<uint32>(params[1]), std::move(data), DeliveredFn(ctx, params[4], params[5]));
    }

    // InjectGCProtoMessage(msgId, body, length, onDelivered, value)
    cell_t N_InjectGCProtoMessage(IPluginContext* ctx, const cell_t* params)
    {
        return g_pGCManager->Send(static_cast<uint32>(params[1]), Str(ctx, params[2]), static_cast<size_t>(params[3]), DeliveredFn(ctx, params[4], params[5]));
    }

    // GetGCMessage(buffer, maxlen): the message of the forward running now, bytes as they are.
    cell_t N_GetGCMessage(IPluginContext* ctx, const cell_t* params)
    {
        const std::string* current = g_pGCManager->Current();
        char* out = nullptr;
        if (!current || ctx->LocalToString(params[1], &out) != SP_ERROR_NONE || !out)
            return 0;
        const size_t length = (std::min)(current->size(), static_cast<size_t>(params[2]));
        std::memcpy(out, current->data(), length);
        return static_cast<cell_t>(length);
    }

    cell_t N_CanInjectGC(IPluginContext*, const cell_t*)
    {
        return g_pGCManager->CanSend();
    }

    cell_t N_HasGCSession(IPluginContext*, const cell_t*)
    {
        return g_pGCManager->HasSession();
    }

    const sp_nativeinfo_t g_natives[] = {
        { "SteamWorks_SendMessageToGC", N_SendMessageToGC },
        { "SteamWorks_InjectGCMessage", N_InjectGCMessage },
        { "SteamWorks_InjectGCProtoMessage", N_InjectGCProtoMessage },
        { "SteamWorks_GetGCMessage", N_GetGCMessage },
        { "SteamWorks_CanInjectGC", N_CanInjectGC },
        { "SteamWorks_HasGCSession", N_HasGCSession },
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
