#!/usr/bin/env python3
"""Generates the SourcePawn side of the Steamworks API from s2sdk's steam_api.json.

    python3 tools/steamgen.py <s2sdk>

    src/gen/steamnatives.gen.cpp    the natives, called through the game server's accessors
    public/scripting/SteamAPI.inc   a methodmap per interface (ISteamUGC -> SteamUGC), static natives
    tools/steamgen_skipped.txt      what has no safe script form, and why

Script forms: 64-bit values and SteamIDs are decimal text; a text or 64-bit return
value is a buffer and its size after the parameters; a call result is a callback
and a data cell after the parameters, the native answering whether the call went out.
"""
import json
import os
import re
import sys

INTERFACES = {
    "ISteamGameServer": ("SteamGameServer", "SteamGameServer"),
    "ISteamGameServerStats": ("SteamGameServerStats", "SteamGameServerStats"),
    "ISteamUtils": ("SteamUtils", "SteamGameServerUtils"),
    "ISteamUGC": ("SteamUGC", "SteamGameServerUGC"),
    "ISteamInventory": ("SteamInventory", "SteamGameServerInventory"),
    "ISteamNetworking": ("SteamNetworking", "SteamGameServerNetworking"),
    "ISteamNetworkingSockets": ("SteamNetworkingSockets", "SteamGameServerNetworkingSockets_SteamAPI"),
    "ISteamNetworkingMessages": ("SteamNetworkingMessages", "SteamGameServerNetworkingMessages_SteamAPI"),
}

# The server's own session and identity are the engine's: a script calling these breaks the server.
EXCLUDED = {
    "LogOn", "LogOnAnonymous", "LogOff", "SetDedicatedServer", "SetProduct", "SetModDir", "SetSpectatorPort",
    "BeginAuthSession", "EndAuthSession", "CancelAuthTicket", "GetAuthSessionTicket", "RunCallbacks", "RunFrame",
    "SendUserConnectAndAuthenticate_DEPRECATED", "CreateUnauthenticatedUserConnection", "SendUserDisconnect_DEPRECATED",
    "HandleIncomingPacket", "GetNextOutgoingPacket", "BUpdateUserData",
}

RESERVED = {"in", "out", "forward", "function", "public", "native", "any", "this", "char", "int", "float", "bool", "new", "delete", "view_as"}


class Types:
    def __init__(self, api):
        self.typedefs = {t["typedef"]: t["type"] for t in api["typedefs"]}
        self.enums = {e["enumname"] for e in api["enums"]}
        for i in api["interfaces"]:
            for e in i.get("enums", []):
                self.enums.add(e["enumname"])
                self.enums.add(i["classname"] + "::" + e["enumname"])

    def resolve(self, t):
        t = t.strip()
        seen = set()
        while t in self.typedefs and t not in seen:
            seen.add(t)
            t = self.typedefs[t].strip()
        return t

    def kind(self, t):
        """int, bool, float, u64, steamid, enum, or None."""
        if t == "CSteamID":
            return "steamid"
        r = self.resolve(t)
        if r in ("bool",):
            return "bool"
        if r in ("float", "double"):
            return "float"
        if r in ("int", "unsigned int", "int32", "uint32", "short", "unsigned short", "int16", "uint16", "char", "signed char", "unsigned char", "int8", "uint8"):
            return "int"
        if r in ("long long", "unsigned long long", "int64", "uint64", "uint64_t", "int64_t"):
            return "u64"
        if t in self.enums or r in self.enums:
            return "enum"
        return None


def spname(name):
    name = re.sub(r"[^A-Za-z0-9_]", "_", name)
    return name + "_" if name in RESERVED else name


class Method:
    def __init__(self):
        self.sp = []        # script parameters
        self.setup = []     # C++ before the call
        self.args = []      # C++ arguments
        self.after = []     # C++ after the call
        self.index = 1      # the next params[] slot


