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

/* QSPMsgOptions reads the <!--modal ...--> directive a game puts in front of
   a MSG text. The directive has to be invisible to other players, so the
   cases here are as much about what must be left alone - an ordinary comment,
   an unclosed one - as about what gets read. */

#include "testing.h"
#include "../qspgui/comtools.h"

QSP_TEST(msgoptions_leaves_plain_text_alone)
{
    wxString text(wxT("Hello <!--modal w=10-->"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), false);
    QSP_CHECK_STR(text, wxT("Hello <!--modal w=10-->"));
}

QSP_TEST(msgoptions_reads_sizes_title_and_ok)
{
    wxString text(wxT("<!--modal w=720 h=60% title=\"The Journal\" ok='Close it'-->Body"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), true);
    QSP_CHECK_STR(text, wxT("Body"));
    QSP_CHECK_INT(options.Width, 720);
    QSP_CHECK_BOOL(options.IsWidthPercent, false);
    QSP_CHECK_INT(options.Height, 60);
    QSP_CHECK_BOOL(options.IsHeightPercent, true);
    QSP_CHECK_STR(options.Title, wxT("The Journal"));
    QSP_CHECK_STR(options.OkLabel, wxT("Close it"));
}

QSP_TEST(msgoptions_is_case_insensitive_and_skips_leading_space)
{
    wxString text(wxT("  \r\n<!--MODAL Width = 50% TITLE=Log-->\r\nBody"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), true);
    QSP_CHECK_STR(text, wxT("Body"));
    QSP_CHECK_INT(options.Width, 50);
    QSP_CHECK_BOOL(options.IsWidthPercent, true);
    QSP_CHECK_STR(options.Title, wxT("Log"));
}

QSP_TEST(msgoptions_directive_alone_is_enough)
{
    /* A bare directive still strips, so a game that only wants the caption
       can have it without any size */
    wxString text(wxT("<!--modal-->Body"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), true);
    QSP_CHECK_STR(text, wxT("Body"));
    QSP_CHECK_BOOL(options.HasSize(), false);
}

QSP_TEST(msgoptions_ignores_other_comments)
{
    wxString text(wxT("<!--modality-->Body"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), false);
    QSP_CHECK_STR(text, wxT("<!--modality-->Body"));
}

QSP_TEST(msgoptions_unclosed_directive_is_left_as_text)
{
    wxString text(wxT("<!--modal w=500 Body"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), false);
    QSP_CHECK_STR(text, wxT("<!--modal w=500 Body"));
}

QSP_TEST(msgoptions_drops_bad_sizes_and_unknown_keys)
{
    wxString text(wxT("<!--modal w=abc h=-5 future=\"x y\" flag title=T-->Body"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), true);
    QSP_CHECK_INT(options.Width, 0);
    QSP_CHECK_INT(options.Height, 0);
    QSP_CHECK_STR(options.Title, wxT("T"));
}

QSP_TEST(msgoptions_caps_percent_at_whole_window)
{
    wxString text(wxT("<!--modal w=250%-->Body"));
    QSPMsgOptions options;
    QSP_CHECK_BOOL(QSPMsgOptions::Parse(text, options), true);
    QSP_CHECK_INT(options.Width, 100);
    QSP_CHECK_BOOL(options.IsWidthPercent, true);
}
