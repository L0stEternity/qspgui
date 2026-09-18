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
#include <set>
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
    ID_DEV_CLIENT
};

#define QSP_DEV_PROTOCOL 1
#define QSP_DEV_EXPRBUFSIZE (64 * 1024)
/* A loop can execute a hundred thousand lines between two idle turns. Trace
   events are batched, and the batch is capped so a runaway loop costs a
   counter rather than gigabytes of queued JSON. */
#define QSP_DEV_TRACELIMIT 2000
#define QSP_DEV_MAXVALUES 32

/* The engine's debug callback is a plain C function pointer with nowhere to
   put a context argument, so the server that asked for tracing is reachable
   only through a file-static. Only one player runs per process. */
static QSPDevServer *g_devTraceServer = 0;

static void QSPDevDebugCallback(QSPString line)
{
    if (g_devTraceServer) g_devTraceServer->OnDebugLine(qspToWxString(line));
}

/* ------------------------------------------------------------------ */
/* JSON reader                                                         */
/* ------------------------------------------------------------------ */

bool QSPJsonReader::SkipSpace(const wxString &text, size_t &pos)
{
    while (pos < text.length())
    {
        wxUniChar ch = text[pos];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            return true;
        ++pos;
    }
    return false;
}

/* Copies the next value verbatim, whatever its type. Nested containers are
   tracked by depth so that an object or an array can be handed back as raw
   text and decoded later only if some command actually needs it. */
bool QSPJsonReader::ReadToken(const wxString &text, size_t &pos, wxString &token)
{
    if (!SkipSpace(text, pos)) return false;

    size_t start = pos;
    wxUniChar ch = text[pos];
    if (ch == '"')
    {
        ++pos;
        while (pos < text.length())
        {
            wxUniChar cur = text[pos++];
            if (cur == '\\')
            {
                if (pos >= text.length()) return false;
                ++pos;
            }
            else if (cur == '"')
            {
                token = text.Mid(start, pos - start);
                return true;
            }
        }
        return false;
    }

    if (ch == '{' || ch == '[')
    {
        int depth = 0;
        bool inString = false;
        while (pos < text.length())
        {
            wxUniChar cur = text[pos++];
            if (inString)
            {
                if (cur == '\\')
                {
                    if (pos >= text.length()) return false;
                    ++pos;
                }
                else if (cur == '"')
                    inString = false;
            }
            else if (cur == '"')
                inString = true;
            else if (cur == '{' || cur == '[')
                ++depth;
            else if (cur == '}' || cur == ']')
            {
                if (--depth == 0)
                {
                    token = text.Mid(start, pos - start);
                    return true;
                }
            }
        }
        return false;
    }

    while (pos < text.length())
    {
        wxUniChar cur = text[pos];
        if (cur == ',' || cur == '}' || cur == ']' ||
            cur == ' ' || cur == '\t' || cur == '\r' || cur == '\n')
            break;
        ++pos;
    }
    if (pos == start) return false;
    token = text.Mid(start, pos - start);
    return true;
}

bool QSPJsonReader::Parse(const wxString &text)
{
    m_fields.clear();

    size_t pos = 0;
    if (!SkipSpace(text, pos)) return false;
    if (text[pos] != '{') return false;
    ++pos;

    if (!SkipSpace(text, pos)) return false;
    if (text[pos] == '}') return true;

    for (;;)
    {
        wxString keyToken;
        if (!ReadToken(text, pos, keyToken)) return false;
        wxString key;
        if (!DecodeString(keyToken, key)) return false;

        if (!SkipSpace(text, pos)) return false;
        if (text[pos] != ':') return false;
        ++pos;

        wxString valueToken;
        if (!ReadToken(text, pos, valueToken)) return false;
        m_fields[key] = valueToken;

        if (!SkipSpace(text, pos)) return false;
        wxUniChar ch = text[pos++];
        if (ch == '}') return true;
        if (ch != ',') return false;
    }
}

