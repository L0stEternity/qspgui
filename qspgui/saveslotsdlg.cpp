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

#include "saveslotsdlg.h"

wxIMPLEMENT_CLASS(QSPSaveSlotsDlg, wxDialog);

BEGIN_EVENT_TABLE(QSPSaveSlotsDlg, wxDialog)
    EVT_BUTTON(ID_SLOTS_SAVE, QSPSaveSlotsDlg::OnSave)
    EVT_BUTTON(ID_SLOTS_LOAD, QSPSaveSlotsDlg::OnLoad)
    EVT_BUTTON(ID_SLOTS_DELETE, QSPSaveSlotsDlg::OnDelete)
    EVT_LIST_ITEM_SELECTED(ID_SLOTS_LIST, QSPSaveSlotsDlg::OnSelectionChange)
    EVT_LIST_ITEM_DESELECTED(ID_SLOTS_LIST, QSPSaveSlotsDlg::OnSelectionChange)
    EVT_LIST_ITEM_ACTIVATED(ID_SLOTS_LIST, QSPSaveSlotsDlg::OnActivate)
    /* CHAR_HOOK rather than the list's own key event: the digits have to work
       while a button has the focus too. */
    EVT_CHAR_HOOK(QSPSaveSlotsDlg::OnChar)
END_EVENT_TABLE()

QSPSaveSlotsDlg::QSPSaveSlotsDlg(wxWindow *parent,
                                 QSPSaveSlots *slots,
                                 bool canSave,
                                 const wxColour &backColor,
                                 const wxColour &fontColor,
                                 const wxFont &font)
    : m_slots(slots), m_canSave(canSave), m_action(ACTION_NONE), m_slot(0)
{
    if (!Create(parent, wxID_ANY, _("Save slots"), wxDefaultPosition, wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)) return;
    // ----------
    SetBackgroundColour(backColor);
    SetForegroundColour(fontColor);

    m_list = new wxListCtrl(this, ID_SLOTS_LIST, wxDefaultPosition, wxDefaultSize,
                            wxLC_REPORT | wxLC_SINGLE_SEL);
    m_list->SetBackgroundColour(backColor);
    m_list->SetTextColour(fontColor);
    m_list->SetFont(font);
    m_list->InsertColumn(0, _("Slot"), wxLIST_FORMAT_LEFT, 50);
    m_list->InsertColumn(1, _("Contents"), wxLIST_FORMAT_LEFT, 280);
    m_list->InsertColumn(2, _("Saved"), wxLIST_FORMAT_LEFT, 150);
    // ----------
    /* Save, load and delete act on the row above them; Close is set apart on
       the right, the way OK is everywhere else in the player. */
    wxSizer *sizerButtons = new wxBoxSizer(wxHORIZONTAL);
    m_btnSave = new wxButton(this, ID_SLOTS_SAVE, _("&Save"));
    m_btnLoad = new wxButton(this, ID_SLOTS_LOAD, _("&Load"));
    m_btnDelete = new wxButton(this, ID_SLOTS_DELETE, _("&Delete"));
    wxButton *btnClose = new wxButton(this, wxID_CANCEL, _("Close"));
    wxButton *buttons[] = { m_btnSave, m_btnLoad, m_btnDelete, btnClose };
    for (size_t i = 0; i < WXSIZEOF(buttons); ++i)
    {
        buttons[i]->SetFont(font);
        buttons[i]->SetBackgroundColour(backColor);
        buttons[i]->SetForegroundColour(fontColor);
    }
    sizerButtons->Add(m_btnSave, 0, wxALL, 5);
    sizerButtons->Add(m_btnLoad, 0, wxALL, 5);
    sizerButtons->Add(m_btnDelete, 0, wxALL, 5);
    sizerButtons->AddStretchSpacer(1);
    sizerButtons->Add(btnClose, 0, wxALL, 5);
    // ----------
    wxSizer *sizerMain = new wxBoxSizer(wxVERTICAL);
    sizerMain->Add(m_list, 1, wxALL | wxGROW, 5);
    sizerMain->Add(sizerButtons, 0, wxGROW, 0);
    sizerMain->SetMinSize(MinWidth, MinHeight);
    SetSizerAndFit(sizerMain);
    SetMinClientSize(wxSize(MinWidth, MinHeight));
    // ----------
    Populate();
    /* The first slot, so every key and button has something to act on the
       moment the dialog opens */
    Select(1);
    m_list->SetFocus();
    Center();
}

