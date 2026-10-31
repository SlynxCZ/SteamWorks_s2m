#!/usr/bin/env python3
"""Generates the SourcePawn side of the Steamworks API from s2sdk's steam_api.json.

    python3 tools/steamgen.py <s2sdk>

    src/gen/steamnatives.gen.cpp    the natives, called through the game server's accessors
    public/scripting/steamworks/steam*.inc   a methodmap per interface (ISteamUGC -> SteamUGC), static natives
    public/scripting/steamworks/callresults.inc   what the call results hand their callbacks
    tools/steamgen_skipped.txt      what has no safe script form, and why

Script forms: 64-bit values and SteamIDs are decimal text; a text or 64-bit return
value is a buffer and its size after the parameters; a call result is a callback
and a data cell after the parameters, the native answering whether the call went out.
"""
import json
import os
import re
import sys

# Every interface the server can reach: a gameserver or a global accessor. A client-only
# one (ISteamFriends, ISteamApps, ...) has neither and answers nothing on a dedicated server.
# ISteamClient hands out raw interfaces: the methodmaps are what it would give.
SKIPPED_INTERFACES = {"ISteamClient"}


def server_accessor(iface):
    accessors = iface.get("accessors", [])
    for kind in ("gameserver", "global"):
        for a in accessors:
            if a["kind"] == kind:
                return a["name"]
    return None


# The server's own session and identity are the engine's: a script calling these breaks the server.
EXCLUDED = {
    "LogOn", "LogOnAnonymous", "LogOff", "SetDedicatedServer", "SetProduct", "SetModDir", "SetSpectatorPort",
    "BeginAuthSession", "EndAuthSession", "CancelAuthTicket", "GetAuthSessionTicket", "RunCallbacks", "RunFrame",
    "SendUserConnectAndAuthenticate_DEPRECATED", "CreateUnauthenticatedUserConnection", "SendUserDisconnect_DEPRECATED",
    "HandleIncomingPacket", "GetNextOutgoingPacket", "BUpdateUserData", "GetHostedDedicatedServerAddress",
}

RESERVED = {"in", "out", "forward", "function", "public", "native", "any", "this", "char", "int", "float", "bool", "new", "delete", "view_as"}


class Types:
    def __init__(self, api):
        self.typedefs = {t["typedef"]: t["type"] for t in api["typedefs"]}
        self.enums = {e["enumname"] for e in api["enums"]}
        self.structs = {x["struct"]: x for x in api["structs"]}
        self.structs.update({x["struct"]: x for x in api["callback_structs"]})
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
        self.counts = []    # const arrays: (param, its size's param, element, variable)
        self.struct = None  # an output struct, handed to a callback
        self.structvar = None


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
        elif t in ("const SteamNetworkingIdentity &", "const SteamNetworkingIdentity *", "const SteamNetworkingIPAddr &", "const SteamNetworkingIPAddr *"):
            base = t.split()[1]
            out.sp.append("const char[] %s" % name)
            out.setup.append("%s %s; %s.Clear(); %s.ParseString(Str(ctx, params[%d]));" % (base, var, var, var, i))
            out.args.append(var if t.endswith("&") else "&" + var)
            out.index += 1
        elif t in ("SteamNetworkingIdentity *", "SteamNetworkingIPAddr *"):
            base = t.split()[0]
            out.sp += ["char[] %s" % name, "int %sLength" % p["paramname"]]
            out.setup.append("%s %s; %s.Clear();" % (base, var, var))
            out.args.append("&" + var)
            tail = ", true" if base == "SteamNetworkingIPAddr" else ""
            out.after.append("{ char text[128] = \"\"; %s.ToString(text, sizeof(text)%s); ToBuffer(ctx, params[%d], params[%d], text); }" % (var, tail, i, i + 1))
            out.index += 2
        elif t == "SteamIPAddress_t":
            out.sp.append("int %s" % name)
            out.setup.append("SteamIPAddress_t %s{}; %s.m_eType = k_ESteamIPTypeIPv4; %s.m_unIPv4 = static_cast<uint32>(params[%d]);" % (var, var, var, i))
            out.args.append(var)
            out.index += 1
        elif t == "SteamIPAddress_t *":
            out.sp.append("int &%s" % name)
            out.setup.append("cell_t* c%d = nullptr; if (ctx->LocalToPhysAddr(params[%d], &c%d) != SP_ERROR_NONE) return 0; SteamIPAddress_t %s{};" % (n, i, n, var))
            out.args.append("&" + var)
            out.after.append("*c%d = static_cast<cell_t>(%s.m_unIPv4);" % (n, var))
            out.index += 1
        elif t.startswith("const ") and t.endswith("*") and types.kind(t[6:-1].strip()) in ("int", "enum") and p.get("array_count"):
            base = t[6:-1].strip()
            count = [x["paramname"] for x in params].index(p["array_count"])
            if types.kind(params[count]["paramtype"]) != "int":
                return "array with a size that is not a number"
            out.sp.append("const int[] %s" % name)
            out.setup.append("cell_t* c%d = nullptr; if (ctx->LocalToPhysAddr(params[%d], &c%d) != SP_ERROR_NONE) return 0; std::vector<%s> %s; __COUNT%d__" % (n, i, n, base, var, n))
            out.counts.append((n, count, base, var))
            out.args.append("%s.data()" % var)
            out.index += 1
        elif t == "const char *":
            if attrs:
                return "const char* with %s" % sorted(attrs)
            out.sp.append("const char[] %s" % name)
            out.args.append("Str(ctx, params[%d])" % i)
            out.index += 1
        elif t == "char *":
            nxt = params[n + 1] if n + 1 < len(params) else None
            if "out_string_count" not in p and not (nxt and types.kind(nxt["paramtype"]) == "int"):
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
            elif base in types.structs and not attrs:
                if out.struct:
                    return "two output structs"
                out.struct = base
                out.setup.append("%s %s{};" % (base, var))
                out.args.append("&" + var)
                out.structvar = var
            else:
                return "parameter %s" % t
        else:
            return "parameter %s" % t
    # An array's size is another parameter: its slot is known once they are all placed.
    slots = {}
    slot = 1
    for n, p in enumerate(params):
        slots[n] = slot
        t = p["paramtype"].strip()
        k = types.kind(t[:-1].strip()) if t.endswith("*") and not t.startswith("const") else None
        if t in ("SteamNetworkingIdentity *", "SteamNetworkingIPAddr *") or k in ("u64", "steamid"):
            slot += 2
        elif t.endswith("*") and not t.startswith("const") and t[:-1].strip() in types.structs and k is None and t not in ("char *", "void *", "SteamIPAddress_t *"):
            pass
        else:
            slot += 1
    for n, count, base, var in out.counts:
        fill = "%s.resize(static_cast<size_t>(params[%d])); for (size_t i = 0; i < %s.size(); i++) %s[i] = static_cast<%s>(c%d[i]);" % (var, slots[count], var, var, base, n)
        out.setup = [x.replace("__COUNT%d__" % n, fill) for x in out.setup]
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
#include <steam/isteamnetworkingutils.h>
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


