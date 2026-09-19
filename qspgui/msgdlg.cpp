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

#include "msgdlg.h"
#include "comtools.h"

#include <wx/clipbrd.h>

wxIMPLEMENT_CLASS(QSPMsgDlg, wxDialog);

BEGIN_EVENT_TABLE(QSPMsgDlg, wxDialog)
    EVT_HTML_LINK_CLICKED(ID_MSG_DESC, QSPMsgDlg::OnLinkClicked)
    EVT_BUTTON(ID_MSG_COPY, QSPMsgDlg::OnCopy)
    EVT_INIT_DIALOG(QSPMsgDlg::OnInitDialog)
END_EVENT_TABLE()

QSPMsgDlg::QSPMsgDlg(wxWindow* parent,
                     wxWindowID id,
                     const wxColour& backColor,
                     const wxColour& fontColor,
                     const wxFont& font,
                     const wxString& caption,
                     const wxString& text,
                     bool isHtml,
                     PathProvider *pathProvider)
{
    if (!Create(parent, id, caption, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)) return;
    // ----------
    SetBackgroundColour(backColor);
    wxSizer *sizerUp = new wxBoxSizer(wxVERTICAL);
    m_desc = new QSPTextBox(this, ID_MSG_DESC);
    m_desc->SetPathProvider(pathProvider);
    m_desc->SetIsHtml(isHtml);
    m_desc->SetBackgroundColour(backColor);
    m_desc->SetForegroundColour(fontColor);
    m_desc->SetTextFont(font);
    m_desc->SetText(text);
    wxStaticLine* line = new wxStaticLine(this, wxID_STATIC, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    sizerUp->Add(m_desc, 1, wxALL | wxGROW, 2);
    sizerUp->Add(line, 0, wxALL | wxGROW, 2);
    // ----------
    /* Copy on the left, away from OK on the right: the two do unrelated
       things, and a reader dismissing the dialog should not land on the one
       that does not dismiss it. */
    wxSizer *sizerBottom = new wxBoxSizer(wxHORIZONTAL);
    /* Built now and hidden, so a caller that has something worth copying only
       has to hand the text over; the layout is already sized for it. */
    m_btnCopy = new wxButton(this, ID_MSG_COPY, _("&Copy details"));
    m_btnCopy->SetFont(font);
    m_btnCopy->SetBackgroundColour(backColor);
    m_btnCopy->SetForegroundColour(fontColor);
    m_btnCopy->Hide();
    sizerBottom->Add(m_btnCopy, 0, wxALL, 8);
    sizerBottom->AddStretchSpacer(1);
    wxButton *btnOk = new wxButton(this, wxID_OK, _("OK"));
    btnOk->SetDefault();
    btnOk->SetFont(font);
    btnOk->SetBackgroundColour(backColor);
    btnOk->SetForegroundColour(fontColor);
    sizerBottom->Add(btnOk, 0, wxALL, 8);
    // ----------
    wxSizer *sizerMain = new wxBoxSizer(wxVERTICAL);
    sizerMain->Add(sizerUp, 1, wxGROW, 0);
    sizerMain->Add(sizerBottom, 0, wxGROW, 0);
    // ----------
    sizerMain->SetMinSize(MinWidth, MinHeight);
    SetSizerAndFit(sizerMain);
    btnOk->SetFocus();
}

void QSPMsgDlg::OnInitDialog(wxInitDialogEvent& WXUNUSED(event))
{
    int deltaH = GetClientSize().GetHeight() - m_desc->GetSize().GetHeight();
    int deltaW = GetClientSize().GetWidth() - m_desc->GetSize().GetWidth();
    int height = m_desc->GetInternalRepresentation()->GetHeight() + m_desc->GetCharHeight() + deltaH;
    int width = m_desc->GetInternalRepresentation()->GetWidth() + deltaW;
    height = wxMin(wxMax(height, MinHeight), MaxHeight);
    width = wxMin(wxMax(width, MinWidth), MaxWidth);
    SetClientSize(width, height);
    Center();
}

void QSPMsgDlg::SetCopyText(const wxString& text)
{
    m_copyText = text;
    m_btnCopy->Show(!text.IsEmpty());
    Layout();
}

void QSPMsgDlg::OnCopy(wxCommandEvent& WXUNUSED(event))
{
    if (m_copyText.IsEmpty()) return;

    /* wxClipboardLocker rather than Open/Close by hand: another program can be
       holding the clipboard, and then this must simply do nothing rather than
       leave it open behind a dialog the reader is about to dismiss. */
    wxClipboardLocker locker;
    if (!locker) return;

    wxTheClipboard->SetData(new wxTextDataObject(m_copyText));
    wxTheClipboard->Flush();
    /* Said on the button itself: a dialog that answers a dialog to report that
       a copy worked is one dialog too many. */
    m_btnCopy->SetLabel(_("Copied"));
    m_btnCopy->Disable();
}

void QSPMsgDlg::OnLinkClicked(wxHtmlLinkEvent& event)
{
    wxString href;
    wxHtmlLinkInfo info(event.GetLinkInfo());
    if (info.GetEvent()->LeftUp())
    {
        href = info.GetHref();
        if (href[0] == wxT('#'))
            m_desc->LoadPage(href);
        else
            QSPTools::LaunchDefaultBrowser(href);
    }
    else
        event.Skip();
}