void QSPSaveSlotsDlg::Populate()
{
    /* Rebuilt whole rather than patched: it is nine rows, and the alternative
       is keeping a second copy of what the files already say. */
    m_list->DeleteAllItems();
    for (int slot = 1; slot <= QSPSaveSlots::Count; ++slot)
    {
        QSPSaveSlots::Info info(m_slots->GetInfo(slot));
        long row = m_list->InsertItem(slot - 1, wxString::Format(wxT("%d"), slot));
        if (row == -1) continue;

        wxString contents;
        wxString when;
        if (!info.isUsed)
            contents = _("empty");
        else
        {
            /* A save put there by hand has no sidecar line, so it can only be
               reported as being there at all. */
            contents = (info.location.IsEmpty() ? _("saved") : info.location);
            if (info.time.IsValid())
                when = info.time.Format(wxT("%Y-%m-%d %H:%M"));
        }
        m_list->SetItem(row, 1, contents);
        m_list->SetItem(row, 2, when);
    }
}

int QSPSaveSlotsDlg::GetSelectedSlot() const
{
    long row = m_list->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (row == -1) return 0;
    return (int)row + 1;
}

void QSPSaveSlotsDlg::Select(int slot)
{
    if (slot < 1 || slot > QSPSaveSlots::Count) return;
    long row = slot - 1;
    m_list->SetItemState(row, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                         wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
    m_list->EnsureVisible(row);
    UpdateButtons();
}

void QSPSaveSlotsDlg::UpdateButtons()
{
    int slot = GetSelectedSlot();
    /* An empty slot is a perfectly good place to save to and nothing to load
       or delete, so only saving survives it. */
    bool isUsed = (slot > 0 && m_slots->GetInfo(slot).isUsed);
    m_btnSave->Enable(slot > 0 && m_canSave);
    m_btnLoad->Enable(isUsed);
    m_btnDelete->Enable(isUsed);
}

void QSPSaveSlotsDlg::Finish(Action action, int slot)
{
    m_action = action;
    m_slot = slot;
    EndModal(wxID_OK);
}

void QSPSaveSlotsDlg::OnSave(wxCommandEvent &WXUNUSED(event))
{
    int slot = GetSelectedSlot();
    if (slot < 1 || !m_canSave) return;

    if (m_slots->GetInfo(slot).isUsed)
    {
        /* The one place in the dialog where a wrong click cannot be undone
           without asking first */
        wxString question(wxString::Format(_("Slot %d already holds a save. Overwrite it?"), slot));
        if (wxMessageBox(question, _("Save slots"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES)
            return;
    }
    Finish(ACTION_SAVE, slot);
}

void QSPSaveSlotsDlg::OnLoad(wxCommandEvent &WXUNUSED(event))
{
    int slot = GetSelectedSlot();
    if (slot < 1 || !m_slots->GetInfo(slot).isUsed) return;
    Finish(ACTION_LOAD, slot);
}

void QSPSaveSlotsDlg::OnDelete(wxCommandEvent &WXUNUSED(event))
{
    int slot = GetSelectedSlot();
    if (slot < 1 || !m_slots->GetInfo(slot).isUsed) return;

    wxString question(wxString::Format(_("Delete the save in slot %d? This can't be undone."), slot));
    if (wxMessageBox(question, _("Save slots"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this) != wxYES)
        return;

    if (!m_slots->Delete(slot))
        wxMessageBox(wxString::Format(_("Couldn't delete slot %d"), slot),
                     _("Save slots"), wxOK | wxICON_ERROR, this);
    /* Either way the list is read off the files again: a failed delete has to
       show the save still sitting there. */
    Populate();
    Select(slot);
}

void QSPSaveSlotsDlg::OnSelectionChange(wxListEvent &event)
{
    event.Skip();
    UpdateButtons();
}

void QSPSaveSlotsDlg::OnActivate(wxListEvent &event)
{
    int slot = (int)event.GetIndex() + 1;
    if (m_slots->GetInfo(slot).isUsed)
        Finish(ACTION_LOAD, slot);
    else if (m_canSave)
        Finish(ACTION_SAVE, slot);
}

void QSPSaveSlotsDlg::OnChar(wxKeyEvent &event)
{
    int key = event.GetKeyCode();
    if (key >= '1' && key <= '9' && (key - '0') <= QSPSaveSlots::Count && !event.HasAnyModifiers())
    {
        Select(key - '0');
        m_list->SetFocus();
        return;
    }
    event.Skip();
}
