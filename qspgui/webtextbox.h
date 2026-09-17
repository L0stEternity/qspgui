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

       Unlike the classic renderer this never re-navigates once the shell
       document is up: text changes are pushed into the live DOM, which is
       what keeps location changes from flashing. */
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
        /* Markup from $SETMAINDESCHEAD. It lives in the document head so it
           survives the content rebuilds games do on every refresh. */
        void SetHeadContent(const wxString& head);
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
        void PushContent();
        void PushStyle();
        void PushHead();
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
        bool m_isShellRequested;
        PathProvider *m_pathProvider;
        bool m_isShellReady;
        bool m_toUseHtml;
        bool m_toScroll;
        wxString m_text;
        wxString m_headContent;
        wxString m_backImagePath;
        wxFont m_font;
        wxColour m_linkColor;
        wxColour m_backColor;
        wxColour m_fontColor;
        wxString m_gameDir;
        wxString m_baseUrl;
    };

#endif
