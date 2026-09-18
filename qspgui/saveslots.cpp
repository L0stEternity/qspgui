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

#include "saveslots.h"

#include <wx/fileconf.h>
#include <wx/filename.h>

void QSPSaveSlots::SetGameFile(const wxString &gameFilePath)
{
    m_gameFilePath = gameFilePath;
}

/* Beside the game file and named after it, the way the quick slot and the
   game's own config already are: one set of slots per game, and uninstalling
   a game takes its saves with it. */
wxString QSPSaveSlots::GetSlotPath(int slot) const
{
    if (m_gameFilePath.IsEmpty() || !IsValidSlot(slot)) return wxEmptyString;

    wxFileName path(m_gameFilePath);
    path.SetName(wxString::Format(wxT("%s_slot%d"), path.GetName(), slot));
    path.SetExt(wxT("sav"));
    return path.GetFullPath();
}

wxString QSPSaveSlots::GetSidecarPath() const
{
    if (m_gameFilePath.IsEmpty()) return wxEmptyString;

    wxFileName path(m_gameFilePath);
    path.SetName(path.GetName() + wxT("_slots"));
    path.SetExt(wxT("cfg"));
    return path.GetFullPath();
}

QSPSaveSlots::Info QSPSaveSlots::GetInfo(int slot) const
{
    Info info;
    info.slot = slot;
    if (!IsValidSlot(slot)) return info;

    wxString slotPath(GetSlotPath(slot));
    /* The save file decides whether the slot is used. A sidecar entry for a
       file somebody deleted describes nothing. */
    if (slotPath.IsEmpty() || !wxFileExists(slotPath)) return info;
    info.isUsed = true;

    wxString sidecarPath(GetSidecarPath());
    if (sidecarPath.IsEmpty() || !wxFileExists(sidecarPath)) return info;

    wxFileConfig cfg(wxEmptyString, wxEmptyString, sidecarPath);
    wxString key(wxString::Format(wxT("Slot%d/"), slot));

    info.location = cfg.Read(key + wxT("Location"), wxEmptyString);

    /* Seconds since the epoch rather than formatted text: a date written in
       one locale and read back in another is how a save ends up claiming to be
       from the future. */
    wxLongLong_t stamp = 0;
    if (cfg.Read(key + wxT("Time"), &stamp) && stamp > 0)
        info.time = wxDateTime((time_t)stamp);

    return info;
}

void QSPSaveSlots::Remember(int slot, const wxString &location)
{
    wxString sidecarPath(GetSidecarPath());
    if (sidecarPath.IsEmpty() || !IsValidSlot(slot)) return;

    wxFileConfig cfg(wxEmptyString, wxEmptyString, sidecarPath);
    wxString key(wxString::Format(wxT("Slot%d/"), slot));
    cfg.Write(key + wxT("Location"), location);
    cfg.Write(key + wxT("Time"), (wxLongLong_t)wxDateTime::Now().GetTicks());
    cfg.Flush();
}

void QSPSaveSlots::Forget(int slot)
{
    wxString sidecarPath(GetSidecarPath());
    if (sidecarPath.IsEmpty() || !IsValidSlot(slot) || !wxFileExists(sidecarPath)) return;

    wxFileConfig cfg(wxEmptyString, wxEmptyString, sidecarPath);
    cfg.DeleteGroup(wxString::Format(wxT("Slot%d"), slot));
    cfg.Flush();
}

/* Built fresh every time the menu opens, so a slot written by another copy of
   the player - or deleted from the file manager - reads correctly. */
wxString QSPSaveSlots::Describe(int slot) const
{
    Info info(GetInfo(slot));
    if (!info.isUsed)
        return wxString::Format(_("&%d: empty"), slot);

    wxString detail;
    if (!info.location.IsEmpty()) detail = info.location;
    if (info.time.IsValid())
    {
        /* ISO-ish and unambiguous; the slot list is scanned, not read */
        wxString when(info.time.Format(wxT("%Y-%m-%d %H:%M")));
        detail = (detail.IsEmpty() ? when : detail + wxT(" - ") + when);
    }
    if (detail.IsEmpty()) detail = _("saved");

    return wxString::Format(wxT("&%d: %s"), slot, detail);
}
