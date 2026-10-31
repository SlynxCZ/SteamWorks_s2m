/*
 * SourcePawn natives for SteamWorks.inc. Names and behaviour after
 * alliedmodders/SM-SteamWorks; a client is a player slot.
 */
#include "plugin.h"

#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingfakeip.h>

#include <cstdlib>
#include <string>

using namespace SourcePawn;

namespace
{
    const char* Str(IPluginContext* ctx, cell_t addr)
    {
        char* s = nullptr;
        return ctx->LocalToString(addr, &s) == SP_ERROR_NONE && s ? s : "";
    }

    CSteamID FromAccount(cell_t accountId, EAccountType type = k_EAccountTypeIndividual)
    {
        return CSteamID(static_cast<uint32>(accountId), k_EUniversePublic, type);
    }

    CSteamID FromSlot(cell_t slot)
    {
        const std::string id = g_pSource2ModClients->GetAuthId(CPlayerSlot(slot), SOURCE2MOD_AUTHID_STEAMID64, false);
        return CSteamID(static_cast<uint64>(std::strtoull(id.c_str(), nullptr, 10)));
    }

    cell_t N_IsLoaded(IPluginContext*, const cell_t*)
    {
        return g_Plugin.IsLoaded();
    }

    cell_t N_IsConnected(IPluginContext*, const cell_t*)
    {
        return g_Plugin.IsConnected();
    }

    cell_t N_IsVACEnabled(IPluginContext*, const cell_t*)
    {
        ISteamGameServer* gs = SteamGameServer();
        return gs && gs->BSecure();
    }

    // GetPublicIP(int ipaddr[4])
    cell_t N_GetPublicIP(IPluginContext* ctx, const cell_t* params)
    {
        cell_t* out = nullptr;
        if (ctx->LocalToPhysAddr(params[1], &out) != SP_ERROR_NONE)
            return 0;
        ISteamGameServer* gs = SteamGameServer();
        const uint32 ip = gs ? gs->GetPublicIP().m_unIPv4 : 0;
        for (int i = 0; i < 4; i++)
            out[i] = (ip >> (24 - i * 8)) & 0xFF;
        return ip != 0;
    }

    cell_t N_GetPublicIPCell(IPluginContext*, const cell_t*)
    {
        ISteamGameServer* gs = SteamGameServer();
        return gs ? static_cast<cell_t>(gs->GetPublicIP().m_unIPv4) : 0;
    }

    // GetFakeIP(&ip, &port)
    cell_t N_GetFakeIP(IPluginContext* ctx, const cell_t* params)
    {
        cell_t* ip = nullptr;
        cell_t* port = nullptr;
        if (ctx->LocalToPhysAddr(params[1], &ip) != SP_ERROR_NONE || ctx->LocalToPhysAddr(params[2], &port) != SP_ERROR_NONE)
            return 0;
        *ip = *port = 0;
        ISteamNetworkingSockets* sockets = SteamGameServerNetworkingSockets_SteamAPI();
        if (!sockets)
            return 0;
        SteamNetworkingFakeIPResult_t result{};
        sockets->GetFakeIP(0, &result);
        if (result.m_eResult != k_EResultOK)
            return 0;
        *ip = static_cast<cell_t>(result.m_unIP);
        *port = result.m_unPorts[0];
        return 1;
    }

    template <void (ISteamGameServer::*Set)(const char*)>
    cell_t N_SetString(IPluginContext* ctx, const cell_t* params)
    {
        ISteamGameServer* gs = SteamGameServer();
        if (!gs)
            return 0;
        (gs->*Set)(Str(ctx, params[1]));
        return 1;
    }

    // SetRule(key, value)
    cell_t N_SetRule(IPluginContext* ctx, const cell_t* params)
    {
        ISteamGameServer* gs = SteamGameServer();
        if (!gs)
            return 0;
        gs->SetKeyValue(Str(ctx, params[1]), Str(ctx, params[2]));
        return 1;
    }

    cell_t N_ClearRules(IPluginContext*, const cell_t*)
    {
        ISteamGameServer* gs = SteamGameServer();
        if (!gs)
            return 0;
        gs->ClearAllKeyValues();
        return 1;
    }

    cell_t N_SetAdvertiseServerActive(IPluginContext*, const cell_t* params)
    {
        ISteamGameServer* gs = SteamGameServer();
        if (!gs)
            return 0;
        gs->SetAdvertiseServerActive(params[1] != 0);
        return 1;
    }