bool QSPJsonReader::DecodeString(const wxString &token, wxString &result)
{
    result.Clear();
    if (token.length() < 2 || token[0] != '"') return false;

    for (size_t i = 1; i + 1 < token.length(); ++i)
    {
        wxUniChar ch = token[i];
        if (ch != '\\')
        {
            result += ch;
            continue;
        }
        if (++i + 1 > token.length()) return false;
        wxUniChar esc = token[i];
        switch (esc.GetValue())
        {
        case '"': result += '"'; break;
        case '\\': result += '\\'; break;
        case '/': result += '/'; break;
        case 'b': result += '\b'; break;
        case 'f': result += '\f'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'u':
            {
                if (i + 4 >= token.length()) return false;
                unsigned long code = 0;
                for (int digit = 0; digit < 4; ++digit)
                {
                    wxUniChar hex = token[++i];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') code |= (unsigned long)(hex.GetValue() - '0');
                    else if (hex >= 'a' && hex <= 'f') code |= (unsigned long)(hex.GetValue() - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') code |= (unsigned long)(hex.GetValue() - 'A' + 10);
                    else return false;
                }
                result += wxUniChar((wxUint32)code);
                break;
            }
        default:
            return false;
        }
    }
    return true;
}

bool QSPJsonReader::Has(const wxString &key) const
{
    return m_fields.find(key) != m_fields.end();
}

wxString QSPJsonReader::GetRaw(const wxString &key) const
{
    std::map<wxString, wxString>::const_iterator it = m_fields.find(key);
    if (it == m_fields.end()) return wxEmptyString;
    return it->second;
}

wxString QSPJsonReader::GetString(const wxString &key, const wxString &defValue) const
{
    wxString token = GetRaw(key);
    if (token.IsEmpty()) return defValue;
    if (token[0] != '"') return token; /* tolerate a bare number or keyword */

    wxString value;
    if (!DecodeString(token, value)) return defValue;
    return value;
}

long QSPJsonReader::GetInt(const wxString &key, long defValue) const
{
    wxString token = GetRaw(key);
    if (token.IsEmpty()) return defValue;
    if (token[0] == '"')
    {
        wxString decoded;
        if (!DecodeString(token, decoded)) return defValue;
        token = decoded;
    }
    long value;
    if (!token.ToLong(&value)) return defValue;
    return value;
}

bool QSPJsonReader::GetBool(const wxString &key, bool defValue) const
{
    wxString token = GetRaw(key);
    if (token.IsEmpty()) return defValue;
    if (token == wxT("true")) return true;
    if (token == wxT("false")) return false;
    return GetInt(key, defValue ? 1 : 0) != 0;
}

/* Splits an array into its item tokens, verbatim. Strings keep their quotes;
   objects and nested arrays come back whole, to be parsed by the caller. */
std::vector<wxString> QSPJsonReader::GetRawArray(const wxString &key) const
{
    std::vector<wxString> items;
    wxString token = GetRaw(key);
    if (token.length() < 2 || token[0] != '[') return items;

    size_t pos = 1;
    if (!SkipSpace(token, pos)) return items;
    if (token[pos] == ']') return items;

    for (;;)
    {
        wxString itemToken;
        if (!ReadToken(token, pos, itemToken)) break;
        items.push_back(itemToken);

        if (!SkipSpace(token, pos)) break;
        wxUniChar ch = token[pos++];
        if (ch != ',') break;
    }
    return items;
}

std::vector<wxString> QSPJsonReader::GetStringArray(const wxString &key) const
{
    std::vector<wxString> items;
    wxString token = GetRaw(key);
    if (token.length() < 2 || token[0] != '[') return items;

    size_t pos = 1;
    if (!SkipSpace(token, pos)) return items;
    if (token[pos] == ']') return items;

    for (;;)
    {
        wxString itemToken;
        if (!ReadToken(token, pos, itemToken)) break;
        if (!itemToken.IsEmpty() && itemToken[0] == '"')
        {
            wxString item;
            if (DecodeString(itemToken, item))
                items.push_back(item);
        }
        else
            items.push_back(itemToken);

        if (!SkipSpace(token, pos)) break;
        wxUniChar ch = token[pos++];
        if (ch != ',') break;
    }
    return items;
}

/* ------------------------------------------------------------------ */
/* JSON builder                                                        */
/* ------------------------------------------------------------------ */

wxString QSPJsonBuilder::Escape(const wxString &text)
{
    wxString out;
    out.reserve(text.length() + 8);
    for (wxString::const_iterator it = text.begin(); it != text.end(); ++it)
    {
        wxUniChar ch = *it;
        switch (ch.GetValue())
        {
        case '"': out += wxT("\\\""); break;
        case '\\': out += wxT("\\\\"); break;
        case '\b': out += wxT("\\b"); break;
        case '\f': out += wxT("\\f"); break;
        case '\n': out += wxT("\\n"); break;
        case '\r': out += wxT("\\r"); break;
        case '\t': out += wxT("\\t"); break;
        default:
            if (ch.GetValue() < 0x20)
                out += wxString::Format(wxT("\\u%04x"), (unsigned int)ch.GetValue());
            else
                out += ch;
            break;
        }
    }
    return out;
}

void QSPJsonBuilder::Separate()
{
    if (m_needComma) m_out += wxT(",");
}

void QSPJsonBuilder::StartObject()
{
    Separate();
    m_out += wxT("{");
    m_needComma = false;
}

void QSPJsonBuilder::EndObject()
{
    m_out += wxT("}");
    m_needComma = true;
}

void QSPJsonBuilder::StartArray()
{
    Separate();
    m_out += wxT("[");
    m_needComma = false;
}

void QSPJsonBuilder::EndArray()
{
    m_out += wxT("]");
    m_needComma = true;
}

void QSPJsonBuilder::Key(const wxString &key)
{
    Separate();
    m_out += wxT("\"") + Escape(key) + wxT("\":");
    m_needComma = false;
}

void QSPJsonBuilder::ValueString(const wxString &value)
{
    Separate();
    m_out += wxT("\"") + Escape(value) + wxT("\"");
    m_needComma = true;
}

void QSPJsonBuilder::ValueInt(long value)
{
    Separate();
    m_out += wxString::Format(wxT("%ld"), value);
    m_needComma = true;
}

void QSPJsonBuilder::ValueBool(bool value)
{
    Separate();
    m_out += (value ? wxT("true") : wxT("false"));
    m_needComma = true;
}

void QSPJsonBuilder::ValueNull()
{
    Separate();
    m_out += wxT("null");
    m_needComma = true;
}

void QSPJsonBuilder::ValueRaw(const wxString &json)
{
    Separate();
    m_out += json;
    m_needComma = true;
}

void QSPJsonBuilder::Member(const wxString &key, const wxString &value)
{
    Key(key);
    ValueString(value);
}

void QSPJsonBuilder::MemberInt(const wxString &key, long value)
{
    Key(key);
    ValueInt(value);
}

void QSPJsonBuilder::MemberBool(const wxString &key, bool value)
{
    Key(key);
    ValueBool(value);
}

/* ------------------------------------------------------------------ */
/* Small QSP helpers                                                   */
/* ------------------------------------------------------------------ */

/* A writable copy of a string handed to the engine.
   qspPrepareStringToExecution upper-cases code in place, so anything passed to
   QSPExecString or the expression calls has to live in a buffer the engine may
   scribble on - a literal would fault, and a wxString would come back
   upper-cased. The copy lives as long as the temporary, i.e. until the end of
   the call it is an argument to. */
class QSPDevString
{
public:
    explicit QSPDevString(const wxString &text)
        : m_buffer(text.length() + 1, 0)
    {
        if (text.length())
            memcpy(&m_buffer[0], text.wc_str(), text.length() * sizeof(QSP_CHAR));
    }

    operator QSPString() { return qspStringFromLen(&m_buffer[0], (int)m_buffer.size() - 1); }

private:
    std::vector<QSP_CHAR> m_buffer;
};

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
      m_refreshIsNewDesc(false)
{
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
    m_outbox[socket].append(data.data(), data.length());
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
    for (size_t i = 0; i < m_clients.size(); ++i)
    {
        if (m_authorized[m_clients[i]])
            Send(m_clients[i], line);
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
    DrainQueue();
    FlushNotifications();
    for (size_t i = 0; i < m_clients.size(); ++i)
        FlushOutbox(m_clients[i]);
    if (!m_pending.empty())
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
    int size = 64 * 1024;
    snapshot.resize(size);
    if (!QSPSaveGameAsData(&snapshot[0], &size, QSP_FALSE))
    {
        /* The engine reports the size it needs through the same argument */
        if (!size)
        {
            errorText = DescribeLastError();
            return false;
        }
        snapshot.resize(size);
        if (!QSPSaveGameAsData(&snapshot[0], &size, QSP_FALSE))
        {
            errorText = DescribeLastError();
            return false;
        }
    }
    snapshot.resize(size);
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
    if (it == m_slots.end())
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
        wxFile file(path);
        wxFileOffset size = file.Length();
        world.resize((size_t)size);
        if (size && file.Read(&world[0], (size_t)size) != size)
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
    wxT("ACT"), wxT("ADDOBJ"), wxT("AND"), wxT("ARRCOMP"), wxT("ARRITEM"), wxT("ARRPACK"),
    wxT("ARRPOS"), wxT("ARRSIZE"), wxT("ARRTYPE"), wxT("CLA"), wxT("CLEAR"), wxT("CLOSE"),
    wxT("CLR"), wxT("CLS"), wxT("CMDCLEAR"), wxT("CMDCLR"), wxT("COPYARR"), wxT("COUNTOBJ"),
    wxT("CURACTS"), wxT("CURLOC"), wxT("CUROBJS"), wxT("DELACT"), wxT("DELOBJ"), wxT("DESC"),
    wxT("DYNAMIC"), wxT("DYNEVAL"), wxT("END"), wxT("EXEC"), wxT("EXIT"), wxT("FREELIB"),
    wxT("FUNC"), wxT("GETOBJ"), wxT("GOSUB"), wxT("GOTO"), wxT("GS"), wxT("GT"), wxT("IF"),
    wxT("IIF"), wxT("INCLIB"), wxT("INPUT"), wxT("INSTR"), wxT("ISNUM"), wxT("ISPLAY"),
    wxT("JUMP"), wxT("KILLALL"), wxT("KILLOBJ"), wxT("KILLVAR"), wxT("LCASE"), wxT("LEN"),
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

        size_t shared = (current.size() < entry.values.size() ? current.size() : entry.values.size());
        for (size_t j = 0; j < shared; ++j)
        {
            if (current[j] == entry.values[j]) continue;
            any = true;
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
            any = true;
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
            any = true;
            changes.StartObject();
            changes.Member(wxT("name"), entry.name);
            changes.MemberInt(wxT("index"), (long)j);
            changes.Member(wxT("kind"), wxT("removed"));
            changes.Member(wxT("old"), entry.values[j]);
            changes.EndObject();
        }
        /* An emptied variable whose values all vanished still counts as news */
        if (!any && exists != entry.exists)
        {
            any = true;
            changes.StartObject();
            changes.Member(wxT("name"), entry.name);
            changes.MemberInt(wxT("index"), -1);
            changes.Member(wxT("kind"), exists ? wxT("added") : wxT("removed"));
            changes.EndObject();
        }

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
    if (isOn)
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
        m_traceEvents.clear();
        m_traceDropped = 0;
        /* The filter is deliberately kept: turning tracing off and on again
           is how a client steps, and it should not widen each time. */
    }
}

/* Runs with game code still on the stack, once per executed line. Everything
   here is a plain read - the current state and, if asked for, the watched
   values - and the result is queued rather than sent, because the engine must
   not re-enter and a socket write can fail short. */
void QSPDevServer::OnDebugLine(const wxString &line)
{
    if (!m_tracing || m_clients.empty()) return;

    QSPString execLoc;
    int actIndex = -1, lineNum = 0;
    QSPGetCurStateData(&execLoc, &actIndex, &lineNum);

    wxString loc = qspToWxString(execLoc);
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
