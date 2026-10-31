# SteamWorks_s2m

The Steamworks API for [Source2Mod](https://source2mod.net), for SourcePawn scripts and for other C++ plugins. Without it, a plugin that wants Steam has to link `steam_api` itself and hook the game for callbacks. Forwards and the request handle follow [SM-SteamWorks](https://github.com/alliedmodders/SM-SteamWorks), so scripts ported from CS:GO mostly keep their names.

Status: runs on a Linux dedicated server with bots. Answers that need a real player (group status, licenses, shared games) have not been tried with a real client yet, nor has running next to a plugin that hooks the game coordinator itself, such as an inventory plugin. Such a plugin can swallow messages before this one sees them.

## What you get

- **The interfaces:** `ISteamGameServer`, `ISteamGameServerStats`, `ISteamHTTP`, `ISteamUGC`, `ISteamUtils`, `ISteamInventory`, `ISteamNetworking`, `ISteamNetworkingSockets`, `ISteamNetworkingMessages` and `ISteamGameCoordinator`. In scripts these are one generated methodmap per interface; 92 methods have no safe script form and are listed in [`tools/steamgen_skipped.txt`](tools/steamgen_skipped.txt).
- **Steam's callbacks as forwards:** connection to Steam, client validation with the owner of a shared game, group status, stats, Workshop downloads, restart requests and the logon token.
- **HTTP:** `SteamWorksHTTPRequest`, a request handle that is cleaned up with its script, streaming included.
- **Game coordinator:** watch messages in both directions, and put messages in front of the game.

## Install

Copy the release's `addons` folder into `game/csgo`. The plugin is `steamworks`, and it needs a Source2Mod core with plugin API 9. Load it before the server logs on to Steam if you want to inject GC messages or answer `SteamWorks_TokenRequested`.

## SourcePawn

```sourcepawn
#include <steamworks/steamworks>

public void SteamWorks_SteamServersConnected()
{
    SteamWorksHTTPRequest request = new SteamWorksHTTPRequest(k_EHTTPMethodGET, "https://example.com/motd.txt");
    request.SetCallbacks(OnMotd);
    request.Send();
}

void OnMotd(Handle hRequest, bool bFailure, bool bRequestSuccessful, EHTTPStatusCode eStatusCode)
{
    SteamWorksHTTPRequest request = view_as<SteamWorksHTTPRequest>(hRequest);
    if (!bFailure && IsHTTPStatusSuccess(eStatusCode))
        request.WriteResponseBodyToFile("motd.txt");
    delete request;
}

public void SteamWorks_OnValidateClient(int ownerauthid, int authid)
{
    if (ownerauthid != authid)
        PrintToServer("%d plays a game shared by %d", authid, ownerauthid);
}
```

`steamworks.inc` holds what the interfaces do not cover: forwards, the request handle and the GC natives. It also includes the generated files, `steamgameserver.inc`, `steamugc.inc` and the rest. `tools/steamgen.py` writes those from s2sdk's `steam_api.json`.

To compile against it, add this repo's `public/scripting` to spcomp's include paths, or use the `scripting/include` folder that comes with a release.

## C++

Add `public` to your include paths, then fetch the interface once all plugins are in:

```cpp
#include "steamworks/ISteamWorks.h"

ISteamWorks* g_pSteamWorks = nullptr;

void MyPlugin::OnAllSource2ModPluginsLoaded()
{
    GET_SOURCE2MOD_IFACE(STEAMWORKS_INTERFACE_VERSION, g_pSteamWorks, ISteamWorks);
    g_pSteamWorks->AddListener(this);
}

void MyPlugin::OnValidateClient(CSteamID owner, CSteamID user)
{
    // from ISteamWorksListener; owner differs from user on a shared game
}
```

Getters return `nullptr` while the Steam API is down. Remove your listener before your plugin unloads.

GC listeners get serialized protobuf bytes, not protobuf objects, because those must not cross between plugin libraries:

```cpp
bool MyPlugin::OnGCMessage(uint32_t eMsg, ISteamWorksGCMessage& msg)
{
    // parse msg.GetBody() / msg.GetBodySize() with your own protobufs
    return true; // false drops it
}
```

## Game coordinator and `FollowCS2ServerGuidelines`

Watching GC messages always works. Injecting a message, changing one or dropping one is refused while `FollowCS2ServerGuidelines` is on in `core.json`, which is the default. A faked GC message can fake an inventory, and that can get a server's GSLTs banned. Only turn the option off if you accept that risk.

Injecting also needs the game's GC client to have registered, so the plugin has to be loaded before the Steam logon. Delivery happens on a game frame, so nothing arrives while the server hibernates.

## Building

Needs the Source2Mod SDK, s2sdk and Metamod:Source, with `SOURCE2MOD_SDK`, `S2SDK`, `MMSOURCE_DEV` and `CSGO_PROTO` set. CMake is for local work and AMBuild for releases; a line in `pushbuild.txt` makes CI build and publish one.
