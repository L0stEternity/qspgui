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

#ifndef SAVESLOTSDLG_H
    #define SAVESLOTSDLG_H

    #include <wx/wx.h>
    #include <wx/listctrl.h>
    #include "saveslots.h"

    enum
    {
        ID_SLOTS_LIST = 2100,
        ID_SLOTS_SAVE,
        ID_SLOTS_LOAD,
        ID_SLOTS_DELETE
    };

    /* Every slot in one list, with what is in it and the three things that can
       be done to it. The two submenus this replaces could only ever show nine
       labels and act on one of them; deleting a slot meant finding the .sav in
       a file manager.

       Deleting is done here, because it is nothing but files. Saving and
       loading are not: both drive the engine, which is the frame's job, so the
       dialog closes and reports which one the player asked for. */
    class QSPSaveSlotsDlg : public wxDialog
    {
        const int MinWidth = 520;
        const int MinHeight = 320;

        DECLARE_CLASS(QSPSaveSlotsDlg)
        DECLARE_EVENT_TABLE()
    public:
        enum Action
        {
            ACTION_NONE,
            ACTION_SAVE,
            ACTION_LOAD
        };

        /* canSave is the game's answer, not the slots': $NOSAVE switches
           saving off while leaving what is already in the slots loadable. */
        QSPSaveSlotsDlg(wxWindow *parent,
                        QSPSaveSlots *slots,
                        bool canSave,
                        const wxColour &backColor,
                        const wxColour &fontColor,
                        const wxFont &font);

        /* What the player asked for, once the dialog has closed */
        Action GetAction() const { return m_action; }
        /* 1-based, as the list shows it; 0 when nothing was asked for */
        int GetSlot() const { return m_slot; }

    protected:
        // Internal methods
        /* Read off the files every time, so a slot written by a second copy of
           the player - or deleted from a file manager - shows up correctly. */
        void Populate();
        int GetSelectedSlot() const;
        void Select(int slot);
        void UpdateButtons();
        /* Closes the dialog with the action for the frame to carry out */
        void Finish(Action action, int slot);

        // Events
        void OnSave(wxCommandEvent &event);
        void OnLoad(wxCommandEvent &event);
        void OnDelete(wxCommandEvent &event);
        void OnSelectionChange(wxListEvent &event);
        /* Double-click or Enter on a row: load it, or save into it if empty */
        void OnActivate(wxListEvent &event);
        /* 1 to 9 pick a slot wherever the focus is */
        void OnChar(wxKeyEvent &event);

        // Fields
        QSPSaveSlots *m_slots;
        bool m_canSave;
        wxListCtrl *m_list;
        wxButton *m_btnSave;
        wxButton *m_btnLoad;
        wxButton *m_btnDelete;
        Action m_action;
        int m_slot;
    };

#endif
