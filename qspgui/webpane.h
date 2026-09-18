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

#ifndef WEBPANE_H
    #define WEBPANE_H

    #include <wx/wx.h>
    #include <wx/webview.h>
    #include <wx/arrstr.h>
    #include "pathprovider.h"

    /* Virtual hosts used by the Edge backend. Mapping a host to a folder keeps
       relative paths contained inside that folder (the URL parser collapses
       "..", so a game cannot walk out of its own directory) and gives media a
       real https origin, which file:// does not - seeking in <video> needs
       it. */
    #define QSP_SHELL_HOST wxT("qsp.shell")
    #define QSP_GAME_HOST  wxT("qsp.game")
    #define QSP_SHELL_DIR  wxT("qspgui_web")

    /* Small conversions every web-backed pane needs. Free functions rather
       than a namespace only so they can be reached from the panes' own
       translation units without another header each. */
    class QSPWebUtil
    {
    public:
        /* A JS string literal, quotes included. Anything outside a
           conservative safe set is escaped as \uXXXX, so the script's own
           encoding never has to be reasoned about. */
        static wxString ToJsString(const wxString& str);
        static wxString ToJsArray(const wxArrayString& items);
        /* Undo encodeURIComponent() */
        static wxString FromUriComponent(const wxString& str);
        static wxString ToCssColor(const wxColour& color);
        /* Tags a shell URL so a build that changed the document is not served
           the cached previous one */
        static wxString HashOf(const wxString& str);
        static wxString ToFileUrl(const wxString& dirPath);
    };

    /* Something the game's own JS did wrong: a thrown error, a rejected
       promise, a console.error, or an asset that would not load. Raised by a
       pane and handled by the frame, which logs it and hands it to a connected
       editor - a game script failing silently is otherwise indistinguishable
       from a game script doing nothing. */
    class QSPScriptDiagEvent : public wxCommandEvent
    {
    public:
        QSPScriptDiagEvent(wxEventType type = wxEVT_NULL, wxWindowID id = 0)
            : wxCommandEvent(type, id) {}
        QSPScriptDiagEvent(const QSPScriptDiagEvent& event)
            : wxCommandEvent(event), m_kind(event.m_kind), m_text(event.m_text),
              m_where(event.m_where), m_pane(event.m_pane) {}

        virtual wxEvent *Clone() const { return new QSPScriptDiagEvent(*this); }

        /* "error", "warning" or "resource" */
        void SetKind(const wxString& kind) { m_kind = kind; }
        const wxString& GetKind() const { return m_kind; }
        void SetText(const wxString& text) { m_text = text; }
        const wxString& GetText() const { return m_text; }
        /* File and position, "console", or the URL that failed to load */
        void SetWhere(const wxString& where) { m_where = where; }
        const wxString& GetWhere() const { return m_where; }
        void SetPane(const wxString& pane) { m_pane = pane; }
        const wxString& GetPane() const { return m_pane; }

    private:
        wxString m_kind;
        wxString m_text;
        wxString m_where;
        wxString m_pane;
    };

    wxDECLARE_EVENT(wxEVT_QSP_SCRIPT_DIAG, QSPScriptDiagEvent);

    /* What every pane backed by a browser engine needs and none of them should
       own a copy of: the view itself, the shell document and the virtual hosts
       that serve it and the game folder, the navigation veto that keeps the
       document from ever being replaced, keyboard forwarding back to the
       frame, and the developer aids.

       A subclass supplies its own document through InitShell and is told when
       that document is live; everything above the message channel is its own
       business.

       The document is never navigated after startup. A navigation blanks the
       view before the new page paints, and that blank frame is the flash the
       web renderer exists to avoid - so links and clicks are routed through
       the script message channel instead. The single exception is a game
       folder change, which is once per game load, never per location. */
    class QSPWebPane : public wxPanel
    {
        DECLARE_CLASS(QSPWebPane)
    public:
        QSPWebPane(wxWindow *parent, wxWindowID id);

        void SetPathProvider(PathProvider *provider);

        /* Developer aids: the devtools, the browser context menu that opens
           them, and F12. Off in an ordinary player, because a right-click menu
           offering "View source" in the middle of a game is not what a reader
           expects.

           Static because panes are built inside the frame's constructor,
           before anything outside holds a pointer to one - so this has to be
           set before the frame is created. */
        static void EnableDevMode(bool isOn) { ms_isDevMode = isOn; }
        static bool IsDevModeEnabled() { return ms_isDevMode; }
        /* No-op unless dev mode is on and the backend supports it */
        void ShowDevTools();

        void SetPaneName(const wxString& name);
        const wxString& GetPaneName() const { return m_paneName; }

    protected:
        /* Called by the subclass's constructor with the document it wants and
           the file name to serve it under. One file per pane kind, all in the
           same directory behind the shell host. */
        void InitShell(const wxString& fileName, const wxString& document);

        /* The document is loaded and its script has announced itself. The
           subclass pushes its current state in here, because a fresh document
           knows nothing of what came before it. */
        virtual void OnShellReady() {}
        /* A message the base does not handle: anything whose first character
           is not one of the ones it claims. */
        virtual void OnPaneMessage(const wxString& WXUNUSED(message)) {}

        void RunScript(const wxString& script);
        bool IsShellReady() const { return m_isShellReady; }
        const wxString& GetBaseUrl() const { return m_baseUrl; }
        PathProvider *GetPathProvider() const { return m_pathProvider; }
        wxWebView *GetView() const { return m_view; }

        /* The stock diagnostics hook, appended to a subclass's document so
           every pane reports a failing script the same way. Closes the
           document's IIFE and announces readiness, so it goes last. */
        static wxString GetDiagnosticsScript();
        /* Key forwarding and the context-menu suppression, for a document that
           wants the standard behaviour. Goes before the diagnostics. */
        static wxString GetInputScript();

        void SetupGameFolderAccess();

        // Events
        void OnSize(wxSizeEvent& event);
        void OnWebViewLoaded(wxWebViewEvent& event);
        void OnWebViewNavigating(wxWebViewEvent& event);
        void OnWebViewError(wxWebViewEvent& event);
        void OnScriptMessage(wxWebViewEvent& event);

        // Fields
        wxWebView *m_view;
        PathProvider *m_pathProvider;
        wxString m_shellUrl;
        wxString m_shellFile;
        bool m_isShellRequested;
        bool m_isShellReady;
        wxString m_gameDir;
        wxString m_baseUrl;
        wxString m_paneName;

    private:
        bool SetupShellHost();
        void LoadShell();
        wxString GetShellDir() const;
        wxString GetShellPath() const;
        void WriteShellFile(const wxString& document);

        static bool ms_isDevMode;
    };

#endif
