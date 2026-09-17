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
    #include <wx/webview.h>
    #include "pathprovider.h"

    /* Drop-in replacement for QSPTextBox backed by a real browser engine
       (WebView2 on Windows, WebKitGTK on Linux, WKWebView on macOS).

       The document is loaded once and never navigated again, because a
       navigation blanks the view before the new page paints and that blank
       frame is the flash. Instead the document holds two stacked layers: an
       update fills the hidden one, waits until it is laid out and its images
       have decoded, and only then swaps which layer is visible. The old
       content stays on screen until the new one is complete, so the pane goes
       straight from one finished state to the next. */
    class QSPWebTextBox : public wxPanel
    {
        DECLARE_CLASS(QSPWebTextBox)
        DECLARE_EVENT_TABLE()
    public:
        // C-tors / D-tor
        QSPWebTextBox(wxWindow *parent, wxWindowID id);

        // Methods
        void RefreshUI();
        void LoadBackImage(const wxString& imagePath);
        void SetPathProvider(PathProvider *provider);
        void LoadPage(const wxString& location);

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
        void RunScript(const wxString& script);
        void SetupGameFolderAccess();
        bool SetupShellHost();
        void LoadShell();
        wxString GetShellDir() const;
        wxString GetShellPath() const;
        void WriteShellFile();

        // Events
        void OnSize(wxSizeEvent& event);
        void OnWebViewLoaded(wxWebViewEvent& event);
        void OnWebViewNavigating(wxWebViewEvent& event);
        void OnWebViewError(wxWebViewEvent& event);
        void OnScriptMessage(wxWebViewEvent& event);

        // Fields
        wxWebView *m_view;
        wxString m_shellUrl;
        wxString m_shellVersion;
        bool m_isShellRequested;
        bool m_isShellReady;
        bool m_isUpdatePending;
        int m_updateDepth;
        PathProvider *m_pathProvider;
        bool m_toUseHtml;
        bool m_toScroll;
        wxString m_text;
        wxString m_backImagePath;
        wxFont m_font;
        wxColour m_linkColor;
        wxColour m_backColor;
        wxColour m_fontColor;
        wxString m_gameDir;
        wxString m_baseUrl;
    };

#endif
