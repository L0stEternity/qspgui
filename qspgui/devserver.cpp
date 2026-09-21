// Copyright (C) 2001-2025 Val Argunov (byte AT qsp DOT org)
/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "devserver.h"
#include "callbacks_gui.h"
#include "frame.h"
#include "comtools.h"

#include <wx/base64.h>
#include <algorithm>
#include <set>
#include <string.h>
#include <limits.h>
#include <wx/file.h>
#include <wx/filename.h>

namespace QSPDev
{
    int g_engineDepth = 0;
}

/* Identifiers for the socket events of the listener and of the clients */
enum
{
    ID_DEV_SERVER = wxID_HIGHEST + 900,
    ID_DEV_CLIENT,
    ID_DEV_MONITOR
};

#define QSP_DEV_PROTOCOL 1
#define QSP_DEV_EXPRBUFSIZE (64 * 1024)
/* A loop can execute a hundred thousand lines between two idle turns. Trace
   events are batched, and the batch is capped so a runaway loop costs a
   counter rather than gigabytes of queued JSON. */
#define QSP_DEV_TRACELIMIT 2000
#define QSP_DEV_MAXVALUES 32
/* A profile keeps one record per distinct line, not per execution, so the cost
   is the size of the code that ran. The cap is per location and guards against
   generated code - a DYNAMIC building lines in a loop reports a fresh line
   number every time. */
#define QSP_DEV_PROFMAXLINES 4000
/* The inferred call stack, capped: a wrong guess costs a frame, not a heap */
#define QSP_DEV_PROFMAXDEPTH 256
#define QSP_DEV_MONITORINTERVAL 500
#define QSP_DEV_MONITORMININTERVAL 50
/* A single request has to fit in one line; "reload" sends a base64 world */
#define QSP_DEV_MAXREQUEST (96u * 1024u * 1024u)
/* A client that stops reading while a trace is streaming must not be allowed
   to grow the player's heap until it dies - it is dropped instead. */
#define QSP_DEV_MAXOUTBOX (32u * 1024u * 1024u)

/* The engine's debug callback is a plain C function pointer with nowhere to
   put a context argument, so the server that asked for tracing is reachable
   only through a file-static. Only one player runs per process. */
static QSPDevServer *g_devTraceServer = 0;

static void QSPDevDebugCallback(QSPString line)
{
    if (g_devTraceServer) g_devTraceServer->OnDebugLine(qspToWxString(line));
}

/* ------------------------------------------------------------------ */
/* Small QSP helpers                                                   */
/* ------------------------------------------------------------------ */

/* Shared with the JS bridge, which hands the engine strings for the same
   reasons; see QSPMutableString in callbacks_gui.h. */
typedef QSPMutableString QSPDevString;

/* ------------------------------------------------------------------ */
/* Server                                                              */
/* ------------------------------------------------------------------ */

QSPDevServer::QSPDevServer(QSPFrame *frame)
    : m_frame(frame),
      m_server(0),
      m_port(0),
      m_inCommand(false),
      m_idleHooked(false),
      m_refreshPending(false),
      m_varNamesValid(false),
      m_tracing(false),
      m_traceVars(true),
      m_traceLines(true),
      m_traceLimit(QSP_DEV_TRACELIMIT),
      m_traceDropped(0),
      m_refreshIsNewDesc(false),
      m_profiling(false),
      m_profLines(true),
      m_profMaxLines(QSP_DEV_PROFMAXLINES),
      m_profStartMs(0.0),
      m_profStopMs(0.0),
      m_profSamples(0),
      m_profDropped(0),
      m_profSelfMs(0.0),
      m_profWaitMs(0.0),
      m_profOpen(false),
      m_profSampleStart(0.0),
      m_profSampleLoc(0),
      m_profSampleLineRec(0),
      m_monitorOn(false),
      m_monitorIntervalMs(QSP_DEV_MONITORINTERVAL),
      m_monitorLastMs(0.0),
      m_monitorLastSamples(0),
      m_monitorLastSelfMs(0.0),
      m_monitorLastWaitMs(0.0),
      m_paused(false),
      m_breakRequested(false),
      m_stepMode(Step_None)
{
    m_monitorTimer.SetOwner(this, ID_DEV_MONITOR);
    Bind(wxEVT_TIMER, &QSPDevServer::OnMonitorTimer, this, ID_DEV_MONITOR);
    QSPDev::ResetProfCounters();
    memset(m_monitorLast, 0, sizeof(m_monitorLast));
}

QSPDevServer::~QSPDevServer()
{
    Stop();
}

bool QSPDevServer::Start(unsigned short port, const wxString &token)
{
    Stop();

    m_token = token;

    wxIPV4address address;
    address.LocalHost(); /* loopback only - never expose the API to the network */
    address.Service(port);

    m_server = new wxSocketServer(address, wxSOCKET_NOWAIT | wxSOCKET_REUSEADDR);
    if (!m_server->IsOk())
    {
        m_server->Destroy();
        m_server = 0;
        return false;
    }

    wxIPV4address boundAddress;
    m_port = (m_server->GetLocal(boundAddress) ? (unsigned short)boundAddress.Service() : port);

    m_server->SetEventHandler(*this, ID_DEV_SERVER);
    m_server->SetNotify(wxSOCKET_CONNECTION_FLAG);
    m_server->Notify(true);

    Bind(wxEVT_SOCKET, &QSPDevServer::OnServerEvent, this, ID_DEV_SERVER);
    Bind(wxEVT_SOCKET, &QSPDevServer::OnClientEvent, this, ID_DEV_CLIENT);
    wxTheApp->Bind(wxEVT_IDLE, &QSPDevServer::OnIdle, this);
    m_idleHooked = true;
    return true;
}

void QSPDevServer::Stop()
{
    /* Dropping the clients is what lets a held breakpoint go, but the loop
       only reads this once it is next round - so the intent is recorded here
       as well, in case Stop() was itself reached from inside a paused command. */
    m_paused = false;

    while (!m_clients.empty())
        DropClient(m_clients.back());

    if (m_server)
    {
        m_server->Notify(false);
        m_server->Destroy();
        m_server = 0;
    }
    if (m_idleHooked)
    {
        wxTheApp->Unbind(wxEVT_IDLE, &QSPDevServer::OnIdle, this);
        m_idleHooked = false;
    }
    SetTracing(false);
    SetProfiling(false);
    SetMonitor(false, m_monitorIntervalMs);
    m_breakpoints.clear();
    m_breakRequested = false;
    m_stepMode = Step_None;
    UpdateDebugHook();
    m_pending.clear();
    m_slots.clear();
    m_outbox.clear();
    m_watch.clear();
    m_traceLocs.clear();
    m_traceEvents.clear();
    m_varNames.clear();
    m_varNamesValid = false;
    m_port = 0;
}

void QSPDevServer::OnServerEvent(wxSocketEvent &WXUNUSED(event))
{
    wxSocketBase *client = m_server->Accept(false);
    if (!client) return;

    client->SetFlags(wxSOCKET_NOWAIT);
    client->SetEventHandler(*this, ID_DEV_CLIENT);
    client->SetNotify(wxSOCKET_INPUT_FLAG | wxSOCKET_OUTPUT_FLAG | wxSOCKET_LOST_FLAG);
    client->Notify(true);

    m_clients.push_back(client);
    m_buffers[client] = std::string();
    m_authorized[client] = m_token.IsEmpty();

    QSPJsonBuilder hello;
    hello.StartObject();
    hello.Member(wxT("jsonrpc"), wxT("2.0"));
    hello.Member(wxT("method"), wxT("welcome"));
    hello.Key(wxT("params"));
    hello.StartObject();
    hello.Member(wxT("player"), wxT("QSP Classic"));
    hello.Member(wxT("engine"), qspToWxString(QSPGetVersion()));
    hello.MemberInt(wxT("protocol"), QSP_DEV_PROTOCOL);
    hello.MemberBool(wxT("authRequired"), !m_token.IsEmpty());
    hello.EndObject();
    hello.EndObject();
    Send(client, hello.GetText());
}

void QSPDevServer::OnClientEvent(wxSocketEvent &event)
{
    wxSocketBase *socket = event.GetSocket();
    switch (event.GetSocketEvent())
    {
    case wxSOCKET_INPUT:
        ReadFrom(socket);
        break;
    case wxSOCKET_OUTPUT:
        FlushOutbox(socket);
        break;
    case wxSOCKET_LOST:
        DropClient(socket);
        break;
    default:
        break;
    }
}

void QSPDevServer::DropClient(wxSocketBase *socket)
{
    for (std::vector<PendingCommand>::iterator it = m_pending.begin(); it != m_pending.end();)
    {
        if (it->socket == socket)
            it = m_pending.erase(it);
        else
            ++it;
    }

    for (std::vector<wxSocketBase *>::iterator it = m_clients.begin(); it != m_clients.end(); ++it)
    {
        if (*it == socket)
        {
            m_clients.erase(it);
            break;
        }
    }
    m_buffers.erase(socket);
    m_authorized.erase(socket);
    m_outbox.erase(socket);

    socket->Notify(false);
    socket->Destroy();
}

void QSPDevServer::ReadFrom(wxSocketBase *socket)
{
    std::map<wxSocketBase *, std::string>::iterator buffer = m_buffers.find(socket);
    if (buffer == m_buffers.end()) return;

    char chunk[4096];
    for (;;)
    {
        socket->Read(chunk, sizeof(chunk));
        size_t count = socket->LastCount();
        if (!count) break;
        buffer->second.append(chunk, count);
    }

    /* A request has to arrive as one line, so a client that never sends a
       newline would otherwise grow this without limit. The cap is generous
       because "reload" carries a whole base64-encoded world. */
    if (buffer->second.size() > QSP_DEV_MAXREQUEST &&
        buffer->second.find('\n') == std::string::npos)
    {
        DropClient(socket);
        return;
    }

    /* Requests are newline delimited, so a partial one simply stays in the
       buffer until the rest of it arrives. */
    for (;;)
    {
        std::string::size_type breakPos = buffer->second.find('\n');
        if (breakPos == std::string::npos) break;

        std::string rawLine = buffer->second.substr(0, breakPos);
        buffer->second.erase(0, breakPos + 1);
        if (!rawLine.empty() && rawLine[rawLine.length() - 1] == '\r')
            rawLine.erase(rawLine.length() - 1);
        if (rawLine.empty()) continue;

        QueueLine(socket, wxString::FromUTF8(rawLine.c_str(), rawLine.length()));
    }
}

/* Sockets are non-blocking, so a write can be accepted only in part. The
   remainder is kept and retried from the OUTPUT event and from idle - without
   that a trace stream would lose the tail of a line and leave the client
   parsing garbage. */
void QSPDevServer::Send(wxSocketBase *socket, const wxString &line)
{
    if (!socket || !socket->IsConnected()) return;

    /* The buffer can be a view into the string rather than a copy, so the
       string has to outlive it. */
    wxString message(line);
    message += wxT("\n");
    const wxScopedCharBuffer data = message.utf8_str();
    std::string &outbox = m_outbox[socket];
    if (outbox.size() + data.length() > QSP_DEV_MAXOUTBOX)
    {
        DropClient(socket);
        return;
    }
    outbox.append(data.data(), data.length());
    FlushOutbox(socket);
}

void QSPDevServer::FlushOutbox(wxSocketBase *socket)
{
    std::map<wxSocketBase *, std::string>::iterator it = m_outbox.find(socket);
    if (it == m_outbox.end() || it->second.empty()) return;

    if (!socket->IsConnected())
    {
        it->second.clear();
        return;
    }

    socket->Write(it->second.data(), (wxUint32)it->second.size());
    size_t written = (size_t)socket->LastWriteCount();
    if (written >= it->second.size())
        it->second.clear();
    else if (written)
        it->second.erase(0, written);
}

void QSPDevServer::Broadcast(const wxString &line)
{
    /* Over a copy: a client that has stopped reading is dropped by Send,
       which takes it out of m_clients mid-loop. */
    std::vector<wxSocketBase *> clients(m_clients);
    for (size_t i = 0; i < clients.size(); ++i)
    {
        std::map<wxSocketBase *, bool>::const_iterator it = m_authorized.find(clients[i]);
        if (it != m_authorized.end() && it->second)
            Send(clients[i], line);
    }
}

void QSPDevServer::Notify(const wxString &method, const wxString &paramsJson)
{
    if (m_clients.empty()) return;

    QSPJsonBuilder message;
    message.StartObject();
    message.Member(wxT("jsonrpc"), wxT("2.0"));
    message.Member(wxT("method"), method);
    message.Key(wxT("params"));
    message.ValueRaw(paramsJson.IsEmpty() ? wxT("{}") : paramsJson);
    message.EndObject();
    Broadcast(message.GetText());
}

