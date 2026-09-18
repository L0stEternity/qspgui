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

#ifndef WEBTEXTBOX_H
    #define WEBTEXTBOX_H

    #include <wx/wx.h>
    #include <wx/arrstr.h>
    #include "webpane.h"

    class QSPWebTextBox;

    /* One call from the game's JS into the engine.

       The pane itself knows nothing about QSP: it queues one of these to its
       parent, which owns the engine, and the answer travels back through
       ResolveScriptCall(). Queued rather than sent, so the engine is never
       entered from inside the browser's own message callback. */
    class QSPScriptCallEvent : public wxCommandEvent
    {
    public:
        QSPScriptCallEvent(wxEventType type = wxEVT_NULL, wxWindowID id = 0)
            : wxCommandEvent(type, id), m_callId(0) {}
        QSPScriptCallEvent(const QSPScriptCallEvent& event)
            : wxCommandEvent(event), m_callId(event.m_callId),
              m_op(event.m_op), m_args(event.m_args) {}

        virtual wxEvent *Clone() const { return new QSPScriptCallEvent(*this); }

        void SetCallId(long callId) { m_callId = callId; }
        long GetCallId() const { return m_callId; }
        void SetOp(const wxString& op) { m_op = op; }
        const wxString& GetOp() const { return m_op; }
        void SetArgs(const wxArrayString& args) { m_args = args; }
        /* Missing arguments read as empty rather than throwing: the caller is
           the game's own JS and may pass fewer than an operation expects. */
        wxString GetArg(size_t index) const
        {
            return (index < m_args.GetCount() ? m_args[index] : wxString());
        }
        size_t GetArgsCount() const { return m_args.GetCount(); }

    private:
        long m_callId;
        wxString m_op;
        wxArrayString m_args;
    };

    wxDECLARE_EVENT(wxEVT_QSP_SCRIPT_CALL, QSPScriptCallEvent);


    /* Drop-in replacement for QSPTextBox backed by a real browser engine
       (WebView2 on Windows, WebKitGTK on Linux, WKWebView on macOS).

       The document is loaded once and never navigated again, because a
       navigation blanks the view before the new page paints and that blank
       frame is the flash. Instead the document holds two stacked layers: an
       update fills the hidden one, waits until it is laid out and its images
       have decoded, and only then swaps which layer is visible. The old
       content stays on screen until the new one is complete, so the pane goes
       straight from one finished state to the next. */
    class QSPWebTextBox : public QSPWebPane
    {
        DECLARE_CLASS(QSPWebTextBox)
    public:
        // C-tors / D-tor
        QSPWebTextBox(wxWindow *parent, wxWindowID id);

        // Methods
        void RefreshUI();
        void LoadBackImage(const wxString& imagePath);
        void LoadPage(const wxString& location);
        /* Game-supplied CSS and JS. Both are declarative: the pane holds the
           current value and re-applies it whenever the document is rebuilt, so
           a game only has to set the variables once. */
        void SetUserStyles(const wxString& inlineCss, const wxArrayString& files);
        void SetUserScripts(const wxString& inlineJs, const wxArrayString& files);
        /* Answer a call the game's JS made through window.qsp. */
        void ResolveScriptCall(long callId, bool isOk, const wxString& value, bool isNum);

        // Accessors
        void SetIsHtml(bool isHtml);
        void SetText(const wxString& text, bool toScroll = false);
        /* A single refresh touches the pane repeatedly - text, colours, font,
           background - and may clear it before filling it again. Nothing is
           sent between Begin and End, so the view is handed one finished state
           per refresh instead of a sequence of half-built ones. */
        void BeginUpdate();
        void EndUpdate();
        void SetTextFont(const wxFont& font);
        wxFont GetTextFont() const { return m_font; }
        wxString GetText() const { return m_text; }
        void SetLinkColor(const wxColour& clr);
        const wxColour& GetLinkColor() const { return m_linkColor; }

        // Overloaded methods
        virtual bool SetBackgroundColour(const wxColour& colour);
        virtual bool SetForegroundColour(const wxColour& colour);

    protected:
        // Internal methods
        void MarkDirty();
        void Flush();
        wxString BuildUpdateScript() const;
        wxString BuildStyleObject() const;
        wxString BuildUserStylesScript() const;
        wxString BuildUserScriptsScript() const;
        static wxString BuildShellDocument();

        // Overridden from QSPWebPane
        virtual void OnShellReady();
        virtual void OnPaneMessage(const wxString& message);

        // Fields
        bool m_isUpdatePending;
        int m_updateDepth;
        bool m_toUseHtml;
        bool m_toScroll;
        wxString m_text;
        wxString m_backImagePath;
        wxFont m_font;
        wxColour m_linkColor;
        wxColour m_backColor;
        wxColour m_fontColor;
        wxString m_userCss;
        wxArrayString m_userCssFiles;
        wxString m_userJs;
        wxArrayString m_userJsFiles;
    };

#endif