def build(m, types):
    """A Method, or the reason there is none."""
    out = Method()
    params = m["params"]
    for n, p in enumerate(params):
        t = p["paramtype"].strip()
        name = spname(p["paramname"])
        i = out.index
        var = "a%d" % n
        attrs = set(p.keys()) - {"paramname", "paramtype", "paramtype_flat", "desc"}
        kind = types.kind(t)
        if kind in ("int", "enum"):
            out.sp.append("int %s" % name)
            out.args.append("static_cast<%s>(params[%d])" % (t, i))
            out.index += 1
        elif kind == "bool":
            out.sp.append("bool %s" % name)
            out.args.append("params[%d] != 0" % i)
            out.index += 1
        elif kind == "float":
            out.sp.append("float %s" % name)
            out.args.append("static_cast<%s>(sp_ctof(params[%d]))" % (t, i))
            out.index += 1
        elif kind == "u64":
            out.sp.append("const char[] %s" % name)
            out.args.append("static_cast<%s>(U64(ctx, params[%d]))" % (t, i))
            out.index += 1
        elif kind == "steamid":
            out.sp.append("const char[] %s" % name)
            out.args.append("CSteamID(static_cast<uint64>(U64(ctx, params[%d])))" % i)
            out.index += 1
        elif t == "const char *":
            if attrs:
                return "const char* with %s" % sorted(attrs)
            out.sp.append("const char[] %s" % name)
            out.args.append("Str(ctx, params[%d])" % i)
            out.index += 1
        elif t == "char *":
            if "out_string_count" not in p:
                return "char* without a size"
            out.sp.append("char[] %s" % name)
            out.setup.append("char* %s = nullptr; if (ctx->LocalToString(params[%d], &%s) != SP_ERROR_NONE) return 0;" % (var, i, var))
            out.args.append(var)
            out.index += 1
        elif t in ("const void *", "void *"):
            nxt = params[n + 1] if n + 1 < len(params) else None
            if not nxt or types.kind(nxt["paramtype"]) != "int":
                return "%s without a size after it" % t
            if t == "const void *":
                out.sp.append("const char[] %s" % name)
                out.args.append("Str(ctx, params[%d])" % i)
            else:
                out.sp.append("char[] %s" % name)
                out.setup.append("char* %s = nullptr; if (ctx->LocalToString(params[%d], &%s) != SP_ERROR_NONE) return 0;" % (var, i, var))
                out.args.append(var)
            out.index += 1
        elif t.endswith("*") and not t.startswith("const"):
            base = t[:-1].strip()
            k = types.kind(base)
            if attrs & {"out_array_count", "out_array_call", "array_count", "out_struct", "out_buffer_count"}:
                return "array parameter %s" % t
            if k in ("int", "enum", "bool"):
                out.sp.append(("bool &%s" if k == "bool" else "int &%s") % name)
                out.setup.append("cell_t* c%d = nullptr; if (ctx->LocalToPhysAddr(params[%d], &c%d) != SP_ERROR_NONE) return 0; %s %s{};" % (n, i, n, base, var))
                out.args.append("&" + var)
                out.after.append("*c%d = static_cast<cell_t>(%s);" % (n, var))
                out.index += 1
            elif k == "float":
                out.sp.append("float &%s" % name)
                out.setup.append("cell_t* c%d = nullptr; if (ctx->LocalToPhysAddr(params[%d], &c%d) != SP_ERROR_NONE) return 0; %s %s{};" % (n, i, n, base, var))
                out.args.append("&" + var)
                out.after.append("*c%d = sp_ftoc(static_cast<float>(%s));" % (n, var))
                out.index += 1
            elif k in ("u64", "steamid"):
                out.sp.append("char[] %s" % name)
                out.sp.append("int %sLength" % p["paramname"])
                out.setup.append("%s %s{};" % (base, var))
                out.args.append("&" + var)
                value = "%s.ConvertToUint64()" % var if k == "steamid" else "static_cast<uint64>(%s)" % var
                out.after.append("ToBuffer(ctx, params[%d], params[%d], std::to_string(%s).c_str());" % (i, i + 1, value))
                out.index += 2
            else:
                return "parameter %s" % t
        else:
            return "parameter %s" % t
    return out


def field_form(f, types):
    """How a callback field reaches the script: (sp type, push statement), or None to leave it out."""
    t, name = f["fieldtype"].strip(), f["fieldname"]
    if f.get("private"):
        return None
    if re.fullmatch(r"char \[\d+\]", t):
        return ("const char[]", "fn->PushString(p->%s);" % name)
    k = types.kind(t)
    if k in ("int", "enum"):
        return ("int", "fn->PushCell(static_cast<cell_t>(p->%s));" % name)
    if k == "bool":
        return ("bool", "fn->PushCell(p->%s);" % name)
    if k == "float":
        return ("float", "fn->PushFloat(static_cast<float>(p->%s));" % name)
    if k == "u64":
        return ("const char[]", "{ const std::string s = std::to_string(static_cast<uint64>(p->%s)); fn->PushString(s.c_str()); }" % name)
    if k == "steamid":
        return ("const char[]", "{ const std::string s = std::to_string(p->%s.ConvertToUint64()); fn->PushString(s.c_str()); }" % name)
    return None