    cell_t N_SetBotCount(IPluginContext*, const cell_t* params)
    {
        ISteamGameServer* gs = SteamGameServer();
        if (!gs)
            return 0;
        gs->SetBotPlayerCount(params[1]);
        return 1;
    }

    // Removed from the Steamworks SDK; heartbeats are Steam's own business now.
    cell_t N_ForceHeartbeat(IPluginContext*, const cell_t*)
    {
        return 0;
    }

    cell_t GroupStatus(CSteamID user, cell_t group)
    {
        ISteamGameServer* gs = SteamGameServer();
        if (!gs || !gs->BLoggedOn() || !user.IsValid())
            return 0;
        return gs->RequestUserGroupStatus(user, FromAccount(group, k_EAccountTypeClan));
    }

    cell_t N_GetUserGroupStatus(IPluginContext*, const cell_t* params)
    {
        return GroupStatus(FromSlot(params[1]), params[2]);
    }

    cell_t N_GetUserGroupStatusAuthID(IPluginContext*, const cell_t* params)
    {
        return GroupStatus(FromAccount(params[1]), params[2]);
    }

    cell_t HasLicense(CSteamID user, cell_t app)
    {
        ISteamGameServer* gs = SteamGameServer();
        if (!gs || !user.IsValid())
            return k_EUserHasLicenseResultNoAuth;
        return gs->UserHasLicenseForApp(user, static_cast<AppId_t>(app));
    }

    cell_t N_HasLicenseForApp(IPluginContext*, const cell_t* params)
    {
        return HasLicense(FromSlot(params[1]), params[2]);
    }

    cell_t N_HasLicenseForAppId(IPluginContext*, const cell_t* params)
    {
        return HasLicense(FromAccount(params[1]), params[2]);
    }

    // GetClientSteamID(slot, buffer, maxlen)
    cell_t N_GetClientSteamID(IPluginContext* ctx, const cell_t* params)
    {
        const std::string id = g_pSource2ModClients->GetAuthId(CPlayerSlot(params[1]), SOURCE2MOD_AUTHID_STEAMID64, false);
        size_t written = 0;
        ctx->StringToLocalUTF8(params[2], params[3], id.c_str(), &written);
        return static_cast<cell_t>(written);
    }

    // GetServerSteamID(buffer, maxlen)
    cell_t N_GetServerSteamID(IPluginContext* ctx, const cell_t* params)
    {
        ISteamGameServer* gs = SteamGameServer();
        const std::string id = gs ? std::to_string(gs->GetSteamID().ConvertToUint64()) : "";
        size_t written = 0;
        ctx->StringToLocalUTF8(params[1], params[2], id.c_str(), &written);
        return static_cast<cell_t>(written);
    }

    // GetClientOwnerAuthID(slot, &owner)
    cell_t N_GetClientOwnerAuthID(IPluginContext* ctx, const cell_t* params)
    {
        cell_t* out = nullptr;
        if (ctx->LocalToPhysAddr(params[2], &out) != SP_ERROR_NONE)
            return 0;
        *out = static_cast<cell_t>(g_Plugin.OwnerOf(FromSlot(params[1]).GetAccountID()));
        return *out != 0;
    }

    cell_t RequestStats(CSteamID user)
    {
        ISteamGameServerStats* stats = SteamGameServerStats();
        return stats && user.IsValid() && stats->RequestUserStats(user) != k_uAPICallInvalid;
    }

    // RequestStats(slot, appid): the app is the server's own, as the SDK has it.
    cell_t N_RequestStats(IPluginContext*, const cell_t* params)
    {
        return RequestStats(FromSlot(params[1]));
    }

    cell_t N_RequestStatsAuthID(IPluginContext*, const cell_t* params)
    {
        return RequestStats(FromAccount(params[1]));
    }

    cell_t StatCell(IPluginContext* ctx, CSteamID user, const cell_t* params)
    {
        cell_t* out = nullptr;
        ISteamGameServerStats* stats = SteamGameServerStats();
        if (!stats || ctx->LocalToPhysAddr(params[3], &out) != SP_ERROR_NONE)
            return 0;
        int32 value = 0;
        const bool ok = stats->GetUserStat(user, Str(ctx, params[2]), &value);
        *out = value;
        return ok;
    }