def header_comments(sdk, cls):
    """The // lines right above each method in the interface's header, joined."""
    out = {}
    path = os.path.join(sdk, "public/steam", cls.lower() + ".h")
    if not os.path.exists(path):
        return out
    pending = []
    for line in open(path, encoding="utf-8", errors="ignore"):
        s = line.strip()
        if s.startswith("//"):
            text = s.lstrip("/ ").strip()
            if text and not set(text) <= set("-=/"):
                pending.append(text.replace("*/", "* /"))
            continue
        m = re.match(r"virtual\s+.*?\b(\w+)\s*\(", s)
        if m and pending:
            out.setdefault(m.group(1), " ".join(pending))
        if not s.startswith("STEAM_"):
            pending = []
    return out


def main():
    sdk = sys.argv[1]
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    api = json.load(open(os.path.join(sdk, "public/steam/steam_api.json")))
    types = Types(api)
    structs = types.structs

    cpp = [HEADER]
    rows = []
    inc = ["""/**
 * Generated by tools/steamgen.py. What the call results of the Steamworks API hand
 * their callbacks: failure, then the result's fields.
 */

#if defined _steamworks_callresults_included
 #endinput
#endif
#define _steamworks_callresults_included

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

    files = {}
    for iface in api["interfaces"]:
        cls = iface["classname"]
        accessor = server_accessor(iface)
        if not accessor or cls in SKIPPED_INTERFACES:
            skipped.append("%s: no accessor on a game server" % cls)
            continue
        mapname = cls[1:]
        lines = []
        comments = header_comments(sdk, cls)
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
            i = b.index
            if b.struct:
                push = pusher(b.struct)
                b.sp += ["%sCallback callback" % b.struct, "any data = 0"]
                b.after.append("if (IPluginFunction* fn = ctx->GetFunctionById(static_cast<funcid_t>(params[%d]))) { fn->PushCell(false); %s(fn, &%s); fn->PushCell(params[%d]); fn->Execute(nullptr); }" % (i, push, b.structvar, i + 1))
                i += 2
            after = "\n".join("        " + a for a in b.after)
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
            elif ret == "SteamIPAddress_t":
                sptype = "int"
                stmt = "const cell_t result = static_cast<cell_t>(%s.m_unIPv4);\n%s\n        return result;" % (call, after)
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
            doc = comments.get(name)
            if doc:
                lines.append("\t/** %s */" % doc)
            lines.append("\tpublic static native %s %s(%s);\n" % (sptype, spn, ", ".join(b.sp)))
            total += 1
        files[mapname.lower()] = (mapname, cls, "\n".join(lines).rstrip())

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
    scripting = os.path.join(root, "public/scripting/steamworks")
    os.makedirs(scripting, exist_ok=True)
    open(os.path.join(scripting, "callresults.inc"), "w", newline="\n").write("".join(inc))
    for fname, (mapname, cls, body) in files.items():
        open(os.path.join(scripting, fname + ".inc"), "w", newline="\n").write("""/**
 * Generated by tools/steamgen.py from s2sdk's public/steam/steam_api.json. Do not edit.
 *
 * %s as the game server has it. 64-bit values and SteamIDs are decimal text. A text
 * or 64-bit return value is a buffer and its size after the parameters. A call result
 * is a callback and a data cell after the parameters: the native answers whether the
 * call went out. Everything answers 0, false or empty while the Steam API is down.
 */

#if defined _steamworks_%s_included
 #endinput
#endif
#define _steamworks_%s_included

#include <steamworks/callresults>

methodmap %s
{
%s
}
""" % (cls, fname, fname, mapname, body))
    open(os.path.join(scripting, "all.inc"), "w", newline="\n").write("// Generated by tools/steamgen.py: every interface's include.\n" + "".join("#include <steamworks/%s>\n" % f for f in sorted(files)))
    print("includes: " + ", ".join(sorted(files)))
    open(os.path.join(root, "tools/steamgen_skipped.txt"), "w", newline="\n").write("\n".join(skipped) + "\n")
    print("%d natives, %d skipped" % (total, len(skipped)))


main()
