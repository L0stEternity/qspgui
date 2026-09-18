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

#ifndef SAVESLOTS_H
    #define SAVESLOTS_H

    #include <wx/wx.h>
    #include <wx/datetime.h>

    /* Numbered save slots for the game that is open.

       A .sav on its own says nothing about itself: the engine's format has no
       header a player can read without loading it, and loading it is the one
       thing a "which save is this?" question must not do. So each slot carries
       a line in a sidecar written beside the game, and the menu is built from
       that rather than from the saves.

       The sidecar is advisory throughout. The save file is the truth: a slot
       with no file is empty however the sidecar describes it, and a slot whose
       file was put there by hand still loads, just with nothing to show for
       itself in the menu. */
    class QSPSaveSlots
    {
    public:
        /* Nine, because that is how many fit on the number row and in a menu
           without it needing a scrollbar. */
        static constexpr int Count = 9;

        struct Info
        {
            Info() : slot(0), isUsed(false) {}

            int slot;          /* 1-based, as the menu shows it */
            bool isUsed;       /* a save file is actually there */
            wxDateTime time;   /* when it was written; invalid if unknown */
            wxString location; /* $CURLOC at the time; empty if unknown */
        };

        /* Empty path means no game is open and every slot reads as unused */
        void SetGameFile(const wxString &gameFilePath);

        /* Both answer an empty string when no game is open or the slot number
           is out of range, which is the only "no" the callers need. */
        wxString GetSlotPath(int slot) const;
        Info GetInfo(int slot) const;

        /* Called after a save has actually been written */
        void Remember(int slot, const wxString &location);
        void Forget(int slot);

        /* "3: forest clearing - 2026-09-18 14:02", or "3: empty" */
        wxString Describe(int slot) const;

    private:
        wxString GetSidecarPath() const;
        static bool IsValidSlot(int slot) { return slot >= 1 && slot <= Count; }

        wxString m_gameFilePath;
    };

#endif
