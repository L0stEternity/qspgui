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

/* Numbered slots. The save file is the truth and the sidecar is advisory, so
   most of what matters here is what happens when the two disagree - which they
   will, because a player can delete a .sav from the file manager. */

#include "testing.h"
#include "../qspgui/saveslots.h"
#include "../qspgui/comtools.h"

#include <wx/filename.h>
#include <wx/dir.h>

namespace
{
    /* A scratch game folder that cleans itself up, so the cases can write
       real files rather than pretending to. */
    class TempGame
    {
    public:
        TempGame()
        {
            wxFileName dir(wxFileName::GetTempDir(), wxEmptyString);
            dir.AppendDir(wxString::Format(wxT("qspgui_slots_%ld"), (long)wxGetProcessId()));
            m_dir = dir.GetPathWithSep();
            wxFileName::Mkdir(m_dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
            m_gameFile = m_dir + wxT("adventure.qsp");
        }

        ~TempGame()
        {
            wxArrayString files;
            wxDir::GetAllFiles(m_dir, &files);
            for (size_t i = 0; i < files.GetCount(); ++i)
                wxRemoveFile(files[i]);
            wxFileName::Rmdir(m_dir);
        }

        const wxString &GetGameFile() const { return m_gameFile; }

        void WriteSave(const wxString &path)
        {
            const char bytes[] = "not a real save, but a real file";
            QSPFileIO::Write(path, bytes, sizeof(bytes) - 1);
        }

    private:
        wxString m_dir;
        wxString m_gameFile;
    };
}

QSP_TEST(slot_paths_sit_beside_the_game_and_are_distinct)
{
    QSPSaveSlots slots;
    slots.SetGameFile(wxT("C:\\games\\adventure.qsp"));

    wxString first(slots.GetSlotPath(1));
    wxString second(slots.GetSlotPath(2));

    QSP_CHECK(!first.IsEmpty());
    QSP_CHECK(first != second);
    QSP_CHECK(first.EndsWith(wxT(".sav")));
    QSP_CHECK(first.Contains(wxT("adventure")));
    /* Beside the game file, not in some shared folder: uninstalling a game
       takes its saves with it. */
    QSP_CHECK_STR(wxFileName(first).GetPath(), wxFileName(wxT("C:\\games\\adventure.qsp")).GetPath());
}

QSP_TEST(slot_numbers_outside_the_range_have_no_path)
{
    QSPSaveSlots slots;
    slots.SetGameFile(wxT("C:\\games\\adventure.qsp"));

    QSP_CHECK_STR(slots.GetSlotPath(0), wxT(""));
    QSP_CHECK_STR(slots.GetSlotPath(-1), wxT(""));
    QSP_CHECK_STR(slots.GetSlotPath(QSPSaveSlots::Count + 1), wxT(""));
    QSP_CHECK(!slots.GetSlotPath(QSPSaveSlots::Count).IsEmpty());
}

QSP_TEST(no_game_means_no_slots)
{
    QSPSaveSlots slots;
    QSP_CHECK_STR(slots.GetSlotPath(1), wxT(""));
    QSP_CHECK_BOOL(slots.GetInfo(1).isUsed, false);
    /* Remembering against no game must not write anything anywhere */
    slots.Remember(1, wxT("room"));
    QSP_CHECK_BOOL(slots.GetInfo(1).isUsed, false);
}

QSP_TEST(an_unwritten_slot_reads_as_empty)
{
    TempGame game;
    QSPSaveSlots slots;
    slots.SetGameFile(game.GetGameFile());

    QSPSaveSlots::Info info(slots.GetInfo(3));
    QSP_CHECK_BOOL(info.isUsed, false);
    QSP_CHECK_STR(info.location, wxT(""));
    QSP_CHECK(slots.Describe(3).Contains(wxT("3")));
}

QSP_TEST(a_written_slot_reports_its_location_and_time)
{
    TempGame game;
    QSPSaveSlots slots;
    slots.SetGameFile(game.GetGameFile());

    game.WriteSave(slots.GetSlotPath(2));
    slots.Remember(2, wxT("forest clearing"));

    QSPSaveSlots::Info info(slots.GetInfo(2));
    QSP_CHECK_BOOL(info.isUsed, true);
    QSP_CHECK_STR(info.location, wxT("forest clearing"));
    QSP_CHECK_BOOL(info.time.IsValid(), true);
    QSP_CHECK(slots.Describe(2).Contains(wxT("forest clearing")));
}

QSP_TEST(the_save_file_decides_whether_a_slot_is_used)
{
    /* The sidecar is advisory. A slot whose .sav was deleted from the file
       manager is empty, whatever the sidecar still says about it - otherwise
       the menu offers a load that cannot work. */
    TempGame game;
    QSPSaveSlots slots;
    slots.SetGameFile(game.GetGameFile());

    game.WriteSave(slots.GetSlotPath(4));
    slots.Remember(4, wxT("cellar"));
    QSP_CHECK_BOOL(slots.GetInfo(4).isUsed, true);

    wxRemoveFile(slots.GetSlotPath(4));
    QSP_CHECK_BOOL(slots.GetInfo(4).isUsed, false);
}

QSP_TEST(a_save_with_no_sidecar_entry_still_counts)
{
    /* The other direction: a .sav dropped in by hand, or written by a build of
       the player that predates the sidecar, is a usable slot with nothing to
       say for itself. */
    TempGame game;
    QSPSaveSlots slots;
    slots.SetGameFile(game.GetGameFile());

    game.WriteSave(slots.GetSlotPath(5));

    QSPSaveSlots::Info info(slots.GetInfo(5));
    QSP_CHECK_BOOL(info.isUsed, true);
    QSP_CHECK_STR(info.location, wxT(""));
    /* And the label still has to say something */
    QSP_CHECK(!slots.Describe(5).IsEmpty());
}

QSP_TEST(slots_do_not_leak_into_each_other)
{
    TempGame game;
    QSPSaveSlots slots;
    slots.SetGameFile(game.GetGameFile());

    game.WriteSave(slots.GetSlotPath(1));
    slots.Remember(1, wxT("first"));
    game.WriteSave(slots.GetSlotPath(2));
    slots.Remember(2, wxT("second"));

    QSP_CHECK_STR(slots.GetInfo(1).location, wxT("first"));
    QSP_CHECK_STR(slots.GetInfo(2).location, wxT("second"));

    slots.Forget(1);
    QSP_CHECK_STR(slots.GetInfo(1).location, wxT(""));
    QSP_CHECK_STR(slots.GetInfo(2).location, wxT("second"));
    /* Forgetting the description does not delete the save */
    QSP_CHECK_BOOL(slots.GetInfo(1).isUsed, true);
}

QSP_TEST(rewriting_a_slot_replaces_its_description)
{
    TempGame game;
    QSPSaveSlots slots;
    slots.SetGameFile(game.GetGameFile());

    game.WriteSave(slots.GetSlotPath(6));
    slots.Remember(6, wxT("before"));
    slots.Remember(6, wxT("after"));

    QSP_CHECK_STR(slots.GetInfo(6).location, wxT("after"));
}