HEADER = """// Generated by tools/steamgen.py from s2sdk's public/steam/steam_api.json. Do not edit.
#include "plugin.h"

#include <steam/isteamnetworkingmessages.h>
#include <steam/isteamnetworkingsockets.h>
#include <steam/steamnetworkingfakeip.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#undef SendMessage
#undef GetMessage
#endif

using namespace SourcePawn;

namespace
{
    const char* Str(IPluginContext* ctx, cell_t addr)
    {
        char* s = nullptr;
        return ctx->LocalToString(addr, &s) == SP_ERROR_NONE && s ? s : "";
    }

    uint64 U64(IPluginContext* ctx, cell_t addr)
    {
        return std::strtoull(Str(ctx, addr), nullptr, 10);
    }

    cell_t ToBuffer(IPluginContext* ctx, cell_t addr, cell_t maxlen, const char* value)
    {
        size_t written = 0;
        ctx->StringToLocalUTF8(addr, maxlen, value ? value : "", &written);
        return static_cast<cell_t>(written);
    }

    struct PendingCallBase
    {
        virtual ~PendingCallBase() = default;
        PluginId owner = 0;
    };

    std::vector<std::unique_ptr<PendingCallBase>> g_pending;

    template <typename T>
    struct PendingCall final : PendingCallBase
    {
        IPluginFunction* fn = nullptr;
        cell_t data = 0;
        void (*push)(IPluginFunction*, T*) = nullptr;
        CCallResult<PendingCall<T>, T> result;

        void OnResult(T* p, bool failure)
        {
            fn->PushCell(failure);
            push(fn, p);
            fn->PushCell(data);
            fn->Execute(nullptr);
            std::erase_if(g_pending, [this](const auto& c) { return c.get() == this; });
        }
    };

    template <typename T>
    cell_t Track(IPluginContext* ctx, SteamAPICall_t call, cell_t callback, cell_t data, void (*push)(IPluginFunction*, T*))
    {
        IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(callback));
        if (!fn || call == k_uAPICallInvalid)
            return 0;
        auto pending = std::make_unique<PendingCall<T>>();
        pending->owner = g_pSource2ModNatives->GetScriptPlugin(ctx);
        pending->fn = fn;
        pending->data = data;
        pending->push = push;
        pending->result.SetGameserverFlag();
        pending->result.Set(call, pending.get(), &PendingCall<T>::OnResult);
        g_pending.push_back(std::move(pending));
        return 1;
    }

"""


