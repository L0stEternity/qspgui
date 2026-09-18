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

/* Containment. A game names its own media, saves and included worlds, so every
   one of those paths is written by the game rather than by the player - and
   "../../.." is a perfectly ordinary thing for a path to contain. These are the
   checks that keep a game inside its folder. */

#include "testing.h"
#include "../qspgui/comtools.h"

#include <wx/filename.h>

namespace
{
    /* The player stores the game folder absolute and separator-terminated, so
       the tests have to hand over the same shape. Built rather than written
       out, because the tests run on three platforms. */
    wxString BaseDir(const wxString &leaf = wxT("game"))
    {
        wxFileName dir(wxFileName::GetTempDir(), wxEmptyString);
        dir.AppendDir(wxT("qspgui_test"));
        dir.AppendDir(leaf);
        return dir.GetPathWithSep();
    }
}

QSP_TEST(compose_resolves_an_ordinary_relative_path)
{
    wxString base(BaseDir());
    wxString composed(QSPPaths::ComposeContained(base, wxT("images/room.png")));

    QSP_CHECK(!composed.IsEmpty());
    QSP_CHECK(composed.StartsWith(base));
    QSP_CHECK(composed.EndsWith(wxT("room.png")));
}

QSP_TEST(compose_understands_dos_separators_everywhere)
{
    /* A game written on Windows uses backslashes and is expected to run on
       Linux and macOS too, so they have to be separators rather than ordinary
       characters in a file name. */
    wxString base(BaseDir());
    wxString composed(QSPPaths::ComposeContained(base, wxT("images\\room.png")));

    QSP_CHECK(!composed.IsEmpty());
    QSP_CHECK(composed.StartsWith(base));
    QSP_CHECK(composed.EndsWith(wxT("room.png")));
}

QSP_TEST(compose_refuses_to_climb_out_of_the_game_folder)
{
    wxString base(BaseDir());

    QSP_CHECK_STR(QSPPaths::ComposeContained(base, wxT("../secret.txt")), wxT(""));
    QSP_CHECK_STR(QSPPaths::ComposeContained(base, wxT("..\\secret.txt")), wxT(""));
    QSP_CHECK_STR(QSPPaths::ComposeContained(base, wxT("../../../../../../etc/passwd")), wxT(""));
    QSP_CHECK_STR(QSPPaths::ComposeContained(base, wxT("images/../../secret.txt")), wxT(""));
}

QSP_TEST(compose_allows_a_climb_that_stays_inside)
{
    /* Going up and back down within the folder is legitimate - a game's own
       "../shared/ui.png" from a subfolder - and only the resolved path can
       tell that apart from an escape. */
    wxString base(BaseDir());
    wxString composed(QSPPaths::ComposeContained(base, wxT("images/../sounds/step.wav")));

    QSP_CHECK(!composed.IsEmpty());
    QSP_CHECK(composed.StartsWith(base));
    QSP_CHECK(composed.EndsWith(wxT("step.wav")));
}

QSP_TEST(compose_answers_empty_for_empty_input)
{
    QSP_CHECK_STR(QSPPaths::ComposeContained(BaseDir(), wxT("")), wxT(""));
    /* No game open means no folder to be contained by */
    QSP_CHECK_STR(QSPPaths::ComposeContained(wxT(""), wxT("room.png")), wxT(""));
}

QSP_TEST(contained_accepts_a_path_inside_the_folder)
{
    wxString base(BaseDir());
    QSP_CHECK_BOOL(QSPPaths::IsContained(base, base + wxT("save.sav")), true);
    QSP_CHECK_BOOL(QSPPaths::IsContained(base, base + wxT("saves/slot1.sav")), true);
}

QSP_TEST(contained_rejects_a_path_outside_it)
{
    wxString base(BaseDir());
    /* A sibling folder whose name merely starts with the same characters is
       the case a plain prefix test gets wrong; the separator the base carries
       is what keeps it honest. */
    QSP_CHECK_BOOL(QSPPaths::IsContained(base, BaseDir(wxT("game2")) + wxT("save.sav")), false);
    QSP_CHECK_BOOL(QSPPaths::IsContained(base, BaseDir(wxT("other")) + wxT("save.sav")), false);
    QSP_CHECK_BOOL(QSPPaths::IsContained(base, base + wxT("..") + wxFileName::GetPathSeparator() + wxT("save.sav")), false);
}

QSP_TEST(contained_treats_empty_as_a_request_for_a_dialog)
{
    /* The engine passes no path when it wants the player to pick a file, so an
       empty one is not an escape attempt. */
    QSP_CHECK_BOOL(QSPPaths::IsContained(BaseDir(), wxT("")), true);
    /* But with no game folder, an actual path has nothing to be inside of */
    QSP_CHECK_BOOL(QSPPaths::IsContained(wxT(""), wxT("/etc/passwd")), false);
}
