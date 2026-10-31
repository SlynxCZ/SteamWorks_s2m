/*
 * SteamWorksHTTPRequest on ISteamHTTP, after SM-SteamWorks. The script owns
 * the handle: closing it or the script going releases the request, and its
 * pending call results die with it.
 */
#include "plugin.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace SourcePawn;

namespace
{
    struct Request
    {
        HTTPRequestHandle http = INVALID_HTTPREQUEST_HANDLE;
        cell_t handle = 0;
        cell_t data1 = 0;
        cell_t data2 = 0;
        IPluginFunction* onCompleted = nullptr;
        IPluginFunction* onHeaders = nullptr;
        IPluginFunction* onData = nullptr;
        CCallResult<Request, HTTPRequestCompleted_t> completed;
        CCallResult<Request, HTTPRequestHeadersReceived_t> headers;
        CCallResult<Request, HTTPRequestDataReceived_t> dataReceived;
        CCallback<Request, HTTPRequestHeadersReceived_t, true> streamHeaders;
        CCallback<Request, HTTPRequestDataReceived_t, true> streamData;

        Request() : streamHeaders(this, &Request::OnStreamHeaders), streamData(this, &Request::OnStreamData)
        {
            completed.SetGameserverFlag();
            headers.SetGameserverFlag();
            dataReceived.SetGameserverFlag();
        }

        ~Request()
        {
            completed.Cancel();
            headers.Cancel();
            dataReceived.Cancel();
            if (ISteamHTTP* steam = SteamGameServerHTTP(); steam && http != INVALID_HTTPREQUEST_HANDLE)
                steam->ReleaseHTTPRequest(http);
        }

        void PushContext(IPluginFunction* fn) const
        {
            fn->PushCell(data1);
            fn->PushCell(data2);
        }

        void OnCompleted(HTTPRequestCompleted_t* result, bool failure)
        {
            if (!onCompleted)
                return;
            onCompleted->PushCell(handle);
            onCompleted->PushCell(failure);
            onCompleted->PushCell(result->m_bRequestSuccessful);
            onCompleted->PushCell(result->m_eStatusCode);
            PushContext(onCompleted);
            onCompleted->Execute(nullptr);
        }

        void OnStreamHeaders(HTTPRequestHeadersReceived_t* result)
        {
            if (result->m_hRequest != http || !onHeaders)
                return;
            onHeaders->PushCell(handle);
            onHeaders->PushCell(false);
            PushContext(onHeaders);
            onHeaders->Execute(nullptr);
        }

        void OnStreamData(HTTPRequestDataReceived_t* result)
        {
            if (result->m_hRequest != http || !onData)
                return;
            onData->PushCell(handle);
            onData->PushCell(false);
            onData->PushCell(static_cast<cell_t>(result->m_cOffset));
            onData->PushCell(static_cast<cell_t>(result->m_cBytesReceived));
            PushContext(onData);
            onData->Execute(nullptr);
        }
    };

    Source2ModHandleType RequestType()
    {
        static const Source2ModHandleType type = g_pSource2ModNatives->CreateHandleType("SteamWorksHTTPRequest", [](void* p) { delete static_cast<Request*>(p); });
        return type;
    }

    Request* Read(IPluginContext* ctx, cell_t handle)
    {
        return static_cast<Request*>(g_pSource2ModNatives->ReadHandle(ctx, handle, RequestType()));
    }

    const char* Str(IPluginContext* ctx, cell_t addr)
    {
        char* s = nullptr;
        return ctx->LocalToString(addr, &s) == SP_ERROR_NONE && s ? s : "";
    }

    std::string FullPath(const char* path)
    {
        return std::string(g_pSource2ModPaths->GameDirectory()) + "/" + path;
    }

    IPluginFunction* Fn(IPluginContext* ctx, cell_t id)
    {
        return id && id != -1 ? ctx->GetFunctionById(static_cast<funcid_t>(id)) : nullptr;
    }