def main():
    sdk = sys.argv[1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    api = json.load(open(os.path.join(sdk, "public/steam/steam_api.json")))
    types = Types(api)
    structs = {s["struct"]: s for s in api["callback_structs"]}

    cpp = [HEADER]
    rows = []
    inc = ["""/**
 * Generated by tools/steamgen.py from s2sdk's public/steam/steam_api.json. Do not edit.
 *
 * The Steamworks API as the game server has it, one methodmap per interface,
 * served by the SteamWorks_s2m plugin. 64-bit values and SteamIDs are decimal text.
 * A text or 64-bit return value is a buffer and its size after the parameters. A
 * call result is a callback and a data cell after the parameters: the native
 * answers whether the call went out, the callback gets failure, then the result's fields.
 * Everything answers 0, false or empty while the Steam API is down.
 */

#if defined _SteamAPI_Included
 #endinput
#endif
#define _SteamAPI_Included
"""]
    skipped = []
    pushers = {}
    total = 0

    def pusher(name):
        if name in pushers:
            return pushers[name]
        s = structs.get(name)
        if not s:
            return None
        forms = [(f, field_form(f, types)) for f in s["fields"]]
        forms = [(f, x) for f, x in forms if x]
        body = "\n".join("            " + x[1] for f, x in forms)
        cpp.append("    void Push_%s(IPluginFunction* fn, %s* p)\n    {\n%s\n    }\n\n" % (name, name, body))
        sp = "".join("%s %s, " % (x[0], spname(f["fieldname"])) for f, x in forms)
        inc.append("typedef %sCallback = function void (bool failure, %sany data);\n" % (name, sp))
        pushers[name] = "Push_" + name
        return pushers[name]

    maps = []
    for iface in api["interfaces"]:
        cls = iface["classname"]
        if cls not in INTERFACES:
            continue
        mapname, accessor = INTERFACES[cls]
        lines = []
        for m in iface["methods"]:
            name = m["methodname"]
            flat = m["methodname_flat"].rsplit("_", 1)[-1] if "methodname_flat" in m else name
            spn = re.sub(r"^SteamAPI_%s_" % cls, "", m.get("methodname_flat", name))
            if name in EXCLUDED or "DEPRECATED" in name or name.startswith("_"):
                skipped.append("%s::%s: excluded" % (cls, name))
                continue
            b = build(m, types)
            if isinstance(b, str):
                skipped.append("%s::%s: %s" % (cls, name, b))
                continue
            ret = m["returntype"].strip()
            rk = types.kind(ret)
            call = "steam->%s(%s)" % (name, ", ".join(b.args))
            after = "\n".join("        " + a for a in b.after)
            i = b.index
            if ret == "SteamAPICall_t":
                cr = m.get("callresult")
                push = pusher(cr) if cr else None
                if not push:
                    skipped.append("%s::%s: no call result struct" % (cls, name))
                    continue
                b.sp += ["%sCallback callback" % cr, "any data = 0"]
                sptype = "bool"
                stmt = "const SteamAPICall_t call = %s;\n%s\n        return Track<%s>(ctx, call, params[%d], params[%d], %s);" % (call, after, cr, i, i + 1, push)
            elif ret == "void":
                sptype = "void"
                stmt = "%s;\n%s\n        return 0;" % (call, after)
            elif rk in ("int", "enum"):
                sptype = "int"
                stmt = "const cell_t result = static_cast<cell_t>(%s);\n%s\n        return result;" % (call, after)
            elif rk == "bool":
                sptype = "bool"
                stmt = "const cell_t result = %s ? 1 : 0;\n%s\n        return result;" % (call, after)
            elif rk == "float":
                sptype = "float"
                stmt = "const cell_t result = sp_ftoc(static_cast<float>(%s));\n%s\n        return result;" % (call, after)
            elif rk == "u64" or rk == "steamid":
                b.sp += ["char[] result", "int resultLength"]
                sptype = "int"
                conv = "%s.ConvertToUint64()" % call if rk == "steamid" else "static_cast<uint64>(%s)" % call
                stmt = "const uint64 value = %s;\n%s\n        return ToBuffer(ctx, params[%d], params[%d], std::to_string(value).c_str());" % (conv, after, i, i + 1)
            elif ret == "const char *":
                b.sp += ["char[] result", "int resultLength"]
                sptype = "int"
                stmt = "const char* value = %s;\n%s\n        return ToBuffer(ctx, params[%d], params[%d], value);" % (call, after, i, i + 1)
            else:
                skipped.append("%s::%s: returns %s" % (cls, name, ret))
                continue
            fname = "N_%s_%s" % (mapname, spn)
            setup = "".join("        %s\n" % s for s in b.setup)
            cpp.append("    cell_t %s(IPluginContext* ctx, const cell_t* params)\n    {\n        auto* steam = %s();\n        if (!steam)\n            return 0;\n%s        %s\n    }\n\n" % (fname, accessor, setup, stmt))
            rows.append('        { "%s.%s", %s },' % (mapname, spn, fname))
            lines.append("\tpublic static native %s %s(%s);" % (sptype, spn, ", ".join(b.sp)))
            total += 1
        maps.append("methodmap %s\n{\n%s\n}\n" % (mapname, "\n".join(lines)))

    inc.append("\n" + "\n".join(maps))
    cpp.append("    const sp_nativeinfo_t g_natives[] = {\n%s\n        { nullptr, nullptr },\n    };\n}\n\n" % "\n".join(rows))
    cpp.append("""void RegisterGeneratedNatives()
{
    g_pSource2ModNatives->AddNatives(g_natives);
}

void ForgetGeneratedCalls(PluginId id)
{
    std::erase_if(g_pending, [id](const auto& c) { return c->owner == id; });
}

void ClearGeneratedCalls()
{
    g_pending.clear();
}
""")
    os.makedirs(os.path.join(root, "src/gen"), exist_ok=True)
    open(os.path.join(root, "src/gen/steamnatives.gen.cpp"), "w", newline="\n").write("".join(cpp))
    open(os.path.join(root, "public/scripting/SteamAPI.inc"), "w", newline="\n").write("".join(inc))
    open(os.path.join(root, "tools/steamgen_skipped.txt"), "w", newline="\n").write("\n".join(skipped) + "\n")
    print("%d natives, %d skipped" % (total, len(skipped)))


main()
