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

#ifndef WEBLISTBOX_H
    #define WEBLISTBOX_H

    #include <wx/wx.h>
    #include <wx/arrstr.h>
    #include "webpane.h"
    #include "listbox.h" /* for ListBoxType, shared with the classic renderer */

    /* Drop-in replacement for QSPListBox backed by the browser engine.

       The two lists behave differently and always have: the object list is an
       ordinary list where a click selects, and the action list selects on
       hover and runs on a single click, because that is what a reader expects
       of something that reads as a set of links. ListBoxType is what tells
       them apart, exactly as in the classic renderer.

       The list is rebuilt from a full description every time rather than
       diffed: a location rarely has more than a handful of actions, and the
       engine hands over the whole set anyway. What is not rebuilt is the
       document, which is loaded once and never navigated - see QSPWebPane. */
    class QSPWebListBox : public QSPWebPane
    {
        DECLARE_CLASS(QSPWebListBox)
    public:
        // C-tors / D-tor
        QSPWebListBox(wxWindow *parent, wxWindowID id, ListBoxType type = LB_NORMAL);

        // Methods
        void RefreshUI();
        /* The engine rebuilds the list in one pass, so nothing is sent until
           EndItems - and nothing at all if the contents did not change, which
           is the common case: games call SHOWACTS constantly. */
        void BeginItems();
        void AddItem(const wxString& image, const wxString& desc);
        void EndItems();

        // Accessors
        /* Game-supplied CSS reaches the lists too, so one stylesheet themes the
           whole player. Only the stylesheets: a game's scripts run in the
           description panes, where `window.qsp` is, and running them again in
           four more documents would mean four more copies of whatever state
           they set up. */
        void SetUserStyles(const wxString& inlineCss, const wxArrayString& files);

        void SetIsHtml(bool isHtml);
        void SetToShowNums(bool toShow);
        void SetTextFont(const wxFont& font);
        wxFont GetTextFont() const { return m_font; }
        void SetLinkColor(const wxColour& clr);
        const wxColour& GetLinkColor() const { return m_linkColor; }
        /* wxNOT_FOUND when nothing is selected, matching wxHtmlListBox */
        int GetSelection() const { return m_selection; }
        void SetSelection(int selection);

        // Overloaded methods
        virtual bool SetBackgroundColour(const wxColour& colour);
        virtual bool SetForegroundColour(const wxColour& colour);

    protected:
        static wxString BuildShellDocument();
        wxString BuildItemsScript() const;
        wxString BuildStyleScript() const;
        wxString BuildUserStylesScript() const;

        // Overridden from QSPWebPane
        virtual void OnShellReady();
        virtual void OnPaneMessage(const wxString& message);

        /* Raised on the pane and left to propagate, the way the classic list
           box's own events do */
        void SendListEvent(wxEventType type, int index);

        // Fields
        ListBoxType m_type;
        bool m_toUseHtml;
        bool m_toShowNums;
        int m_selection;
        wxFont m_font;
        wxColour m_linkColor;
        wxColour m_backColor;
        wxColour m_fontColor;
        wxArrayString m_images;
        wxArrayString m_descs;
        wxArrayString m_newImages;
        wxArrayString m_newDescs;
        wxString m_userCss;
        wxArrayString m_userCssFiles;
    };

#endif