    // Everything but the constructor: (request, ...).
#define S2M_REQUEST()                              \
    Request* r = Read(ctx, params[1]);             \
    ISteamHTTP* steam = SteamGameServerHTTP();     \
    if (!r || !steam)                              \
        return 0;

    // Request(method, url)
    cell_t N_Create(IPluginContext* ctx, const cell_t* params)
    {
        ISteamHTTP* steam = SteamGameServerHTTP();
        if (!steam)
            return 0;
        HTTPRequestHandle http = steam->CreateHTTPRequest(static_cast<EHTTPMethod>(params[1]), Str(ctx, params[2]));
        if (http == INVALID_HTTPREQUEST_HANDLE)
            return 0;
        auto* r = new Request();
        r->http = http;
        r->handle = g_pSource2ModNatives->CreateHandle(RequestType(), r, ctx);
        if (!r->handle)
        {
            delete r;
            return 0;
        }
        return r->handle;
    }

    cell_t N_SetContextValue(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        r->data1 = params[2];
        r->data2 = params[0] >= 3 ? params[3] : 0;
        return steam->SetHTTPRequestContextValue(r->http, static_cast<uint64>(static_cast<uint32>(params[2])));
    }

    cell_t N_SetNetworkActivityTimeout(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->SetHTTPRequestNetworkActivityTimeout(r->http, static_cast<uint32>(params[2]));
    }

    cell_t N_SetHeaderValue(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->SetHTTPRequestHeaderValue(r->http, Str(ctx, params[2]), Str(ctx, params[3]));
    }

    cell_t N_SetGetOrPostParameter(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->SetHTTPRequestGetOrPostParameter(r->http, Str(ctx, params[2]), Str(ctx, params[3]));
    }

    cell_t N_SetUserAgentInfo(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->SetHTTPRequestUserAgentInfo(r->http, Str(ctx, params[2]));
    }

    cell_t N_SetRequiresVerifiedCertificate(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->SetHTTPRequestRequiresVerifiedCertificate(r->http, params[2] != 0);
    }

    cell_t N_SetAbsoluteTimeoutMS(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->SetHTTPRequestAbsoluteTimeoutMS(r->http, static_cast<uint32>(params[2]));
    }

    // SetCallbacks(request, completed, headers, data, calling): the plugin is always the caller.
    cell_t N_SetCallbacks(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        r->onCompleted = Fn(ctx, params[2]);
        r->onHeaders = params[0] >= 3 ? Fn(ctx, params[3]) : nullptr;
        r->onData = params[0] >= 4 ? Fn(ctx, params[4]) : nullptr;
        return 1;
    }

    cell_t Send(Request* r, ISteamHTTP* steam, bool stream)
    {
        SteamAPICall_t call = k_uAPICallInvalid;
        const bool ok = stream ? steam->SendHTTPRequestAndStreamResponse(r->http, &call) : steam->SendHTTPRequest(r->http, &call);
        if (!ok || call == k_uAPICallInvalid)
            return 0;
        r->completed.Set(call, r, &Request::OnCompleted);
        return 1;
    }

    cell_t N_Send(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return Send(r, steam, false);
    }

    cell_t N_SendAndStream(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return Send(r, steam, true);
    }

    cell_t N_Defer(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->DeferHTTPRequest(r->http);
    }

    cell_t N_Prioritize(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->PrioritizeHTTPRequest(r->http);
    }

    cell_t N_GetResponseHeaderSize(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        cell_t* out = nullptr;
        uint32 size = 0;
        if (ctx->LocalToPhysAddr(params[3], &out) != SP_ERROR_NONE || !steam->GetHTTPResponseHeaderSize(r->http, Str(ctx, params[2]), &size))
            return 0;
        *out = static_cast<cell_t>(size);
        return 1;
    }

