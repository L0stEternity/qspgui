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

#ifndef DEVSERVER_H
    #define DEVSERVER_H

    #include <wx/wx.h>
    #include <wx/socket.h>
    #include "devjson.h"
    #include <map>
    #include <set>
    #include <string>
    #include <vector>

    class QSPFrame;

    namespace QSPDev
    {
        /* Non-zero while game code is running with a nested event loop spinning
           inside it (SLEEP yields, input/message dialogs, menus). Socket
           commands must never call into the engine while that is the case, so
           they are queued and drained once it drops back to zero. */
        extern int g_engineDepth;

        class EngineScope
        {
        public:
            EngineScope() { ++g_engineDepth; }
            ~EngineScope() { --g_engineDepth; }
        };

        inline bool IsEngineBusy() { return g_engineDepth > 0; }
    }

    /* ------------------------------------------------------------------ */
    /* Development API server.
       Line-delimited JSON-RPC 2.0 over a loopback TCP socket, enabled with
       --dev. Lets an external editor reload the game world, run code and
       inspect state without restarting the player or replaying a save. */
    /* ------------------------------------------------------------------ */

    class QSPDevServer : public wxEvtHandler
    {
    public:
        QSPDevServer(QSPFrame *frame);
        virtual ~QSPDevServer();

        bool Start(unsigned short port, const wxString &token);
        void Stop();

        bool IsRunning() const { return m_server != 0; }
        unsigned short GetPort() const { return m_port; }

        /* Notifications pushed to every connected client */
        void NotifyRefreshed(bool isNewDesc);
        void NotifyError();
        void NotifyMessage(const wxString &text);
        void NotifyGameOpened(const wxString &path);
        /* Something the game's own JavaScript did wrong, reported by the web
           renderer's shell. Kind is "error", "warning" or "resource". */
        void NotifyScriptDiag(const wxString &kind, const wxString &text,
                              const wxString &where, const wxString &pane);

        /* Called from the engine's debug callback, once per executed line */
        void OnDebugLine(const wxString &line);

    private:
        void OnServerEvent(wxSocketEvent &event);
        void OnClientEvent(wxSocketEvent &event);
        void OnIdle(wxIdleEvent &event);

        void DropClient(wxSocketBase *socket);
        void ReadFrom(wxSocketBase *socket);
        void Send(wxSocketBase *socket, const wxString &line);
        void FlushOutbox(wxSocketBase *socket);
        void Broadcast(const wxString &line);
        void Notify(const wxString &method, const wxString &paramsJson);

        void FlushNotifications();
        void QueueLine(wxSocketBase *socket, const wxString &line);
        void DrainQueue();
        void Dispatch(wxSocketBase *socket, const wxString &line);

        bool Invoke(const wxString &method, const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);

        /* Commands */
        bool CmdState(QSPJsonBuilder &result, wxString &errorText);
        bool CmdLocations(QSPJsonBuilder &result, wxString &errorText);
        bool CmdLocationCode(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdExec(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdEval(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdGetVar(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdSetVar(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdGoto(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdReload(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdSnapshot(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdRestore(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdVars(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdVarNames(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdSetVars(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdWatch(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdTrace(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdBreak(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdPause(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);
        bool CmdResume(const QSPJsonReader &params, QSPJsonBuilder &result, wxString &errorText);

        /* Helpers */
        bool TakeSnapshot(std::vector<char> &snapshot, wxString &errorText);
        bool RunCode(const wxString &code, bool toRefresh, wxString &errorText);
        wxString DescribeLastError() const;
        void AppendErrorInfo(QSPJsonBuilder &builder) const;
        wxString GetCurrentLocation() const;
        wxString GetExecutingLocation() const;

        /* Variable inspection */
        void AppendVarValue(QSPJsonBuilder &builder, const wxString &name, int index) const;
        bool AppendVar(QSPJsonBuilder &builder, const wxString &name, int maxValues, bool includeEmpty) const;
        const std::vector<wxString> &GetKnownVarNames();
        void InvalidateVarNames();

        /* Watches */
        void SetWatch(const std::vector<wxString> &names);
        void CaptureWatch();
        bool CollectWatchChanges(QSPJsonBuilder &changes);
        void FlushWatch(const wxString &reason);

        /* Tracing */
        void SetTracing(bool isOn);
        void FlushTrace();
        void RecordTrace(const wxString &loc, int actIndex, int lineNum, const wxString &line);

        /* Breakpoints and stepping.

           The engine has no debugger of its own; what it has is a callback per
           executed line, which is enough to decide whether to stop. Stopping
           means not returning from that callback, so the pause is a nested
           event loop with game code still on the stack - which is why only the
           commands that read state are served while it is held. */
        enum StepMode
        {
            Step_None = 0,
            Step_Line,     /* stop at the very next line */
            Step_Location  /* stop at the next line in a different location */
        };

        /* Installs or removes the engine's debug callback to match what is
           actually being asked of it */
        void UpdateDebugHook();
        /* Not const: a step or a one-shot pause request is consumed here */
        bool TakeBreakDecision(const wxString &loc, int lineNum, wxString &reason);
        void EnterPause(const wxString &reason, const wxString &loc, int actIndex,
                        int lineNum, const wxString &line);
        /* One pass over the connected clients while the game is stopped.
           Reads them directly rather than pumping the event loop - see
           EnterPause for why that is not an option. False when nobody is left
           who could resume us. */
        bool ServePaused();
        void HandlePausedLine(wxSocketBase *socket, const wxString &line);
        static bool IsPauseSafeMethod(const wxString &method);
        static wxString MakeBreakKey(const wxString &loc, long lineNum);

        struct PendingCommand
        {
            wxSocketBase *socket;
            wxString line;
        };

        /* One executed line, captured while game code is still on the stack */
        struct TraceEvent
        {
            wxString loc;
            wxString line;
            int actIndex;
            int lineNum;
            wxString changes; /* raw JSON array, empty when nothing moved */
        };

        /* Last seen values of a watched variable, as displayable strings.
           Values are kept rather than a hash so a change can report what it
           was as well as what it became. */
        struct WatchEntry
        {
            wxString name;
            bool exists;
            std::vector<wxString> values;
        };

        QSPFrame *m_frame;
        wxSocketServer *m_server;
        unsigned short m_port;
        wxString m_token;
        std::vector<wxSocketBase *> m_clients;
        /* Raw bytes, because a UTF-8 sequence can straddle two reads */
        std::map<wxSocketBase *, std::string> m_buffers;
        std::map<wxSocketBase *, bool> m_authorized;
        std::vector<PendingCommand> m_pending;
        std::map<wxString, std::vector<char> > m_slots;
        /* Writes are non-blocking, so a partial one has to be kept and retried
           rather than silently truncating the stream */
        std::map<wxSocketBase *, std::string> m_outbox;
        bool m_inCommand;
        bool m_idleHooked;
        /* A refresh arrives while game code is unwinding, when the current
           location cannot be read back, so it is announced from idle instead */
        bool m_refreshPending;
        bool m_refreshIsNewDesc;

        /* Variable names harvested from the loaded world's code. The engine
           cannot enumerate its variable table through the public API, so the
           list is rebuilt from the source and invalidated when the world is. */
        std::vector<wxString> m_varNames;
        bool m_varNamesValid;

        std::vector<WatchEntry> m_watch;
        wxString m_watchReason;

        bool m_tracing;
        bool m_traceVars;
        bool m_traceLines;
        /* Upper-case location names; empty means every location */
        std::vector<wxString> m_traceLocs;
        int m_traceLimit;
        int m_traceDropped;
        std::vector<TraceEvent> m_traceEvents;

        /* "LOCNAME:line", with line 0 meaning any line in that location */
        std::set<wxString> m_breakpoints;
        bool m_paused;
        /* A "pause" command, honoured at the next executed line and then
           forgotten - the engine may not be running any at the moment. */
        bool m_breakRequested;
        int m_stepMode;
        wxString m_stepFromLoc;
    };

#endif
