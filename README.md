# SteamWorks_s2m

The Steamworks API for Source2Mod: natives for SourcePawn scripts (`public/scripting/SteamWorks.inc`) and `ISteamWorks` for C++ plugins (`public/ISteamWorks.h`). Names and behaviour follow [SM-SteamWorks](https://github.com/alliedmodders/SM-SteamWorks), with a client being a player slot.

Served: server state, server browser data, group status, licenses, family sharing, player stats, HTTP, Workshop items, the game coordinator. Injecting game coordinator messages is refused while core.json's `FollowCS2ServerGuidelines` is on, the default.

C++ plugins watching the game coordinator derive from `IGCSOListener` (`public/IGCSOListener.h`) and register with `ISteamWorks::AddGCListener`; `SendGCMessage` puts a protobuf message in front of the game as if the GC had sent it.