    cell_t N_GetResponseHeaderValue(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        char* out = nullptr;
        if (ctx->LocalToString(params[3], &out) != SP_ERROR_NONE || !out)
            return 0;
        return steam->GetHTTPResponseHeaderValue(r->http, Str(ctx, params[2]), reinterpret_cast<uint8*>(out), static_cast<uint32>(params[4]));
    }

    cell_t N_GetResponseBodySize(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        cell_t* out = nullptr;
        uint32 size = 0;
        if (ctx->LocalToPhysAddr(params[2], &out) != SP_ERROR_NONE || !steam->GetHTTPResponseBodySize(r->http, &size))
            return 0;
        *out = static_cast<cell_t>(size);
        return 1;
    }

    cell_t N_GetResponseBodyData(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        char* out = nullptr;
        if (ctx->LocalToString(params[2], &out) != SP_ERROR_NONE || !out)
            return 0;
        return steam->GetHTTPResponseBodyData(r->http, reinterpret_cast<uint8*>(out), static_cast<uint32>(params[3]));
    }

    cell_t N_GetStreamingResponseBodyData(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        char* out = nullptr;
        if (ctx->LocalToString(params[3], &out) != SP_ERROR_NONE || !out)
            return 0;
        return steam->GetHTTPStreamingResponseBodyData(r->http, static_cast<uint32>(params[2]), reinterpret_cast<uint8*>(out), static_cast<uint32>(params[4]));
    }

    cell_t N_GetDownloadProgressPct(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        cell_t* out = nullptr;
        float percent = 0.0f;
        if (ctx->LocalToPhysAddr(params[2], &out) != SP_ERROR_NONE || !steam->GetHTTPDownloadProgressPct(r->http, &percent))
            return 0;
        *out = sp_ftoc(percent);
        return 1;
    }

    cell_t N_GetWasTimedOut(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        cell_t* out = nullptr;
        bool timedOut = false;
        if (ctx->LocalToPhysAddr(params[2], &out) != SP_ERROR_NONE || !steam->GetHTTPRequestWasTimedOut(r->http, &timedOut))
            return 0;
        *out = timedOut;
        return 1;
    }

    cell_t N_SetRawPostBody(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        return steam->SetHTTPRequestRawPostBody(r->http, Str(ctx, params[2]), reinterpret_cast<uint8*>(const_cast<char*>(Str(ctx, params[3]))), static_cast<uint32>(params[4]));
    }

    cell_t N_SetRawPostBodyFromFile(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        FILE* file = fopen(FullPath(Str(ctx, params[3])).c_str(), "rb");
        if (!file)
            return 0;
        std::vector<uint8> body;
        uint8 chunk[4096];
        for (size_t n; (n = fread(chunk, 1, sizeof(chunk), file)) > 0;)
            body.insert(body.end(), chunk, chunk + n);
        fclose(file);
        return steam->SetHTTPRequestRawPostBody(r->http, Str(ctx, params[2]), body.data(), static_cast<uint32>(body.size()));
    }

    // GetResponseBodyCallback(request, callback, value, plugin): sData is a copy of the whole body.
    cell_t N_GetResponseBodyCallback(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        IPluginFunction* fn = Fn(ctx, params[2]);
        uint32 size = 0;
        if (!fn || !steam->GetHTTPResponseBodySize(r->http, &size))
            return 0;
        std::string body(size, '\0');
        if (!steam->GetHTTPResponseBodyData(r->http, reinterpret_cast<uint8*>(body.data()), size))
            return 0;
        fn->PushStringEx(body.data(), body.size() + 1, SM_PARAM_STRING_COPY | SM_PARAM_STRING_BINARY, 0);
        fn->PushCell(params[0] >= 3 ? params[3] : 0);
        fn->PushCell(static_cast<cell_t>(size));
        fn->Execute(nullptr);
        return 1;
    }