/* Commands are never run straight from the socket event. The engine is not
   re-entrant, and a SLEEP or an input dialog yields to the event loop while
   game code is still on the stack - executing a command there would corrupt
   the interpreter state. */
void QSPDevServer::QueueLine(wxSocketBase *socket, const wxString &line)
{
    PendingCommand command;
    command.socket = socket;
    command.line = line;
    m_pending.push_back(command);
    DrainQueue();
}

void QSPDevServer::OnIdle(wxIdleEvent &event)
{
    /* Whatever line was open is not running any more - the engine is either
       between turns or yielding from inside one. Charging it now rather than
       at the next callback is what keeps a location's last line from being
       billed for the seconds the player then spent waiting for a click. */
    if (m_profOpen) CloseProfileSample(QSPDev::IsEngineBusy());
    DrainQueue();
    FlushNotifications();
    for (size_t i = 0; i < m_clients.size(); ++i)
        FlushOutbox(m_clients[i]);
    /* Only ask for another idle turn when the queue could actually move. A
       command that arrives while a modal dialog is up stays pending for as
       long as the dialog does, and asking for more there would spin a core
       until the player closes it; the next real event brings idle back. */
    if (!m_pending.empty() && !QSPDev::IsEngineBusy())
        event.RequestMore();
    event.Skip();
}

/* Sends what could not be described at the moment it happened. Reading the
   current location means evaluating an expression, which is only safe once the
   engine has finished unwinding. */
void QSPDevServer::FlushNotifications()
{
    if (m_inCommand || QSPDev::IsEngineBusy()) return;

    FlushTrace();
    FlushWatch(m_watchReason.IsEmpty() ? wxT("refresh") : m_watchReason);

    if (!m_refreshPending) return;

    m_refreshPending = false;
    if (m_clients.empty()) return;

    QSPJsonBuilder params;
    params.StartObject();
    params.MemberBool(wxT("isNewDesc"), m_refreshIsNewDesc);
    params.Member(wxT("loc"), GetCurrentLocation());
    params.EndObject();
    m_refreshIsNewDesc = false;
    Notify(wxT("refreshed"), params.GetText());
}

void QSPDevServer::DrainQueue()
{
    if (m_inCommand || QSPDev::IsEngineBusy()) return;

    while (!m_pending.empty())
    {
        PendingCommand command = m_pending.front();
        m_pending.erase(m_pending.begin());

        m_inCommand = true;
        Dispatch(command.socket, command.line);
        m_inCommand = false;
        FlushNotifications();

        /* A command may have run game code that yielded and disconnected a
           client, so bail out rather than trusting the rest of the queue. */
        if (QSPDev::IsEngineBusy()) break;
    }
}

void QSPDevServer::Dispatch(wxSocketBase *socket, const wxString &line)
{
    QSPJsonReader request;
    QSPJsonBuilder response;

    if (!request.Parse(line))
    {
        response.StartObject();
        response.Member(wxT("jsonrpc"), wxT("2.0"));
        response.Key(wxT("id"));
        response.ValueNull();
        response.Key(wxT("error"));
        response.StartObject();
        response.MemberInt(wxT("code"), -32700);
        response.Member(wxT("message"), wxT("Parse error"));
        response.EndObject();
        response.EndObject();
        Send(socket, response.GetText());
        return;
    }

    wxString id = request.GetRaw(wxT("id"));
    wxString method = request.GetString(wxT("method"));

    QSPJsonReader params;
    wxString paramsRaw = request.GetRaw(wxT("params"));
    if (!paramsRaw.IsEmpty())
        params.Parse(paramsRaw);

    wxString errorText;
    QSPJsonBuilder result;
    bool isOk;

    if (method == wxT("hello"))
    {
        if (m_token.IsEmpty() || params.GetString(wxT("token")) == m_token)
        {
            m_authorized[socket] = true;
            result.StartObject();
            result.MemberBool(wxT("authorized"), true);
            result.EndObject();
            isOk = true;
        }
        else
        {
            errorText = wxT("Invalid token");
            isOk = false;
        }
    }
    else if (!m_authorized[socket])
    {
        errorText = wxT("Not authorized, call \"hello\" with a token first");
        isOk = false;
    }
    else if (m_frame->IsBusyLoading())
    {
        /* A game world is being rewritten right now, and the editor's command
           would read or run against half of it. It is told to come back
           rather than left waiting: the load pumps the event loop, so this
           request arrived in the middle of one. */
        errorText = wxT("Busy loading a game, try again in a moment");
        isOk = false;
    }
    else
    {
        isOk = Invoke(method, params, result, errorText);
    }

    if (id.IsEmpty()) return; /* a notification wants no answer */

    response.StartObject();
    response.Member(wxT("jsonrpc"), wxT("2.0"));
    response.Key(wxT("id"));
    response.ValueRaw(id);
    if (isOk)
    {
        response.Key(wxT("result"));
        response.ValueRaw(result.GetText().IsEmpty() ? wxT("{}") : result.GetText());
    }
    else
    {
        response.Key(wxT("error"));
        response.StartObject();
        response.MemberInt(wxT("code"), -32000);
        response.Member(wxT("message"), errorText);
        response.EndObject();
    }
    response.EndObject();
    Send(socket, response.GetText());
}

bool QSPDevServer::Invoke(const wxString &method, const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    if (method == wxT("ping"))
    {
        result.StartObject();
        result.Member(wxT("player"), wxT("QSP Classic"));
        result.Member(wxT("engine"), qspToWxString(QSPGetVersion()));
        result.MemberInt(wxT("protocol"), QSP_DEV_PROTOCOL);
        result.EndObject();
        return true;
    }
    if (method == wxT("state")) return CmdState(result, errorText);
    if (method == wxT("locations")) return CmdLocations(result, errorText);
    if (method == wxT("locationCode")) return CmdLocationCode(params, result, errorText);
    if (method == wxT("exec")) return CmdExec(params, result, errorText);
    if (method == wxT("eval")) return CmdEval(params, result, errorText);
    if (method == wxT("getVar")) return CmdGetVar(params, result, errorText);
    if (method == wxT("setVar")) return CmdSetVar(params, result, errorText);
    if (method == wxT("vars")) return CmdVars(params, result, errorText);
    if (method == wxT("varNames")) return CmdVarNames(params, result, errorText);
    if (method == wxT("setVars")) return CmdSetVars(params, result, errorText);
    if (method == wxT("watch")) return CmdWatch(params, result, errorText);
    if (method == wxT("trace")) return CmdTrace(params, result, errorText);
    if (method == wxT("profile")) return CmdProfile(params, result, errorText);
    if (method == wxT("monitor")) return CmdMonitor(params, result, errorText);
    if (method == wxT("break")) return CmdBreak(params, result, errorText);
    if (method == wxT("pause")) return CmdPause(params, result, errorText);
    if (method == wxT("resume")) return CmdResume(params, result, errorText);
    if (method == wxT("goto")) return CmdGoto(params, result, errorText);
    if (method == wxT("reload")) return CmdReload(params, result, errorText);
    if (method == wxT("snapshot")) return CmdSnapshot(params, result, errorText);
    if (method == wxT("restore")) return CmdRestore(params, result, errorText);
    if (method == wxT("restart"))
    {
        if (!QSPRestartGame(QSP_TRUE))
        {
            errorText = DescribeLastError();
            return false;
        }
        result.StartObject();
        result.MemberBool(wxT("ok"), true);
        result.EndObject();
        return true;
    }

    errorText = wxT("Unknown method: ") + method;
    return false;
}

/* ------------------------------------------------------------------ */
/* Commands                                                            */
/* ------------------------------------------------------------------ */

/* QSPGetCurStateData reports where the interpreter is *executing*, which is
   reset once control returns, so it only answers this between engine
   callbacks. The location the player is standing in is qspCurLoc, which the
   library does not export - $CURLOC is the way to it. */
wxString QSPDevServer::GetCurrentLocation() const
{
    std::vector<QSP_CHAR> buffer(1024);
    if (!QSPCalculateStrExpression(QSPDevString(wxT("$CURLOC")), &buffer[0], (int)buffer.size(), QSP_FALSE))
        return wxEmptyString;
    return wxString(&buffer[0]);
}

/* Valid while the engine is running game code, e.g. inside a refresh */
wxString QSPDevServer::GetExecutingLocation() const
{
    QSPString locName;
    int actIndex, lineNum;
    QSPGetCurStateData(&locName, &actIndex, &lineNum);
    return qspToWxString(locName);
}

wxString QSPDevServer::DescribeLastError() const
{
    QSPErrorInfo info = QSPGetLastErrorData();
    if (!info.ErrorNum) return wxT("Unknown error");

    wxString locName(qspToWxString(info.LocName));
    wxString desc(qspToWxString(info.ErrorDesc));
    if (locName.IsEmpty())
        return wxString::Format(wxT("[%d] %s (line %d)"), info.ErrorNum, desc.wx_str(), info.IntLineNum);
    return wxString::Format(wxT("[%d] %s (%s, line %d)"), info.ErrorNum, desc.wx_str(), locName.wx_str(), info.TopLineNum);
}

void QSPDevServer::AppendErrorInfo(QSPJsonBuilder &builder) const
{
    QSPErrorInfo info = QSPGetLastErrorData();
    builder.MemberInt(wxT("code"), info.ErrorNum);
    builder.Member(wxT("desc"), qspToWxString(info.ErrorDesc));
    builder.Member(wxT("loc"), qspToWxString(info.LocName));
    builder.MemberInt(wxT("actIndex"), info.ActIndex);
    builder.MemberInt(wxT("topLineNum"), info.TopLineNum);
    builder.MemberInt(wxT("intLineNum"), info.IntLineNum);
    builder.Member(wxT("line"), qspToWxString(info.IntLine));
}

bool QSPDevServer::RunCode(const wxString &code, bool toRefresh, wxString &errorText)
{
    m_watchReason = wxT("exec");
    if (!QSPExecString(QSPDevString(code), toRefresh ? QSP_TRUE : QSP_FALSE))
    {
        errorText = DescribeLastError();
        return false;
    }
    return true;
}

