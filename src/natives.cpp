/*
 * SourcePawn natives of steamworks.inc that are not an interface's: see tools/steamgen.py for those.
 */
#include "plugin.h"

#include <cstdlib>
#include <string>

using namespace SourcePawn;

namespace
{
    cell_t N_IsLoaded(IPluginContext*, const cell_t*)
    {
        return g_Plugin.IsLoaded();
    }

    // GetClientOwnerAuthID(slot, &owner)
    cell_t N_GetClientOwnerAuthID(IPluginContext* ctx, const cell_t* params)
    {
        cell_t* out = nullptr;
        if (ctx->LocalToPhysAddr(params[2], &out) != SP_ERROR_NONE)
            return 0;
        const std::string id = g_pSource2ModClients->GetAuthId(CPlayerSlot(params[1]), SOURCE2MOD_AUTHID_STEAMID64, false);
        *out = static_cast<cell_t>(g_Plugin.OwnerOf(CSteamID(static_cast<uint64>(std::strtoull(id.c_str(), nullptr, 10))).GetAccountID()));
        return *out != 0;
    }

    const sp_nativeinfo_t g_natives[] = {
        { "SteamWorks_IsLoaded", N_IsLoaded },
        { "SteamWorks_GetClientOwnerAuthID", N_GetClientOwnerAuthID },
        { nullptr, nullptr },
    };
}

void RegisterSteamWorksNatives()
{
    g_pSource2ModNatives->AddNatives(g_natives);
}
