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