    cell_t StatFloat(IPluginContext* ctx, CSteamID user, const cell_t* params)
    {
        cell_t* out = nullptr;
        ISteamGameServerStats* stats = SteamGameServerStats();
        if (!stats || ctx->LocalToPhysAddr(params[3], &out) != SP_ERROR_NONE)
            return 0;
        float value = 0.0f;
        const bool ok = stats->GetUserStat(user, Str(ctx, params[2]), &value);
        *out = sp_ftoc(value);
        return ok;
    }

    cell_t N_GetStatCell(IPluginContext* ctx, const cell_t* params)
    {
        return StatCell(ctx, FromSlot(params[1]), params);
    }

    cell_t N_GetStatAuthIDCell(IPluginContext* ctx, const cell_t* params)
    {
        return StatCell(ctx, FromAccount(params[1]), params);
    }

    cell_t N_GetStatFloat(IPluginContext* ctx, const cell_t* params)
    {
        return StatFloat(ctx, FromSlot(params[1]), params);
    }

    cell_t N_GetStatAuthIDFloat(IPluginContext* ctx, const cell_t* params)
    {
        return StatFloat(ctx, FromAccount(params[1]), params);
    }

    // GetUserAchievement(authid, name, &achieved)
    cell_t N_GetUserAchievement(IPluginContext* ctx, const cell_t* params)
    {
        cell_t* out = nullptr;
        ISteamGameServerStats* stats = SteamGameServerStats();
        if (!stats || ctx->LocalToPhysAddr(params[3], &out) != SP_ERROR_NONE)
            return 0;
        bool achieved = false;
        const bool ok = stats->GetUserAchievement(FromAccount(params[1]), Str(ctx, params[2]), &achieved);
        *out = achieved;
        return ok;
    }

    const sp_nativeinfo_t g_natives[] = {
        { "SteamWorks_IsLoaded", N_IsLoaded },
        { "SteamWorks_IsConnected", N_IsConnected },
        { "SteamWorks_IsVACEnabled", N_IsVACEnabled },
        { "SteamWorks_GetPublicIP", N_GetPublicIP },
        { "SteamWorks_GetPublicIPCell", N_GetPublicIPCell },
        { "SteamWorks_GetFakeIP", N_GetFakeIP },
        { "SteamWorks_SetGameData", N_SetString<&ISteamGameServer::SetGameData> },
        { "SteamWorks_SetGameDescription", N_SetString<&ISteamGameServer::SetGameDescription> },
        { "SteamWorks_SetMapName", N_SetString<&ISteamGameServer::SetMapName> },
        { "SteamWorks_SetGameTags", N_SetString<&ISteamGameServer::SetGameTags> },
        { "SteamWorks_SetServerName", N_SetString<&ISteamGameServer::SetServerName> },
        { "SteamWorks_SetRegion", N_SetString<&ISteamGameServer::SetRegion> },
        { "SteamWorks_SetRule", N_SetRule },
        { "SteamWorks_ClearRules", N_ClearRules },
        { "SteamWorks_SetAdvertiseServerActive", N_SetAdvertiseServerActive },
        { "SteamWorks_SetBotCount", N_SetBotCount },
        { "SteamWorks_ForceHeartbeat", N_ForceHeartbeat },
        { "SteamWorks_GetUserGroupStatus", N_GetUserGroupStatus },
        { "SteamWorks_GetUserGroupStatusAuthID", N_GetUserGroupStatusAuthID },
        { "SteamWorks_HasLicenseForApp", N_HasLicenseForApp },
        { "SteamWorks_HasLicenseForAppId", N_HasLicenseForAppId },
        { "SteamWorks_GetClientSteamID", N_GetClientSteamID },
        { "SteamWorks_GetServerSteamID", N_GetServerSteamID },
        { "SteamWorks_GetClientOwnerAuthID", N_GetClientOwnerAuthID },
        { "SteamWorks_RequestStats", N_RequestStats },
        { "SteamWorks_RequestStatsAuthID", N_RequestStatsAuthID },
        { "SteamWorks_GetStatCell", N_GetStatCell },
        { "SteamWorks_GetStatAuthIDCell", N_GetStatAuthIDCell },
        { "SteamWorks_GetStatFloat", N_GetStatFloat },
        { "SteamWorks_GetStatAuthIDFloat", N_GetStatAuthIDFloat },
        { "SteamWorks_GetUserAchievement", N_GetUserAchievement },
        { nullptr, nullptr },
    };
}

void RegisterSteamWorksNatives()
{
    g_pSource2ModNatives->AddNatives(g_natives);
}