bool QSPDevServer::CmdState(QSPJsonBuilder &result, wxString &WXUNUSED(errorText))
{
    QSPString execLoc;
    int actIndex, lineNum;
    QSPGetCurStateData(&execLoc, &actIndex, &lineNum);

    result.StartObject();
    result.MemberBool(wxT("gameOpened"), m_frame->IsGameOpened());
    result.Member(wxT("gameFile"), m_frame->GetGameFilePath());
    result.Member(wxT("loc"), GetCurrentLocation());
    result.Member(wxT("execLoc"), qspToWxString(execLoc));
    result.MemberInt(wxT("actIndex"), actIndex);
    result.MemberInt(wxT("lineNum"), lineNum);
    result.Member(wxT("desc"), qspToWxString(QSPGetMainDesc()));
    result.Member(wxT("vars"), qspToWxString(QSPGetVarsDesc()));
    result.MemberInt(wxT("selActionIndex"), QSPGetSelActionIndex());
    result.MemberInt(wxT("selObjectIndex"), QSPGetSelObjectIndex());

    int count = QSPGetActions(0, 0);
    result.Key(wxT("actions"));
    result.StartArray();
    if (count > 0)
    {
        std::vector<QSPListItem> items(count);
        QSPGetActions(&items[0], count);
        for (int i = 0; i < count; ++i)
        {
            result.StartObject();
            result.Member(wxT("name"), qspToWxString(items[i].Name));
            result.Member(wxT("image"), qspToWxString(items[i].Image));
            result.EndObject();
        }
    }
    result.EndArray();

    count = QSPGetObjects(0, 0);
    result.Key(wxT("objects"));
    result.StartArray();
    if (count > 0)
    {
        std::vector<QSPObjectItem> items(count);
        QSPGetObjects(&items[0], count);
        for (int i = 0; i < count; ++i)
        {
            result.StartObject();
            result.Member(wxT("name"), qspToWxString(items[i].Title));
            result.Member(wxT("image"), qspToWxString(items[i].Image));
            result.EndObject();
        }
    }
    result.EndArray();
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdLocations(QSPJsonBuilder &result, wxString &WXUNUSED(errorText))
{
    int count = QSPGetLocationNames(0, 0);

    result.StartObject();
    result.MemberInt(wxT("count"), count);
    result.Key(wxT("names"));
    result.StartArray();
    if (count > 0)
    {
        std::vector<QSPString> names(count);
        QSPGetLocationNames(&names[0], count);
        for (int i = 0; i < count; ++i)
            result.ValueString(qspToWxString(names[i]));
    }
    result.EndArray();
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdLocationCode(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString name = params.GetString(wxT("name"));
    if (name.IsEmpty())
    {
        errorText = wxT("\"name\" is required");
        return false;
    }

    int linesCount = QSPGetLocationCode(QSPDevString(name), 0, 0);
    if (linesCount < 0)
    {
        errorText = wxT("Unknown location: ") + name;
        return false;
    }

    result.StartObject();
    result.Member(wxT("name"), name);
    result.Member(wxT("desc"), qspToWxString(QSPGetLocationDesc(QSPDevString(name))));

    result.Key(wxT("code"));
    result.StartArray();
    if (linesCount > 0)
    {
        std::vector<QSPLineInfo> lines(linesCount);
        QSPGetLocationCode(QSPDevString(name), &lines[0], linesCount);
        for (int i = 0; i < linesCount; ++i)
        {
            result.StartObject();
            result.MemberInt(wxT("lineNum"), lines[i].LineNum);
            result.Member(wxT("line"), qspToWxString(lines[i].Line));
            result.EndObject();
        }
    }
    result.EndArray();

    int actionsCount = QSPGetLocationActions(QSPDevString(name), 0, 0);
    result.Key(wxT("actions"));
    result.StartArray();
    if (actionsCount > 0)
    {
        std::vector<QSPListItem> actions(actionsCount);
        QSPGetLocationActions(QSPDevString(name), &actions[0], actionsCount);
        for (int i = 0; i < actionsCount; ++i)
        {
            result.StartObject();
            result.Member(wxT("name"), qspToWxString(actions[i].Name));
            result.Member(wxT("image"), qspToWxString(actions[i].Image));

            int actLines = QSPGetLocationActionCode(QSPDevString(name), i, 0, 0);
            result.Key(wxT("code"));
            result.StartArray();
            if (actLines > 0)
            {
                std::vector<QSPLineInfo> lines(actLines);
                QSPGetLocationActionCode(QSPDevString(name), i, &lines[0], actLines);
                for (int j = 0; j < actLines; ++j)
                {
                    result.StartObject();
                    result.MemberInt(wxT("lineNum"), lines[j].LineNum);
                    result.Member(wxT("line"), qspToWxString(lines[j].Line));
                    result.EndObject();
                }
            }
            result.EndArray();
            result.EndObject();
        }
    }
    result.EndArray();
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdExec(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString code = params.GetString(wxT("code"));
    if (code.IsEmpty())
    {
        errorText = wxT("\"code\" is required");
        return false;
    }

    if (!RunCode(code, params.GetBool(wxT("refresh"), true), errorText))
        return false;

    result.StartObject();
    result.MemberBool(wxT("ok"), true);
    result.Member(wxT("loc"), GetCurrentLocation());
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdEval(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString expr = params.GetString(wxT("expr"));
    if (expr.IsEmpty())
    {
        errorText = wxT("\"expr\" is required");
        return false;
    }

    bool toRefresh = params.GetBool(wxT("refresh"), false);
    if (params.GetString(wxT("type"), wxT("str")) == wxT("num"))
    {
        QSP_BIGINT value;
        if (!QSPCalculateNumExpression(QSPDevString(expr), &value, toRefresh ? QSP_TRUE : QSP_FALSE))
        {
            errorText = DescribeLastError();
            return false;
        }
        result.StartObject();
        result.Member(wxT("type"), wxT("num"));
        result.Key(wxT("value"));
        result.ValueRaw(wxLongLong((wxLongLong_t)value).ToString());
        result.EndObject();
        return true;
    }

    std::vector<QSP_CHAR> buffer(QSP_DEV_EXPRBUFSIZE);
    if (!QSPCalculateStrExpression(QSPDevString(expr), &buffer[0], QSP_DEV_EXPRBUFSIZE, toRefresh ? QSP_TRUE : QSP_FALSE))
    {
        errorText = DescribeLastError();
        return false;
    }
    result.StartObject();
    result.Member(wxT("type"), wxT("str"));
    result.Member(wxT("value"), wxString(&buffer[0]));
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdGetVar(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString name = params.GetString(wxT("name"));
    if (name.IsEmpty())
    {
        errorText = wxT("\"name\" is required");
        return false;
    }
    /* The engine upper-cases names while preprocessing code, so a lookup has
       to match that or it silently finds nothing. */
    name.MakeUpper();
    int index = (int)params.GetInt(wxT("index"), 0);

    int count = 0;
    QSPGetVarValuesCount(QSPDevString(name), &count);

    result.StartObject();
    result.Member(wxT("name"), name);
    result.MemberInt(wxT("index"), index);
    result.MemberInt(wxT("count"), count);

    QSPString strValue;
    if (QSPGetStrVarValue(QSPDevString(name), index, &strValue))
    {
        result.Member(wxT("type"), wxT("str"));
        result.Member(wxT("value"), qspToWxString(strValue));
    }
    else
    {
        QSP_BIGINT numValue = 0;
        QSPGetNumVarValue(QSPDevString(name), index, &numValue);
        result.Member(wxT("type"), wxT("num"));
        result.Key(wxT("value"));
        result.ValueRaw(wxLongLong((wxLongLong_t)numValue).ToString());
    }
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdSetVar(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString name = params.GetString(wxT("name"));
    if (name.IsEmpty())
    {
        errorText = wxT("\"name\" is required");
        return false;
    }

    /* Shared with the JS bridge: the name is validated so it cannot turn the
       generated assignment into arbitrary code, and the value is escaped as a
       QSP literal. An absent index means the first item - anything that is not
       a non-negative number becomes a string key, and "" is a key like any
       other, so leaving it out would quietly write beside the value the
       caller meant to replace. */
    wxString code;
    if (!QSPCode::BuildAssignment(name,
                                  params.GetString(wxT("index"), wxT("0")),
                                  params.GetString(wxT("value")),
                                  params.GetBool(wxT("append"), false),
                                  &code, &errorText))
        return false;

    if (!RunCode(code, params.GetBool(wxT("refresh"), true), errorText))
        return false;

    result.StartObject();
    result.MemberBool(wxT("ok"), true);
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdGoto(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString loc = params.GetString(wxT("loc"));
    if (loc.IsEmpty())
    {
        errorText = wxT("\"loc\" is required");
        return false;
    }

    wxString code = (params.GetBool(wxT("sub"), false) ? wxT("GOSUB ") : wxT("GOTO "));
    code += QSPCode::ToQspLiteral(loc);

    std::vector<wxString> args = params.GetStringArray(wxT("args"));
    for (size_t i = 0; i < args.size(); ++i)
        code += wxT(", ") + QSPCode::ToQspLiteral(args[i]);

    if (!RunCode(code, true, errorText))
        return false;

    result.StartObject();
    result.MemberBool(wxT("ok"), true);
    result.Member(wxT("loc"), GetCurrentLocation());
    result.EndObject();
    return true;
}

bool QSPDevServer::TakeSnapshot(std::vector<char> &snapshot, wxString &errorText)
{
    if (!QSPGameState::Save(snapshot, false))
    {
        errorText = DescribeLastError();
        return false;
    }
    if (snapshot.empty())
    {
        errorText = wxT("The engine produced an empty snapshot");
        return false;
    }
    return true;
}

bool QSPDevServer::CmdSnapshot(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    std::vector<char> snapshot;
    if (!TakeSnapshot(snapshot, errorText)) return false;

    wxString slot = params.GetString(wxT("slot"), wxT("default"));
    m_slots[slot] = snapshot;

    result.StartObject();
    result.Member(wxT("slot"), slot);
    result.MemberInt(wxT("size"), (long)snapshot.size());
    result.Member(wxT("loc"), GetCurrentLocation());
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdRestore(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString slot = params.GetString(wxT("slot"), wxT("default"));
    std::map<wxString, std::vector<char> >::iterator it = m_slots.find(slot);
    if (it == m_slots.end() || it->second.empty())
    {
        errorText = wxT("No snapshot in slot: ") + slot;
        return false;
    }

    if (!QSPOpenSavedGameFromData(&it->second[0], (int)it->second.size(), QSP_TRUE))
    {
        errorText = DescribeLastError();
        return false;
    }

    result.StartObject();
    result.MemberBool(wxT("ok"), true);
    result.Member(wxT("loc"), GetCurrentLocation());
    result.EndObject();
    return true;
}

/* Swaps the game world under a running session.
   The engine has no way to patch a single location - qspLocs is private to the
   library, and loading a world with isNewGame = QSP_FALSE is the INCLUDE path,
   which skips names that already exist instead of replacing them. What does
   work is a round trip through a save: variables survive a world load
   untouched, and the restore resolves the current location by name against the
   new world. */
bool QSPDevServer::CmdReload(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    InvalidateVarNames();
    std::vector<char> world;
    wxString path;

    if (params.Has(wxT("data")))
    {
        wxMemoryBuffer decoded = wxBase64Decode(params.GetString(wxT("data")));
        if (!decoded.GetDataLen())
        {
            errorText = wxT("\"data\" is not valid base64");
            return false;
        }
        world.assign((char *)decoded.GetData(), (char *)decoded.GetData() + decoded.GetDataLen());
    }
    else
    {
        path = params.GetString(wxT("path"), m_frame->GetGameFilePath());
        if (path.IsEmpty())
        {
            errorText = wxT("No game file to reload, pass \"path\" or \"data\"");
            return false;
        }
        if (!wxFileExists(path))
        {
            errorText = wxT("File not found: ") + path;
            return false;
        }
        /* An editor that has just written this file may still be holding it
           open, so the file existing says nothing about it being readable. */
        if (!QSPFileIO::Read(path, world) || world.empty())
        {
            errorText = wxT("Cannot read ") + path;
            return false;
        }
    }

    bool keepState = params.GetBool(wxT("keepState"), true) && m_frame->IsGameOpened();
    wxString reenter = params.GetString(wxT("reenter"), wxT("goto"));
    wxString curLoc = GetCurrentLocation();

    std::vector<char> snapshot;
    if (keepState)
    {
        /* qspCheckGameStatus only verifies the game CRC when DEBUG is zero,
           so setting it is what lets this snapshot load into the rebuilt
           world. */
        wxString ignored;
        RunCode(wxT("DEBUG=1"), false, ignored);
        if (!TakeSnapshot(snapshot, errorText))
            return false;
    }

    if (!QSPLoadGameWorldFromData(&world[0], (int)world.size(), QSP_TRUE))
    {
        errorText = DescribeLastError();
        return false;
    }
    if (!path.IsEmpty())
        m_frame->UpdateGameFile(path);

    bool stateRestored = false;
    if (keepState)
    {
        stateRestored = (QSPOpenSavedGameFromData(&snapshot[0], (int)snapshot.size(), QSP_TRUE) != QSP_FALSE);
        if (!stateRestored)
        {
            /* The world is already swapped, so falling back to a clean start
               is better than leaving the session half reloaded. */
            errorText = DescribeLastError();
            QSPRestartGame(QSP_TRUE);
        }
    }
    else
    {
        QSPRestartGame(QSP_TRUE);
    }

    /* The snapshot carries the rendered description and the literal code of
       the current actions, so until the location is entered again the screen
       still shows what the old world produced. */
    bool reentered = false;
    if (stateRestored && !curLoc.IsEmpty() && reenter != wxT("none"))
    {
        wxString reenterError;
        if (reenter == wxT("code"))
            reentered = (QSPExecLocationCode(QSPDevString(curLoc), QSP_TRUE) != QSP_FALSE);
        else if (reenter == wxT("gosub"))
            reentered = RunCode(wxT("GOSUB ") + QSPCode::ToQspLiteral(curLoc), true, reenterError);
        else
            reentered = RunCode(wxT("GOTO ") + QSPCode::ToQspLiteral(curLoc), true, reenterError);

        if (!reentered && errorText.IsEmpty())
            errorText = reenterError;
    }

    result.StartObject();
    result.MemberBool(wxT("ok"), true);
    result.MemberInt(wxT("locations"), QSPGetLocationNames(0, 0));
    result.Member(wxT("loc"), GetCurrentLocation());
    result.Member(wxT("previousLoc"), curLoc);
    result.MemberBool(wxT("stateRestored"), stateRestored);
    result.MemberBool(wxT("reentered"), reentered);
    if (!errorText.IsEmpty())
        result.Member(wxT("warning"), errorText);
    result.EndObject();

    errorText.Clear();
    return true;
}

/* ------------------------------------------------------------------ */
/* Notifications                                                       */
/* ------------------------------------------------------------------ */

void QSPDevServer::NotifyRefreshed(bool isNewDesc)
{
    if (m_clients.empty()) return;

    m_refreshPending = true;
    if (isNewDesc) m_refreshIsNewDesc = true;
}

void QSPDevServer::NotifyError()
{
    if (m_clients.empty()) return;

    QSPJsonBuilder params;
    params.StartObject();
    AppendErrorInfo(params);
    params.EndObject();
    Notify(wxT("error"), params.GetText());
}

void QSPDevServer::NotifyMessage(const wxString &text)
{
    if (m_clients.empty()) return;

    QSPJsonBuilder params;
    params.StartObject();
    params.Member(wxT("text"), text);
    params.EndObject();
    Notify(wxT("message"), params.GetText());
}

void QSPDevServer::NotifyGameOpened(const wxString &path)
{
    /* A different world means different code and a different variable table,
       so the scanned names and the remembered watch values are both stale. */
    InvalidateVarNames();
    CaptureWatch();

    if (m_clients.empty()) return;

    QSPJsonBuilder params;
    params.StartObject();
    params.Member(wxT("path"), path);
    params.MemberInt(wxT("locations"), QSPGetLocationNames(0, 0));
    params.EndObject();
    Notify(wxT("gameOpened"), params.GetText());
}

/* Pushed straight out rather than queued: this arrives from an ordinary event
   handler with the engine idle, and nothing here touches the engine at all. */
void QSPDevServer::NotifyScriptDiag(const wxString &kind, const wxString &text,
                                    const wxString &where, const wxString &pane)
{
    if (m_clients.empty()) return;

    QSPJsonBuilder params;
    params.StartObject();
    params.Member(wxT("kind"), kind);
    params.Member(wxT("text"), text);
    params.Member(wxT("where"), where);
    params.Member(wxT("pane"), pane);
    params.EndObject();
    Notify(wxT("scriptError"), params.GetText());
}

/* ------------------------------------------------------------------ */
/* Variables                                                           */
/* ------------------------------------------------------------------ */

/* The engine keys a variable by its name with the type prefix stripped, so
   $FOO and FOO are one variable whose values each carry their own type, and it
   upper-cases every name while preprocessing code. A lookup has to match both
   or it silently finds nothing. */
static wxString QSPDevNormalizeVarName(const wxString &name)
{
    wxString result(name);
    if (!result.IsEmpty() && (result[0] == wxT('$') || result[0] == wxT('%')))
        result = result.Mid(1);
    result.Trim(true).Trim(false);
    result.MakeUpper();
    return result;
}

static const wxChar *QSPDevTypeName(QSP_TINYINT type)
{
    switch (type)
    {
    case QSP_TYPE_TUPLE: return wxT("tuple");
    case QSP_TYPE_NUM: return wxT("num");
    case QSP_TYPE_BOOL: return wxT("bool");
    case QSP_TYPE_STR: return wxT("str");
    case QSP_TYPE_CODE: return wxT("code");
    case QSP_TYPE_VARREF: return wxT("varref");
    }
    return wxT("undef");
}

/* {"type":..,"value":..}, with tuples nested as arrays of the same shape */
static void QSPDevAppendVariant(QSPJsonBuilder &builder, const QSPVariant &value, int depth)
{
    builder.StartObject();
    builder.Member(wxT("type"), QSPDevTypeName(value.Type));
    builder.Key(wxT("value"));
    if (QSP_ISTUPLE(value.Type))
    {
        /* A tuple can hold tuples. The nesting is bounded in practice, but the
           depth is capped anyway so a cycle-free but pathological value cannot
           blow the stack while an editor is merely watching it. */
        if (depth >= 8)
            builder.ValueString(wxT("..."));
        else
        {
            builder.StartArray();
            for (int i = 0; i < value.Val.Tuple.ValsCount; ++i)
                QSPDevAppendVariant(builder, value.Val.Tuple.Vals[i], depth + 1);
            builder.EndArray();
        }
    }
    else if (QSP_ISNUM(value.Type))
        builder.ValueRaw(wxLongLong((wxLongLong_t)QSP_NUM(value)).ToString());
    else
        builder.ValueString(qspToWxString(QSP_STR(value)));
    builder.EndObject();
}

/* The same value as a plain string, which is what change detection compares.
   Two values that print alike are treated as equal - good enough to drive an
   editor's change highlighting, and far cheaper than a deep compare. */
static wxString QSPDevVariantToText(const QSPVariant &value, int depth)
{
    if (QSP_ISTUPLE(value.Type))
    {
        if (depth >= 8) return wxT("[...]");
        wxString result(wxT("["));
        for (int i = 0; i < value.Val.Tuple.ValsCount; ++i)
        {
            if (i) result += wxT(", ");
            result += QSPDevVariantToText(value.Val.Tuple.Vals[i], depth + 1);
        }
        return result + wxT("]");
    }
    if (QSP_ISNUM(value.Type))
        return wxLongLong((wxLongLong_t)QSP_NUM(value)).ToString();
    return qspToWxString(QSP_STR(value));
}

void QSPDevServer::AppendVarValue(QSPJsonBuilder &builder, const wxString &name, int index) const
{
    QSPVariant value;
    if (QSPGetVarValue(QSPDevString(name), index, &value))
        QSPDevAppendVariant(builder, value, 0);
    else
        builder.ValueNull();
}

/* One variable as {name, count, values[]}. Returns false when the engine holds
   no variable by that name, so the caller can leave it out of a dump. */
bool QSPDevServer::AppendVar(QSPJsonBuilder &builder, const wxString &name, int maxValues, bool includeEmpty) const
{
    /* Validated first: qspVarReference raises QSP_ERR_INCORRECTNAME for a
       malformed name, which would leave the engine's error state dirty for
       whatever the game does next. */
    if (!QSPCode::IsValidVarName(name)) return false;

    /* QSPGetVarValuesCount answers for any syntactically valid name: the
       engine hands back a shared empty variable rather than nothing when a
       name is unknown. Holding values is therefore the only signal that a
       variable exists, and a variable emptied down to zero values cannot be
       told apart from one that was never set. */
    int count = 0;
    QSPGetVarValuesCount(QSPDevString(name), &count);
    bool exists = (count > 0);
    if (!exists && !includeEmpty) return false;

    int shown = (maxValues >= 0 && count > maxValues ? maxValues : count);

    builder.StartObject();
    builder.Member(wxT("name"), name);
    builder.MemberBool(wxT("exists"), exists);
    builder.MemberInt(wxT("count"), count);
    builder.MemberBool(wxT("truncated"), shown < count);
    builder.Key(wxT("values"));
    builder.StartArray();
    for (int i = 0; i < shown; ++i)
        AppendVarValue(builder, name, i);
    builder.EndArray();
    builder.EndObject();
    return true;
}

/* ------------------------------------------------------------------ */
/* Recovering variable names                                           */
/* ------------------------------------------------------------------ */

/* QSP cannot list the variables it holds: qspGlobalVars is private to the
   library and nothing in the public API walks it. What the API will do is
   answer for a name, so the names are recovered from the world's own source -
   every token that reads as a variable reference - and each candidate is then
   asked about. A name the game only ever builds at runtime (DYNAMIC, or SET
   through a computed name) is not in the source and so is not found this way;
   a client that knows such a name can still pass it in explicitly. */
/* Statement and function names, taken from the engine's own tables in
   statements.c and mathops.c. They read exactly like variable references in
   the source, and while the value lookup weeds them out on its own - the
   engine holds no variable by those names - a name list offered for
   completion should not be full of them. */
static const wxChar *g_devKeywords[] =
{
    wxT("ACT"), wxT("ADDOBJ"), wxT("ADDQST"), wxT("AND"), wxT("ARRCOMP"), wxT("ARRITEM"), wxT("ARRPACK"),
    wxT("ARRPOS"), wxT("ARRSIZE"), wxT("ARRTYPE"), wxT("CLA"), wxT("CLEAR"), wxT("CLOSE"),
    wxT("CLR"), wxT("CLS"), wxT("CMDCLEAR"), wxT("CMDCLR"), wxT("COPYARR"), wxT("COUNTOBJ"),
    wxT("CURACTS"), wxT("CURLOC"), wxT("CUROBJS"), wxT("DELACT"), wxT("DELOBJ"), wxT("DESC"),
    wxT("DYNAMIC"), wxT("DYNEVAL"), wxT("END"), wxT("EXEC"), wxT("EXIT"), wxT("FREELIB"),
    wxT("FUNC"), wxT("GETOBJ"), wxT("GOSUB"), wxT("GOTO"), wxT("GS"), wxT("GT"), wxT("IF"),
    wxT("IIF"), wxT("INCLIB"), wxT("INPUT"), wxT("INSTR"), wxT("ISNUM"), wxT("ISPLAY"),
    wxT("JUMP"), wxT("KILLALL"), wxT("KILLOBJ"), wxT("KILLQST"), wxT("KILLVAR"), wxT("LCASE"), wxT("LEN"),
    wxT("LET"), wxT("LOC"), wxT("LOCAL"), wxT("LOOP"), wxT("MAINTXT"), wxT("MAX"), wxT("MENU"),
    wxT("MID"), wxT("MIN"), wxT("MOD"), wxT("MODOBJ"), wxT("MSECSCOUNT"), wxT("MSG"),
    wxT("NL"), wxT("NO"), wxT("OBJ"), wxT("OPENGAME"), wxT("OPENQST"), wxT("OR"), wxT("P"),
    wxT("PL"), wxT("PLAY"), wxT("QSPVER"), wxT("RAND"), wxT("REFINT"), wxT("REPLACE"),
    wxT("RESETOBJ"), wxT("RGB"), wxT("RND"), wxT("SAVEGAME"), wxT("SCANSTR"), wxT("SELACT"),
    wxT("SELOBJ"), wxT("SET"), wxT("SETTIMER"), wxT("SETVAR"), wxT("SHOWACTS"),
    wxT("SHOWINPUT"), wxT("SHOWOBJS"), wxT("SHOWSTAT"), wxT("SORTARR"), wxT("STATTXT"),
    wxT("STR"), wxT("STRCOMP"), wxT("STRFIND"), wxT("STRPOS"), wxT("TRIM"), wxT("UCASE"),
    wxT("UNPACKARR"), wxT("UNSEL"), wxT("UNSELECT"), wxT("USER_TEXT"), wxT("USRTXT"),
    wxT("VAL"), wxT("VIEW"), wxT("WAIT"), wxT("XGOTO"), wxT("XGT")
};

static bool QSPDevIsKeyword(const wxString &name)
{
    for (size_t i = 0; i < WXSIZEOF(g_devKeywords); ++i)
    {
        if (name == g_devKeywords[i]) return true;
    }
    return false;
}

static bool QSPDevIsNameChar(wxUniChar ch)
{
    wxUint32 code = ch.GetValue();
    /* Anything outside ASCII is a letter as far as QSP is concerned - a great
       many games name their variables in Russian. */
    if (code > 127) return true;
    return code == '_' ||
           (code >= '0' && code <= '9') ||
           (code >= 'a' && code <= 'z') ||
           (code >= 'A' && code <= 'Z');
}

static bool QSPDevIsDigit(wxUniChar ch)
{
    wxUint32 code = ch.GetValue();
    return code >= '0' && code <= '9';
}

static void QSPDevCollectVarNames(const wxString &code, std::set<wxString> &names)
{
    size_t pos = 0, length = code.length();
    while (pos < length)
    {
        wxUniChar ch = code[pos];

        /* Skip over literals - their contents are text, not code. Code blocks
           in braces are deliberately not skipped: they hold real code. */
        if (ch == wxT('\'') || ch == wxT('"'))
        {
            wxUniChar quote = ch;
            ++pos;
            while (pos < length)
            {
                if (code[pos] == quote)
                {
                    /* A doubled quote is an escaped one, not the end */
                    if (pos + 1 < length && code[pos + 1] == quote)
                        pos += 2;
                    else
                    {
                        ++pos;
                        break;
                    }
                }
                else
                    ++pos;
            }
            continue;
        }

        if (ch == wxT('$') || ch == wxT('%') || QSPDevIsNameChar(ch))
        {
            size_t start = pos;
            if (ch == wxT('$') || ch == wxT('%')) ++pos;
            size_t nameStart = pos;
            while (pos < length && QSPDevIsNameChar(code[pos])) ++pos;
            if (pos == nameStart)
                continue; /* a lone sigil */

            /* A digit cannot start a name, so this was a number */
            if (QSPDevIsDigit(code[nameStart]) && start == nameStart)
                continue;

            /* Followed by "(" it is a function call, not a variable. Arrays
               subscript with "[", which is left alone. */
            size_t after = pos;
            while (after < length && (code[after] == wxT(' ') || code[after] == wxT('\t'))) ++after;
            if (after < length && code[after] == wxT('(')) continue;

            wxString name = code.Mid(nameStart, pos - nameStart);
            name.MakeUpper();
            /* A sigil settles it: "$MID" is a variable, bare "MID" is the
               function. Without one, a keyword is assumed to be the keyword. */
            if (start == nameStart && QSPDevIsKeyword(name)) continue;
            names.insert(name);
            continue;
        }

        ++pos;
    }
}

const std::vector<wxString> &QSPDevServer::GetKnownVarNames()
{
    if (m_varNamesValid) return m_varNames;

    std::set<wxString> names;
    /* Owned by the engine rather than by any game's source */
    names.insert(wxT("ARGS"));
    names.insert(wxT("RESULT"));
    names.insert(wxT("DEBUG"));

    int locCount = QSPGetLocationNames(0, 0);
    if (locCount > 0)
    {
        std::vector<QSPString> locations(locCount);
        QSPGetLocationNames(&locations[0], locCount);
        for (int i = 0; i < locCount; ++i)
        {
            wxString locName = qspToWxString(locations[i]);

            int lineCount = QSPGetLocationCode(QSPDevString(locName), 0, 0);
            if (lineCount > 0)
            {
                std::vector<QSPLineInfo> lines(lineCount);
                QSPGetLocationCode(QSPDevString(locName), &lines[0], lineCount);
                for (int j = 0; j < lineCount; ++j)
                    QSPDevCollectVarNames(qspToWxString(lines[j].Line), names);
            }

            int actCount = QSPGetLocationActions(QSPDevString(locName), 0, 0);
            for (int a = 0; a < actCount; ++a)
            {
                int actLines = QSPGetLocationActionCode(QSPDevString(locName), a, 0, 0);
                if (actLines <= 0) continue;
                std::vector<QSPLineInfo> lines(actLines);
                QSPGetLocationActionCode(QSPDevString(locName), a, &lines[0], actLines);
                for (int j = 0; j < actLines; ++j)
                    QSPDevCollectVarNames(qspToWxString(lines[j].Line), names);
            }
        }
    }

    m_varNames.assign(names.begin(), names.end());
    m_varNamesValid = true;
    return m_varNames;
}

void QSPDevServer::InvalidateVarNames()
{
    m_varNames.clear();
    m_varNamesValid = false;
}

/* ------------------------------------------------------------------ */
/* Watches                                                             */
/* ------------------------------------------------------------------ */

void QSPDevServer::SetWatch(const std::vector<wxString> &names)
{
    m_watch.clear();
    for (size_t i = 0; i < names.size(); ++i)
    {
        wxString name = QSPDevNormalizeVarName(names[i]);
        if (name.IsEmpty() || !QSPCode::IsValidVarName(name)) continue;

        bool duplicate = false;
        for (size_t j = 0; j < m_watch.size(); ++j)
        {
            if (m_watch[j].name == name) { duplicate = true; break; }
        }
        if (duplicate) continue;

        WatchEntry entry;
        entry.name = name;
        entry.exists = false;
        m_watch.push_back(entry);
    }
    CaptureWatch();
}

/* Reads every watched variable and forgets what it saw before, so the next
   comparison reports movement since now rather than since the watch was set. */
void QSPDevServer::CaptureWatch()
{
    for (size_t i = 0; i < m_watch.size(); ++i)
    {
        WatchEntry &entry = m_watch[i];
        int count = 0;
        QSPGetVarValuesCount(QSPDevString(entry.name), &count);
        entry.exists = (count > 0);
        entry.values.clear();
        entry.values.reserve(count);
        for (int j = 0; j < count; ++j)
        {
            QSPVariant value;
            if (QSPGetVarValue(QSPDevString(entry.name), j, &value))
                entry.values.push_back(QSPDevVariantToText(value, 0));
            else
                entry.values.push_back(wxEmptyString);
        }
    }
}

/* Diffs the watched variables against what was last seen and writes the moves
   into a JSON array, updating the remembered values as it goes. Returns false
   when nothing changed, so a caller can skip an empty notification.

   Nothing here runs game code: reading a value is a hash lookup, which is why
   this is safe to call from the debug callback with the interpreter still on
   the stack. */
bool QSPDevServer::CollectWatchChanges(QSPJsonBuilder &changes)
{
    bool any = false;
    changes.StartArray();
    for (size_t i = 0; i < m_watch.size(); ++i)
    {
        WatchEntry &entry = m_watch[i];

        int count = 0;
        QSPGetVarValuesCount(QSPDevString(entry.name), &count);
        bool exists = (count > 0);

        std::vector<wxString> current;
        current.reserve(count);
        for (int j = 0; j < count; ++j)
        {
            QSPVariant value;
            if (QSPGetVarValue(QSPDevString(entry.name), j, &value))
                current.push_back(QSPDevVariantToText(value, 0));
            else
                current.push_back(wxEmptyString);
        }

        if (exists == entry.exists && current == entry.values) continue;

        /* Per entry, not shared with the rest: the fallback below has to know
           whether *this* variable produced anything, not whether some earlier
           one did. */
        bool changed = false;
        size_t shared = (current.size() < entry.values.size() ? current.size() : entry.values.size());
        for (size_t j = 0; j < shared; ++j)
        {
            if (current[j] == entry.values[j]) continue;
            changed = true;
            changes.StartObject();
            changes.Member(wxT("name"), entry.name);
            changes.MemberInt(wxT("index"), (long)j);
            changes.Member(wxT("kind"), wxT("changed"));
            changes.Member(wxT("old"), entry.values[j]);
            changes.Key(wxT("new"));
            AppendVarValue(changes, entry.name, (int)j);
            changes.EndObject();
        }
        for (size_t j = shared; j < current.size(); ++j)
        {
            changed = true;
            changes.StartObject();
            changes.Member(wxT("name"), entry.name);
            changes.MemberInt(wxT("index"), (long)j);
            changes.Member(wxT("kind"), wxT("added"));
            changes.Key(wxT("new"));
            AppendVarValue(changes, entry.name, (int)j);
            changes.EndObject();
        }
        for (size_t j = shared; j < entry.values.size(); ++j)
        {
            changed = true;
            changes.StartObject();
            changes.Member(wxT("name"), entry.name);
            changes.MemberInt(wxT("index"), (long)j);
            changes.Member(wxT("kind"), wxT("removed"));
            changes.Member(wxT("old"), entry.values[j]);
            changes.EndObject();
        }
        /* An emptied variable whose values all vanished still counts as news */
        if (!changed && exists != entry.exists)
        {
            changed = true;
            changes.StartObject();
            changes.Member(wxT("name"), entry.name);
            changes.MemberInt(wxT("index"), -1);
            changes.Member(wxT("kind"), exists ? wxT("added") : wxT("removed"));
            changes.EndObject();
        }

        if (changed) any = true;
        entry.exists = exists;
        entry.values.swap(current);
    }
    changes.EndArray();
    return any;
}

void QSPDevServer::FlushWatch(const wxString &reason)
{
    m_watchReason.Clear();
    if (m_watch.empty() || m_clients.empty()) return;

    QSPJsonBuilder changes;
    if (!CollectWatchChanges(changes)) return;

    /* Safe to evaluate $CURLOC here: this only runs with the engine idle.
       While game code is on the stack the changes ride along with a trace
       event instead, which carries the executing location it was read at. */
    wxString loc = GetExecutingLocation();
    if (loc.IsEmpty()) loc = GetCurrentLocation();

    QSPJsonBuilder params;
    params.StartObject();
    params.Member(wxT("reason"), reason);
    params.Member(wxT("loc"), loc);
    params.Key(wxT("changes"));
    params.ValueRaw(changes.GetText());
    params.EndObject();
    Notify(wxT("varsChanged"), params.GetText());
}

/* ------------------------------------------------------------------ */
/* Tracing                                                             */
/* ------------------------------------------------------------------ */

void QSPDevServer::SetTracing(bool isOn)
{
    if (isOn == m_tracing) return;

    m_tracing = isOn;
    if (!isOn)
    {
        m_traceEvents.clear();
        m_traceDropped = 0;
        /* The filter is deliberately kept: turning tracing off and on again
           is how a client steps, and it should not widen each time. */
    }
    UpdateDebugHook();
}

/* The debug callback costs a call per executed line, so it is installed only
   while something actually wants it - tracing, a breakpoint, or a step the
   client has asked for and not yet been given. */
void QSPDevServer::UpdateDebugHook()
{
    bool isWanted = m_tracing || m_profiling || !m_breakpoints.empty() ||
                    m_breakRequested || m_stepMode != Step_None;
    bool isInstalled = (g_devTraceServer == this);
    if (isWanted == isInstalled) return;

    if (isWanted)
    {
        g_devTraceServer = this;
        QSPSetCallback(QSP_CALL_DEBUG, (QSP_CALLBACK)&QSPDevDebugCallback);
        QSPEnableDebugMode(QSP_TRUE);
    }
    else
    {
        QSPEnableDebugMode(QSP_FALSE);
        QSPSetCallback(QSP_CALL_DEBUG, (QSP_CALLBACK)0);
        g_devTraceServer = 0;
    }
}

/* Runs with game code still on the stack, once per executed line. The tracing
   half is all plain reads and queues its result rather than sending it, because
   a socket write can fail short; the breakpoint half may not return at all
   until the client resumes. */
void QSPDevServer::OnDebugLine(const wxString &line)
{
    if (m_clients.empty()) return;
    /* Re-entering from inside the pause loop would nest one pause in another */
    if (m_paused) return;

    /* Read before anything else this function does, so the line that has just
       finished is charged for its own time and not for the profiler's. */
    double entered = (m_profiling ? QSPDev::NowMs() : 0.0);

    QSPString execLoc;
    int actIndex = -1, lineNum = 0;
    QSPGetCurStateData(&execLoc, &actIndex, &lineNum);
    wxString loc = qspToWxString(execLoc);

    if (m_tracing) RecordTrace(loc, actIndex, lineNum, line);
    /* Last, and it reads the clock again on the way out: the trace above runs
       on the game's stack but is not the game's cost. */
    if (m_profiling) ProfileLine(loc, lineNum, line, entered);

    wxString reason;
    if (TakeBreakDecision(loc, lineNum, reason))
        EnterPause(reason, loc, actIndex, lineNum, line);
}

void QSPDevServer::RecordTrace(const wxString &loc, int actIndex, int lineNum, const wxString &line)
{
    if (!m_traceLocs.empty())
    {
        /* Filtered before the event is built, and before the cap is charged:
           a game spends most of its lines in code the client is not looking
           at, and every one of those would otherwise eat into the budget. */
        bool wanted = false;
        wxString upperLoc(loc);
        upperLoc.MakeUpper();
        for (size_t i = 0; i < m_traceLocs.size(); ++i)
        {
            if (m_traceLocs[i] == upperLoc) { wanted = true; break; }
        }
        if (!wanted) return;
    }

    if ((int)m_traceEvents.size() >= m_traceLimit)
    {
        ++m_traceDropped;
        return;
    }

    TraceEvent event;
    event.loc = loc;
    event.actIndex = actIndex;
    event.lineNum = lineNum;
    if (m_traceLines) event.line = line;

    if (m_traceVars && !m_watch.empty())
    {
        QSPJsonBuilder changes;
        if (CollectWatchChanges(changes))
            event.changes = changes.GetText();
    }

    m_traceEvents.push_back(event);
}

/* Trace events go out in batches. One notification per executed line would
   spend more time in the socket than the game spends interpreting. */
void QSPDevServer::FlushTrace()
{
    if (m_traceEvents.empty() && !m_traceDropped) return;
    if (m_clients.empty())
    {
        m_traceEvents.clear();
        m_traceDropped = 0;
        return;
    }

    QSPJsonBuilder params;
    params.StartObject();
    params.MemberInt(wxT("dropped"), m_traceDropped);
    params.Key(wxT("events"));
    params.StartArray();
    for (size_t i = 0; i < m_traceEvents.size(); ++i)
    {
        const TraceEvent &event = m_traceEvents[i];
        params.StartObject();
        params.Member(wxT("loc"), event.loc);
        params.MemberInt(wxT("actIndex"), event.actIndex);
        params.MemberInt(wxT("lineNum"), event.lineNum);
        if (m_traceLines) params.Member(wxT("line"), event.line);
        if (!event.changes.IsEmpty())
        {
            params.Key(wxT("changes"));
            params.ValueRaw(event.changes);
        }
        params.EndObject();
    }
    params.EndArray();
    params.EndObject();

    m_traceEvents.clear();
    m_traceDropped = 0;
    Notify(wxT("trace"), params.GetText());
}

/* ------------------------------------------------------------------ */
/* Profiling                                                           */
/* ------------------------------------------------------------------ */

void QSPDevServer::SetProfiling(bool isOn)
{
    if (isOn == m_profiling) return;

    if (isOn)
    {
        ResetProfile();
        m_profiling = true;
        /* The player's own counters ride along: a refresh is the one thing the
           line profiler cannot see, and it is usually the answer. */
        QSPDev::g_profOn = true;
    }
    else
    {
        CloseProfileSample(false);
        m_profiling = false;
        m_profStopMs = QSPDev::NowMs();
        if (!m_monitorOn) QSPDev::g_profOn = false;
    }
    UpdateDebugHook();
}

void QSPDevServer::ResetProfile()
{
    m_profLocs.clear();
    m_profEdges.clear();
    m_profStack.clear();
    m_profDepth.clear();
    m_profSamples = 0;
    m_profDropped = 0;
    m_profSelfMs = 0.0;
    m_profWaitMs = 0.0;
    m_profOpen = false;
    m_profSampleLoc = 0;
    m_profSampleLineRec = 0;
    m_profStartMs = QSPDev::NowMs();
    m_profStopMs = 0.0;
    QSPDev::ResetProfCounters();

    /* A monitor running across this would otherwise diff the fresh counters
       against the old totals and report one empty window. Re-based here, so
       its next sample is everything since the reset. */
    m_monitorLastSamples = 0;
    m_monitorLastSelfMs = 0.0;
    m_monitorLastWaitMs = 0.0;
    memset(m_monitorLast, 0, sizeof(m_monitorLast));
}

void QSPDevServer::ChargeProfileSample(double now, bool isWait)
{
    if (!m_profOpen) return;

    m_profOpen = false;
    double elapsed = now - m_profSampleStart;
    /* The clock is monotonic, so this only guards against a caller passing a
       timestamp taken before the sample was opened. */
    if (elapsed < 0.0) elapsed = 0.0;

    if (m_profSampleLoc)
    {
        if (isWait)
        {
            m_profSampleLoc->waitMs += elapsed;
            m_profWaitMs += elapsed;
            if (m_profSampleLineRec) m_profSampleLineRec->waitMs += elapsed;
        }
        else
        {
            m_profSampleLoc->selfMs += elapsed;
            m_profSelfMs += elapsed;
            if (m_profSampleLineRec)
            {
                m_profSampleLineRec->selfMs += elapsed;
                if (elapsed > m_profSampleLineRec->maxMs)
                    m_profSampleLineRec->maxMs = elapsed;
            }
        }
    }
    m_profSampleLoc = 0;
    m_profSampleLineRec = 0;
}

void QSPDevServer::CloseProfileSample(bool isWait)
{
    double now = QSPDev::NowMs();
    ChargeProfileSample(now, isWait);
    /* Waiting means game code is still on the stack - a SLEEP, a dialog, a
       forced refresh - so its frames have not returned and are left alone.
       Otherwise the interpreter is off the stack entirely and all of them
       have. */
    if (!isWait) ProfileLeaveTo(0, now);
}

/* The engine reports where it is, never how it got there: there is no call
   stack in the public API. One is inferred from the locations the lines arrive
   from - a line from a location already on the stack is a return to it, and
   anything else is a call. That is exact for the GOTO and GOSUB chains a game
   is made of, and approximate in one place: a location that recurses into
   itself reads as one long stay rather than as nested frames, so its inclusive
   time is the outermost call's, counted once. Which is what a report wants. */
void QSPDevServer::ProfileEnter(const wxString &loc, double now)
{
    if (!m_profStack.empty() && m_profStack.back().loc == loc) return;

    for (size_t depth = m_profStack.size(); depth > 0; --depth)
    {
        if (m_profStack[depth - 1].loc == loc)
        {
            ProfileLeaveTo(depth, now);
            return;
        }
    }

    if (!m_profStack.empty())
        ++m_profEdges[m_profStack.back().loc + wxT(">") + loc];
    ++m_profLocs[loc].calls;

    /* A guess that goes wrong must cost a lost frame rather than a growing
       vector. Nothing legitimate nests this deep - the engine's own recursion
       limit is far below it. */
    if (m_profStack.size() >= QSP_DEV_PROFMAXDEPTH) return;

    ProfFrame frame;
    frame.loc = loc;
    frame.enteredMs = now;
    m_profStack.push_back(frame);
    ++m_profDepth[loc];
}

void QSPDevServer::ProfileLeaveTo(size_t depth, double now)
{
    while (m_profStack.size() > depth)
    {
        ProfFrame frame = m_profStack.back();
        m_profStack.pop_back();

        std::map<wxString, int>::iterator it = m_profDepth.find(frame.loc);
        if (it == m_profDepth.end() || it->second <= 0) continue;
        --it->second;
        /* Only the outermost frame contributes, or a recursive location would
           be charged the same milliseconds once per level. */
        if (it->second == 0)
        {
            double elapsed = now - frame.enteredMs;
            if (elapsed > 0.0) m_profLocs[frame.loc].inclMs += elapsed;
        }
    }
}

/* Called from the engine's debug callback with the line that is about to run.
   The time since the last call belongs to the last line, which is what makes
   the hook a profiler: every line is measured, none are sampled past. */
void QSPDevServer::ProfileLine(const wxString &loc, int lineNum, const wxString &line, double entered)
{
    ChargeProfileSample(entered, false);
    ProfileEnter(loc, entered);

    LocProfile &locProfile = m_profLocs[loc];
    ++locProfile.hits;
    ++m_profSamples;

    LineProfile *lineRecord = 0;
    std::map<int, LineProfile>::iterator it = locProfile.lines.find(lineNum);
    if (it != locProfile.lines.end())
    {
        lineRecord = &it->second;
    }
    else if ((int)locProfile.lines.size() < m_profMaxLines)
    {
        lineRecord = &locProfile.lines[lineNum];
        if (m_profLines) lineRecord->line = line;
    }
    else
    {
        /* Past the cap the line still counts towards its location, it just
           gets no record of its own. */
        ++m_profDropped;
    }
    if (lineRecord) ++lineRecord->hits;

    m_profSampleLoc = &locProfile;
    m_profSampleLineRec = lineRecord;
    m_profOpen = true;
    /* Read last: everything above is the profiler's cost, not the game's. */
    m_profSampleStart = QSPDev::NowMs();
}

/* One location's row in a report, sorted by whichever column was asked for */
struct QSPDevProfRow
{
    const wxString *loc;
    const void *data;
    double key;
};

static bool QSPDevProfRowLess(const QSPDevProfRow &left, const QSPDevProfRow &right)
{
    /* Descending, and by name where the measurements tie, so two reports of
       the same run come out in the same order. Lines have no name to tie-break
       with - most of them cost the same unmeasurable nothing - and are sorted
       stably instead, which leaves them in line-number order. */
    if (left.key != right.key) return left.key > right.key;
    if (!left.loc || !right.loc) return false;
    return *left.loc < *right.loc;
}

void QSPDevServer::AppendProfileReport(QSPJsonBuilder &result, const wxString &sort,
                                       int limit, int lineLimit, bool withLines) const
{
    double until = (m_profiling || m_profStopMs <= 0.0) ? QSPDev::NowMs() : m_profStopMs;
    double elapsed = until - m_profStartMs;
    if (elapsed < 0.0) elapsed = 0.0;

    result.MemberDouble(wxT("elapsedMs"), elapsed);
    result.MemberInt64(wxT("lines"), m_profSamples);
    result.MemberDouble(wxT("selfMs"), m_profSelfMs);
    result.MemberDouble(wxT("waitMs"), m_profWaitMs);
    result.MemberInt64(wxT("droppedLines"), m_profDropped);
    result.MemberInt(wxT("locationCount"), (long)m_profLocs.size());
    result.MemberInt64(wxT("memoryKB"), QSPDev::GetProcessMemoryKB());

    std::vector<QSPDevProfRow> rows;
    rows.reserve(m_profLocs.size());
    std::map<wxString, LocProfile>::const_iterator it;
    for (it = m_profLocs.begin(); it != m_profLocs.end(); ++it)
    {
        QSPDevProfRow row;
        row.loc = &it->first;
        row.data = &it->second;
        if (sort == wxT("incl")) row.key = it->second.inclMs;
        else if (sort == wxT("wait")) row.key = it->second.waitMs;
        else if (sort == wxT("hits")) row.key = (double)it->second.hits;
        else if (sort == wxT("calls")) row.key = (double)it->second.calls;
        else row.key = it->second.selfMs;
        rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end(), QSPDevProfRowLess);

    result.Key(wxT("locations"));
    result.StartArray();
    for (size_t i = 0; i < rows.size() && (limit <= 0 || (int)i < limit); ++i)
    {
        const LocProfile &locProfile = *(const LocProfile *)rows[i].data;
        result.StartObject();
        result.Member(wxT("loc"), *rows[i].loc);
        result.MemberInt64(wxT("hits"), locProfile.hits);
        result.MemberInt64(wxT("calls"), locProfile.calls);
        result.MemberDouble(wxT("selfMs"), locProfile.selfMs);
        result.MemberDouble(wxT("inclMs"), locProfile.inclMs);
        result.MemberDouble(wxT("waitMs"), locProfile.waitMs);
        result.MemberDouble(wxT("selfPct"),
            m_profSelfMs > 0.0 ? locProfile.selfMs * 100.0 / m_profSelfMs : 0.0, 2);
        if (!withLines)
        {
            result.MemberInt(wxT("lineCount"), (long)locProfile.lines.size());
            result.EndObject();
            continue;
        }

        std::vector<QSPDevProfRow> lineRows;
        lineRows.reserve(locProfile.lines.size());
        std::map<int, LineProfile>::const_iterator lineIt;
        for (lineIt = locProfile.lines.begin(); lineIt != locProfile.lines.end(); ++lineIt)
        {
            QSPDevProfRow row;
            row.loc = 0;
            row.data = &(*lineIt);
            row.key = (sort == wxT("hits") ? (double)lineIt->second.hits
                                           : lineIt->second.selfMs + lineIt->second.waitMs);
            lineRows.push_back(row);
        }
        /* By cost, but stably, so the line numbers of equally cheap lines stay
           in the order the map holds them - QSPDevProfRowLess cannot break the
           tie here, there being no name to break it with. */
        std::stable_sort(lineRows.begin(), lineRows.end(), QSPDevProfRowLess);

        result.Key(wxT("lines"));
        result.StartArray();
        for (size_t j = 0; j < lineRows.size() && (lineLimit <= 0 || (int)j < lineLimit); ++j)
        {
            const std::pair<const int, LineProfile> &entry =
                *(const std::pair<const int, LineProfile> *)lineRows[j].data;
            result.StartObject();
            result.MemberInt(wxT("lineNum"), (long)entry.first);
            result.MemberInt64(wxT("hits"), entry.second.hits);
            result.MemberDouble(wxT("selfMs"), entry.second.selfMs);
            result.MemberDouble(wxT("maxMs"), entry.second.maxMs);
            if (entry.second.waitMs > 0.0)
                result.MemberDouble(wxT("waitMs"), entry.second.waitMs);
            if (!entry.second.line.IsEmpty())
                result.Member(wxT("line"), entry.second.line);
            result.EndObject();
        }
        result.EndArray();
        result.MemberBool(wxT("truncated"),
                          lineLimit > 0 && (int)lineRows.size() > lineLimit);
        result.EndObject();
    }
    result.EndArray();
    result.MemberBool(wxT("truncated"), limit > 0 && (int)rows.size() > limit);

    /* The call graph, as edges rather than a tree: a tree would have to be
       rebuilt for every root, and an editor drawing a flame graph or an arrow
       between two boxes wants the edges anyway. */
    result.Key(wxT("edges"));
    result.StartArray();
    std::map<wxString, long long>::const_iterator edgeIt;
    for (edgeIt = m_profEdges.begin(); edgeIt != m_profEdges.end(); ++edgeIt)
    {
        int split = edgeIt->first.Find(wxT('>'));
        if (split == wxNOT_FOUND) continue;
        result.StartObject();
        result.Member(wxT("from"), edgeIt->first.Left(split));
        result.Member(wxT("to"), edgeIt->first.Mid(split + 1));
        result.MemberInt64(wxT("calls"), edgeIt->second);
        result.EndObject();
    }
    result.EndArray();

    /* What the interpreter asked the player to do, which the line profiler
       sees only as time passing inside a line. */
    result.Key(wxT("counters"));
    result.StartArray();
    for (int i = 0; i < QSPDev::Prof_CounterCount; ++i)
    {
        const QSPDev::ProfCounterData &counter = QSPDev::g_profCounters[i];
        if (!counter.count) continue;
        result.StartObject();
        result.Member(wxT("name"), QSPDev::GetProfCounterName(i));
        result.MemberInt64(wxT("count"), counter.count);
        result.MemberDouble(wxT("totalMs"), counter.totalMs);
        result.MemberDouble(wxT("maxMs"), counter.maxMs);
        if (counter.bytes) result.MemberInt64(wxT("bytes"), counter.bytes);
        result.EndObject();
    }
    result.EndArray();
}

bool QSPDevServer::CmdProfile(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString action(params.GetString(wxT("action"), wxT("report")));
    action.MakeLower();

    bool isChange = (action == wxT("start") || action == wxT("stop") || action == wxT("reset"));
    if (isChange && m_paused)
    {
        /* Turning the hook on or off from inside the hook, with game code held
           on the stack, is the one thing this must not do. Reading is fine. */
        errorText = wxT("Profiling cannot be started or stopped while the game is paused");
        return false;
    }

    if (action == wxT("start"))
    {
        if (params.Has(wxT("lines"))) m_profLines = params.GetBool(wxT("lines"), true);
        if (params.Has(wxT("maxLines")))
        {
            long maxLines = params.GetInt(wxT("maxLines"), QSP_DEV_PROFMAXLINES);
            if (maxLines < 1) maxLines = 1;
            m_profMaxLines = (int)maxLines;
        }
        /* Always a fresh run: a client that asks to start twice means "from
           here", not "carry on from whatever was there". */
        SetProfiling(false);
        SetProfiling(true);
    }
    else if (action == wxT("stop"))
    {
        SetProfiling(false);
    }
    else if (action == wxT("reset"))
    {
        ResetProfile();
    }
    else if (action != wxT("report") && action != wxT("status"))
    {
        errorText = wxT("Unknown profile action: ") + action;
        return false;
    }

    long limit = params.GetInt(wxT("limit"), 50);
    long lineLimit = params.GetInt(wxT("lineLimit"), 50);
    bool withLines = params.GetBool(wxT("withLines"), true);
    wxString sort(params.GetString(wxT("sort"), wxT("self")));
    sort.MakeLower();

    result.StartObject();
    result.MemberBool(wxT("running"), m_profiling);
    result.MemberBool(wxT("keepLines"), m_profLines);
    result.MemberInt(wxT("maxLines"), m_profMaxLines);
    if (action != wxT("status"))
        AppendProfileReport(result, sort, (int)limit, (int)lineLimit, withLines);
    result.EndObject();
    return true;
}

/* ------------------------------------------------------------------ */
/* Live monitor                                                        */
/* ------------------------------------------------------------------ */

void QSPDevServer::SetMonitor(bool isOn, int intervalMs)
{
    if (intervalMs < QSP_DEV_MONITORMININTERVAL) intervalMs = QSP_DEV_MONITORMININTERVAL;
    m_monitorIntervalMs = intervalMs;
    m_monitorTimer.Stop();

    if (!isOn)
    {
        m_monitorOn = false;
        if (!m_profiling) QSPDev::g_profOn = false;
        return;
    }

    m_monitorOn = true;
    /* The player's counters are what the monitor is made of. They are free
       enough to leave running for a session; the line hook is not, and is not
       touched here - a monitor is not a profile. */
    QSPDev::g_profOn = true;
    m_monitorLastMs = QSPDev::NowMs();
    m_monitorLastSamples = m_profSamples;
    m_monitorLastSelfMs = m_profSelfMs;
    m_monitorLastWaitMs = m_profWaitMs;
    memcpy(m_monitorLast, QSPDev::g_profCounters, sizeof(m_monitorLast));
    m_monitorTimer.Start(m_monitorIntervalMs);
}

void QSPDevServer::OnMonitorTimer(wxTimerEvent &WXUNUSED(event))
{
    FlushMonitor();
}

/* A window of the counters rather than their totals: what a graph plots is the
   last half second, and a client that wants totals asks for a report. The
   timer is the sampler, so the sample keeps arriving while the game sits
   still - which is the whole point of a monitor as against a profile. */
void QSPDevServer::FlushMonitor()
{
    if (!m_monitorOn || m_clients.empty()) return;

    double now = QSPDev::NowMs();
    double window = now - m_monitorLastMs;
    if (window <= 0.0) return;

    QSPJsonBuilder params;
    params.StartObject();
    params.MemberDouble(wxT("windowMs"), window);
    params.MemberBool(wxT("profiling"), m_profiling);
    params.MemberBool(wxT("busy"), QSPDev::IsEngineBusy());

    /* Line counts only mean anything while the line hook is installed. They
       are reported as zero rather than left out, so a client graphing them
       does not have to special-case the gap. */
    long long lines = m_profSamples - m_monitorLastSamples;
    double selfMs = m_profSelfMs - m_monitorLastSelfMs;
    double waitMs = m_profWaitMs - m_monitorLastWaitMs;
    if (lines < 0) lines = 0;
    if (selfMs < 0.0) selfMs = 0.0;
    if (waitMs < 0.0) waitMs = 0.0;
    params.MemberInt64(wxT("lines"), lines);
    params.MemberDouble(wxT("linesPerSec"), lines * 1000.0 / window, 0);
    params.MemberDouble(wxT("selfMs"), selfMs);
    params.MemberDouble(wxT("waitMs"), waitMs);
    /* How much of the window the player spent inside the interpreter. A game
       that pegs this is one that never lets the UI breathe. */
    params.MemberDouble(wxT("busyPct"), selfMs * 100.0 / window, 1);
    params.MemberInt64(wxT("memoryKB"), QSPDev::GetProcessMemoryKB());

    params.Key(wxT("counters"));
    params.StartArray();
    for (int i = 0; i < QSPDev::Prof_CounterCount; ++i)
    {
        const QSPDev::ProfCounterData &counter = QSPDev::g_profCounters[i];
        long long count = counter.count - m_monitorLast[i].count;
        double totalMs = counter.totalMs - m_monitorLast[i].totalMs;
        long long bytes = counter.bytes - m_monitorLast[i].bytes;
        /* A reset between two ticks leaves these negative; the window is then
           simply reported as empty rather than as a spike downwards. */
        if (count <= 0 || totalMs < 0.0) continue;
        params.StartObject();
        params.Member(wxT("name"), QSPDev::GetProfCounterName(i));
        params.MemberInt64(wxT("count"), count);
        params.MemberDouble(wxT("totalMs"), totalMs);
        params.MemberDouble(wxT("maxMs"), counter.maxMs);
        if (bytes > 0) params.MemberInt64(wxT("bytes"), bytes);
        params.EndObject();
    }
    params.EndArray();
    params.EndObject();

    m_monitorLastMs = now;
    m_monitorLastSamples = m_profSamples;
    m_monitorLastSelfMs = m_profSelfMs;
    m_monitorLastWaitMs = m_profWaitMs;
    memcpy(m_monitorLast, QSPDev::g_profCounters, sizeof(m_monitorLast));

    Notify(wxT("perf"), params.GetText());
}

bool QSPDevServer::CmdMonitor(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &WXUNUSED(errorText))
{
    long interval = params.GetInt(wxT("intervalMs"), m_monitorIntervalMs);
    if (params.Has(wxT("enabled")))
        SetMonitor(params.GetBool(wxT("enabled"), true), (int)interval);
    else if (m_monitorOn && interval != m_monitorIntervalMs)
        SetMonitor(true, (int)interval);

    result.StartObject();
    result.MemberBool(wxT("enabled"), m_monitorOn);
    result.MemberInt(wxT("intervalMs"), m_monitorIntervalMs);
    result.MemberBool(wxT("profiling"), m_profiling);
    result.EndObject();
    return true;
}

/* ------------------------------------------------------------------ */
/* Breakpoints and stepping                                            */
/* ------------------------------------------------------------------ */

/* Location names are matched case-insensitively, the way the engine matches
   them, so they are folded once here rather than at every comparison. */
wxString QSPDevServer::MakeBreakKey(const wxString &loc, long lineNum)
{
    wxString upper(loc);
    upper.MakeUpper();
    return wxString::Format(wxT("%s:%ld"), upper, lineNum < 0 ? 0 : lineNum);
}

bool QSPDevServer::TakeBreakDecision(const wxString &loc, int lineNum, wxString &reason)
{
    /* A pause asked for while nothing was running: honoured at the first line
       that does run, and forgotten afterwards. */
    if (m_breakRequested)
    {
        m_breakRequested = false;
        reason = wxT("pause");
        return true;
    }
    if (m_stepMode == Step_Line)
    {
        m_stepMode = Step_None;
        reason = wxT("step");
        return true;
    }
    if (m_stepMode == Step_Location && !loc.IsSameAs(m_stepFromLoc, false))
    {
        m_stepMode = Step_None;
        reason = wxT("step");
        return true;
    }
    if (m_breakpoints.empty()) return false;

    /* Two lookups rather than a scan: the whole-location form and the exact
       line. This runs once per executed line, so it has to stay cheap. */
    if (m_breakpoints.count(MakeBreakKey(loc, 0)) ||
        m_breakpoints.count(MakeBreakKey(loc, lineNum)))
    {
        reason = wxT("breakpoint");
        return true;
    }
    return false;
}

/* Stops the game where it stands, with the line that triggered it not yet
   finished. Returning from here is what resumes it, so this holds a nested
   event loop until the client says to go on.

   Two things make that safe. The windows are disabled for the duration, so a
   click on an action cannot call the engine from inside the line it is already
   executing. And the engine is marked busy, which keeps the ordinary command
   queue from draining - only the commands that read state are dispatched, by
   DrainWhilePaused. */
/* Stops the game where it stands, with the line that triggered it not yet
   finished. Returning from here is what resumes it, so this holds until the
   client says to go on.

   It deliberately does NOT pump the wxWidgets event loop. That was the first
   attempt and it is unsound twice over. A breakpoint is reached from inside
   Dispatch (an "exec" is what ran the code), so m_inCommand is already set and
   the nested drain could never serve anything - the pause was unbreakable. And
   a client dropped during the nested pump is destroyed while the outer Dispatch
   frame still holds its pointer and is about to write its response to it, which
   is a use-after-free.

   So the sockets are read directly instead. Nothing is dispatched through the
   normal path, no events run, and no client is ever destroyed while we are
   standing inside the engine. The window stops repainting for the duration,
   which is what being stopped in a debugger looks like. */
void QSPDevServer::EnterPause(const wxString &reason, const wxString &loc, int actIndex,
                              int lineNum, const wxString &line)
{
    if (m_paused || m_clients.empty()) return;

    m_paused = true;
    /* Held time is the debugger's, not the game's. The sample is closed rather
       than charged to the line, so a profile taken across a breakpoint reads
       the same as one taken without. */
    ChargeProfileSample(QSPDev::NowMs(), true);

    /* Sent before the loop starts, and flushed by hand: the client is waiting
       to be told where the game stopped, and nothing else will push it out. */
    QSPJsonBuilder params;
    params.StartObject();
    params.Member(wxT("reason"), reason);
    params.Member(wxT("loc"), loc);
    params.MemberInt(wxT("actIndex"), actIndex);
    params.MemberInt(wxT("lineNum"), lineNum);
    params.Member(wxT("line"), line);
    params.EndObject();
    Notify(wxT("paused"), params.GetText());
    for (size_t i = 0; i < m_clients.size(); ++i)
        FlushOutbox(m_clients[i]);

    {
        /* Marks the engine busy so that if anything does reach DrainQueue it
           still declines to call in. */
        QSPDev::EngineScope engineScope;

        /* Losing every client resumes the game rather than wedging the player
           on a breakpoint nobody is left to clear. */
        while (m_paused && !m_frame->ToQuit())
        {
            if (!ServePaused()) break;
        }
    }

    m_paused = false;
    UpdateDebugHook();

    if (m_clients.empty()) return;
    QSPJsonBuilder resumed;
    resumed.StartObject();
    resumed.Member(wxT("loc"), loc);
    resumed.EndObject();
    Notify(wxT("resumed"), resumed.GetText());
    for (size_t i = 0; i < m_clients.size(); ++i)
        FlushOutbox(m_clients[i]);
}

/* One pass over the connected clients while the game is stopped: push whatever
   is queued for them, then read whatever they have sent.

   Returns false when nobody is left who could resume us. A disconnected client
   is skipped and left alone - dropping it here would destroy a socket that an
   outer frame is still holding, which is the crash this rewrite exists to
   avoid. The ordinary wxSOCKET_LOST path cleans it up once the engine is out. */
bool QSPDevServer::ServePaused()
{
    bool anyConnected = false;

    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        wxSocketBase *socket = m_clients[i];
        if (!socket || !socket->IsConnected()) continue;
        anyConnected = true;

        FlushOutbox(socket);

        /* The timeout is what keeps this from spinning a core for as long as
           the breakpoint is held, which can be minutes. */
        if (!socket->WaitForRead(0, 20)) continue;

        char chunk[4096];
        socket->Read(chunk, sizeof(chunk));
        size_t count = socket->LastCount();
        if (!count) continue;

        std::string &buffer = m_buffers[socket];
        buffer.append(chunk, count);

        for (;;)
        {
            std::string::size_type breakPos = buffer.find('\n');
            if (breakPos == std::string::npos) break;

            std::string rawLine = buffer.substr(0, breakPos);
            buffer.erase(0, breakPos + 1);
            if (!rawLine.empty() && rawLine[rawLine.length() - 1] == '\r')
                rawLine.erase(rawLine.length() - 1);
            if (rawLine.empty()) continue;

            HandlePausedLine(socket, wxString::FromUTF8(rawLine.c_str(), rawLine.length()));
            /* A resume landed: the rest of this client's input, and every other
               client's, belongs to the running game again. */
            if (!m_paused) return true;
        }
    }

    return anyConnected;
}

/* Answered here only if it cannot touch the engine. Everything else keeps its
   place in the queue and runs on the resume - it is late, not refused. */
void QSPDevServer::HandlePausedLine(wxSocketBase *socket, const wxString &line)
{
    QSPJsonReader request;
    if (request.Parse(line) && IsPauseSafeMethod(request.GetString(wxT("method"))))
    {
        Dispatch(socket, line);
        FlushOutbox(socket);
        return;
    }

    PendingCommand queued;
    queued.socket = socket;
    queued.line = line;
    m_pending.push_back(queued);
}

/* What is safe to answer with game code on the stack, mid-statement.

   The bar is not "read-only" but "does not call back into the interpreter".
   getVar is in because it only does QSPGetVarValuesCount / QSPGetStrVarValue /
   QSPGetNumVarValue - the same calls CollectWatchChanges already makes from
   this very callback while tracing. state, vars, locations and locationCode
   are out despite being reads: they evaluate $CURLOC through
   QSPCalculateStrExpression, which runs engine code, and they read the action
   and description buffers while the engine is half way through rebuilding
   them. exec, eval, goto, reload, restore and restart are out for the obvious
   reason. */
bool QSPDevServer::IsPauseSafeMethod(const wxString &method)
{
    /* "profile" and "monitor" only read counters the player keeps for itself,
       so they are safe here; starting or stopping the profiler is not, and
       CmdProfile refuses that while paused rather than the dispatch doing it. */
    return method == wxT("resume") || method == wxT("pause") || method == wxT("break") ||
           method == wxT("ping") || method == wxT("hello") || method == wxT("getVar") ||
           method == wxT("profile") || method == wxT("monitor");
}

bool QSPDevServer::CmdBreak(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString action(params.GetString(wxT("action"), wxT("list")));

    if (action == wxT("set") || action == wxT("clear"))
    {
        wxString loc(params.GetString(wxT("loc")));
        if (loc.IsEmpty())
        {
            errorText = wxT("\"loc\" is required to set or clear a breakpoint");
            return false;
        }
        /* No line, or line 0, means the whole location - which is what an
           editor wants when it is told "stop when the player gets here". */
        wxString key(MakeBreakKey(loc, params.GetInt(wxT("line"), 0)));
        if (action == wxT("set"))
            m_breakpoints.insert(key);
        else
            m_breakpoints.erase(key);
    }
    else if (action == wxT("clearAll"))
    {
        m_breakpoints.clear();
    }
    else if (action != wxT("list"))
    {
        errorText = wxT("\"action\" must be set, clear, clearAll or list");
        return false;
    }

    UpdateDebugHook();

    result.StartObject();
    result.Key(wxT("breakpoints"));
    result.StartArray();
    for (std::set<wxString>::const_iterator it = m_breakpoints.begin(); it != m_breakpoints.end(); ++it)
    {
        result.StartObject();
        result.Member(wxT("loc"), it->BeforeLast(wxT(':')));
        long lineNum = 0;
        it->AfterLast(wxT(':')).ToLong(&lineNum);
        result.MemberInt(wxT("line"), lineNum);
        result.EndObject();
    }
    result.EndArray();
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdPause(const QSPJsonReader &WXUNUSED(params), QSPJsonBuilder &result,
                            wxString &WXUNUSED(errorText))
{
    /* There may be no game code running right now - a player sitting on a
       location is not executing anything - so this is a request honoured at
       the next line rather than an immediate stop. */
    m_breakRequested = true;
    UpdateDebugHook();

    result.StartObject();
    result.MemberBool(wxT("pending"), !m_paused);
    result.MemberBool(wxT("paused"), m_paused);
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdResume(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    wxString mode(params.GetString(wxT("mode"), wxT("run")));

    if (mode == wxT("run")) m_stepMode = Step_None;
    else if (mode == wxT("step")) m_stepMode = Step_Line;
    else if (mode == wxT("stepLoc")) m_stepMode = Step_Location;
    else
    {
        errorText = wxT("\"mode\" must be run, step or stepLoc");
        return false;
    }
    /* Read before the loop lets go, while the executing location is still the
       one the game stopped in */
    m_stepFromLoc = GetExecutingLocation();
    m_breakRequested = false;
    UpdateDebugHook();

    bool wasPaused = m_paused;
    m_paused = false;

    result.StartObject();
    result.MemberBool(wxT("ok"), true);
    result.MemberBool(wxT("wasPaused"), wasPaused);
    result.Member(wxT("mode"), mode);
    result.EndObject();
    return true;
}

/* ------------------------------------------------------------------ */
/* Variable commands                                                   */
/* ------------------------------------------------------------------ */

bool QSPDevServer::CmdVars(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &WXUNUSED(errorText))
{
    int maxValues = (int)params.GetInt(wxT("maxValues"), QSP_DEV_MAXVALUES);
    int maxVars = (int)params.GetInt(wxT("maxVars"), 0);
    bool includeEmpty = params.GetBool(wxT("includeEmpty"), false);
    wxString filter = params.GetString(wxT("filter"));
    filter.MakeUpper();

    /* An explicit list is answered as given - including names the scan could
       never have found, which is how a client watches something built at
       runtime. Without one, every name the world's source mentions is tried
       and the ones the engine actually holds come back. */
    std::vector<wxString> requested = params.GetStringArray(wxT("names"));
    bool isExplicit = !requested.empty();
    if (!isExplicit)
    {
        if (params.GetBool(wxT("rescan"), false)) InvalidateVarNames();
        requested = GetKnownVarNames();
    }

    if (isExplicit) includeEmpty = true;

    int emitted = 0, skipped = 0;
    QSPJsonBuilder vars;
    vars.StartArray();
    for (size_t i = 0; i < requested.size(); ++i)
    {
        wxString name = QSPDevNormalizeVarName(requested[i]);
        if (name.IsEmpty()) continue;
        if (!filter.IsEmpty() && name.Find(filter) == wxNOT_FOUND) continue;

        if (maxVars > 0 && emitted >= maxVars)
        {
            ++skipped;
            continue;
        }
        if (AppendVar(vars, name, maxValues, includeEmpty))
            ++emitted;
    }
    vars.EndArray();

    result.StartObject();
    result.MemberInt(wxT("count"), emitted);
    result.MemberInt(wxT("skipped"), skipped);
    result.MemberBool(wxT("truncated"), skipped > 0);
    result.Member(wxT("loc"), GetCurrentLocation());
    result.Key(wxT("vars"));
    result.ValueRaw(vars.GetText());
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdVarNames(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &WXUNUSED(errorText))
{
    if (params.GetBool(wxT("rescan"), false)) InvalidateVarNames();

    wxString filter = params.GetString(wxT("filter"));
    filter.MakeUpper();
    bool existingOnly = params.GetBool(wxT("existing"), false);

    const std::vector<wxString> &names = GetKnownVarNames();

    result.StartObject();
    result.Key(wxT("names"));
    result.StartArray();
    int count = 0;
    for (size_t i = 0; i < names.size(); ++i)
    {
        if (!filter.IsEmpty() && names[i].Find(filter) == wxNOT_FOUND) continue;
        if (existingOnly)
        {
            int values = 0;
            if (!QSPCode::IsValidVarName(names[i])) continue;
            QSPGetVarValuesCount(QSPDevString(names[i]), &values);
            if (!values) continue;
        }
        result.ValueString(names[i]);
        ++count;
    }
    result.EndArray();
    result.MemberInt(wxT("count"), count);
    result.EndObject();
    return true;
}

/* A variable panel edits several cells at once, and one round trip per cell
   would run - and refresh - the game that many times. */
bool QSPDevServer::CmdSetVars(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText)
{
    std::vector<wxString> entries = params.GetRawArray(wxT("vars"));
    if (entries.empty())
    {
        errorText = wxT("\"vars\" is required and must be a non-empty array");
        return false;
    }

    wxString code;
    for (size_t i = 0; i < entries.size(); ++i)
    {
        QSPJsonReader entry;
        if (!entry.Parse(entries[i]))
        {
            errorText = wxString::Format(wxT("vars[%u] is not an object"), (unsigned)i);
            return false;
        }

        wxString name = entry.GetString(wxT("name"));
        if (name.IsEmpty())
        {
            errorText = wxString::Format(wxT("vars[%u] has no \"name\""), (unsigned)i);
            return false;
        }

        wxString line;
        if (!QSPCode::BuildAssignment(name,
                                      entry.GetString(wxT("index"), wxT("0")),
                                      entry.GetString(wxT("value")),
                                      entry.GetBool(wxT("append"), false),
                                      &line, &errorText))
        {
            errorText = wxString::Format(wxT("vars[%u]: "), (unsigned)i) + errorText;
            return false;
        }

        /* Built as one statement per line so a failure reports the line the
           engine actually choked on. */
        if (!code.IsEmpty()) code += wxT("\r\n");
        code += line;
    }

    if (!RunCode(code, params.GetBool(wxT("refresh"), true), errorText))
        return false;

    result.StartObject();
    result.MemberBool(wxT("ok"), true);
    result.MemberInt(wxT("count"), (long)entries.size());
    result.Member(wxT("loc"), GetCurrentLocation());
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdWatch(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &WXUNUSED(errorText))
{
    if (params.Has(wxT("names")))
        SetWatch(params.GetStringArray(wxT("names")));
    else if (params.GetBool(wxT("reset"), false))
        CaptureWatch();

    result.StartObject();
    result.MemberInt(wxT("count"), (long)m_watch.size());
    result.Key(wxT("names"));
    result.StartArray();
    for (size_t i = 0; i < m_watch.size(); ++i)
        result.ValueString(m_watch[i].name);
    result.EndArray();
    result.EndObject();
    return true;
}

bool QSPDevServer::CmdTrace(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &WXUNUSED(errorText))
{
    if (params.Has(wxT("lines"))) m_traceLines = params.GetBool(wxT("lines"), true);
    if (params.Has(wxT("vars"))) m_traceVars = params.GetBool(wxT("vars"), true);
    if (params.Has(wxT("limit")))
    {
        long limit = params.GetInt(wxT("limit"), QSP_DEV_TRACELIMIT);
        if (limit < 1) limit = 1;
        m_traceLimit = (int)limit;
    }
    if (params.Has(wxT("locs")))
    {
        std::vector<wxString> locs = params.GetStringArray(wxT("locs"));
        m_traceLocs.clear();
        for (size_t i = 0; i < locs.size(); ++i)
        {
            wxString loc(locs[i]);
            loc.MakeUpper();
            if (!loc.IsEmpty()) m_traceLocs.push_back(loc);
        }
    }
    if (params.Has(wxT("enabled")))
        SetTracing(params.GetBool(wxT("enabled"), true));

    result.StartObject();
    result.MemberBool(wxT("enabled"), m_tracing);
    result.MemberBool(wxT("lines"), m_traceLines);
    result.MemberBool(wxT("vars"), m_traceVars);
    result.MemberInt(wxT("limit"), m_traceLimit);
    result.Key(wxT("locs"));
    result.StartArray();
    for (size_t i = 0; i < m_traceLocs.size(); ++i)
        result.ValueString(m_traceLocs[i]);
    result.EndArray();
    result.EndObject();
    return true;
}