    // WriteResponseBodyToFile(request, path): relative to the game directory.
    cell_t N_WriteResponseBodyToFile(IPluginContext* ctx, const cell_t* params)
    {
        S2M_REQUEST();
        uint32 size = 0;
        if (!steam->GetHTTPResponseBodySize(r->http, &size))
            return 0;
        std::vector<uint8> body(size);
        if (size && !steam->GetHTTPResponseBodyData(r->http, body.data(), size))
            return 0;
        FILE* file = fopen(FullPath(Str(ctx, params[2])).c_str(), "wb");
        if (!file)
            return 0;
        const bool ok = fwrite(body.data(), 1, size, file) == size;
        fclose(file);
        return ok;
    }

#define S2M_HTTP(flat, method, fn) \
    { "SteamWorks_" flat, fn }, { "SteamWorksHTTPRequest." method, fn }

    const sp_nativeinfo_t g_natives[] = {
        { "SteamWorks_CreateHTTPRequest", N_Create },
        { "SteamWorksHTTPRequest.SteamWorksHTTPRequest", N_Create },
        S2M_HTTP("SetHTTPRequestContextValue", "SetContextValue", N_SetContextValue),
        S2M_HTTP("SetHTTPRequestNetworkActivityTimeout", "SetNetworkActivityTimeout", N_SetNetworkActivityTimeout),
        S2M_HTTP("SetHTTPRequestHeaderValue", "SetHeaderValue", N_SetHeaderValue),
        S2M_HTTP("SetHTTPRequestGetOrPostParameter", "SetGetOrPostParameter", N_SetGetOrPostParameter),
        S2M_HTTP("SetHTTPRequestUserAgentInfo", "SetUserAgentInfo", N_SetUserAgentInfo),
        S2M_HTTP("SetHTTPRequestRequiresVerifiedCertificate", "SetRequiresVerifiedCertificate", N_SetRequiresVerifiedCertificate),
        S2M_HTTP("SetHTTPRequestAbsoluteTimeoutMS", "SetAbsoluteTimeoutMS", N_SetAbsoluteTimeoutMS),
        S2M_HTTP("SetHTTPCallbacks", "SetCallbacks", N_SetCallbacks),
        S2M_HTTP("SendHTTPRequest", "Send", N_Send),
        S2M_HTTP("SendHTTPRequestAndStreamResponse", "SendAndStreamResponse", N_SendAndStream),
        S2M_HTTP("DeferHTTPRequest", "Defer", N_Defer),
        S2M_HTTP("PrioritizeHTTPRequest", "Prioritize", N_Prioritize),
        S2M_HTTP("GetHTTPResponseHeaderSize", "GetResponseHeaderSize", N_GetResponseHeaderSize),
        S2M_HTTP("GetHTTPResponseHeaderValue", "GetResponseHeaderValue", N_GetResponseHeaderValue),
        S2M_HTTP("GetHTTPResponseBodySize", "GetResponseBodySize", N_GetResponseBodySize),
        S2M_HTTP("GetHTTPResponseBodyData", "GetResponseBodyData", N_GetResponseBodyData),
        S2M_HTTP("GetHTTPStreamingResponseBodyData", "GetStreamingResponseBodyData", N_GetStreamingResponseBodyData),
        S2M_HTTP("GetHTTPDownloadProgressPct", "GetDownloadProgressPct", N_GetDownloadProgressPct),
        S2M_HTTP("GetHTTPRequestWasTimedOut", "GetWasTimedOut", N_GetWasTimedOut),
        S2M_HTTP("SetHTTPRequestRawPostBody", "SetRawPostBody", N_SetRawPostBody),
        S2M_HTTP("SetHTTPRequestRawPostBodyFromFile", "SetRawPostBodyFromFile", N_SetRawPostBodyFromFile),
        S2M_HTTP("GetHTTPResponseBodyCallback", "GetResponseBodyCallback", N_GetResponseBodyCallback),
        S2M_HTTP("WriteHTTPResponseBodyToFile", "WriteResponseBodyToFile", N_WriteResponseBodyToFile),
        { nullptr, nullptr },
    };
}

void RegisterHTTPNatives()
{
    g_pSource2ModNatives->AddNatives(g_natives);
}
