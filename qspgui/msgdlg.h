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

#ifndef MSGDLG_H
    #define MSGDLG_H

    #include <wx/wx.h>
    #include <wx/statline.h>
    #include "textbox.h"
    #include "pathprovider.h"

    enum
    {
        ID_MSG_DESC,
        ID_MSG_COPY
    };

    class QSPMsgDlg : public wxDialog
    {
        const int MinWidth = 450;
        const int MaxWidth = 550;
        const int MinHeight = 100;
        const int MaxHeight = 350;

        DECLARE_CLASS(QSPMsgDlg)
        DECLARE_EVENT_TABLE()
    public:
        // C-tors / D-tor
        QSPMsgDlg(wxWindow* parent,
                  wxWindowID id,
                  const wxColour& backColor,
                  const wxColour& fontColor,
                  const wxFont& font,
                  const wxString& caption,
                  const wxString& text,
                  bool isHtml,
                  PathProvider *pathProvider);

        /* Offers a button that puts this text on the clipboard. What the
           dialog shows is written for whoever is reading the game; this is the
           same thing written for whoever has to fix it, and it is only worth a
           button when there is something worth pasting. Empty by default, and
           the button only appears once it is set. */
        void SetCopyText(const wxString& text);
    protected:
        // Events
        void OnInitDialog(wxInitDialogEvent& event);
        void OnLinkClicked(wxHtmlLinkEvent& event);
        void OnCopy(wxCommandEvent& event);

        // Fields
        QSPTextBox *m_desc;
        wxButton *m_btnCopy;
        wxString m_copyText;
    };

#endif
