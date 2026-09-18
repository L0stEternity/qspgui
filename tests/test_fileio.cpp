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

/* Whole-file reads. Every game world and every save comes in through here, and
   the sizes involved go straight to the engine as an int - so the cases that
   matter are the ones that used to reach malloc with a length nobody checked. */

#include "testing.h"
#include "../qspgui/comtools.h"

#include <wx/filename.h>

namespace
{
    wxString TempPath(const wxString &leaf)
    {
        wxFileName path(wxFileName::GetTempDir(), leaf);
        return path.GetFullPath();
    }
}

QSP_TEST(read_returns_the_bytes_that_were_written)
{
    wxString path(TempPath(wxT("qspgui_io_roundtrip.bin")));
    const char payload[] = "world\0bytes\r\nwith embedded nulls";
    const size_t length = sizeof(payload) - 1;

    QSP_CHECK_BOOL(QSPFileIO::Write(path, payload, length), true);

    std::vector<char> data;
    QSP_CHECK_BOOL(QSPFileIO::Read(path, data), true);
    QSP_CHECK_INT((long)data.size(), (long)length);
    QSP_CHECK_BOOL(data.size() == length && memcmp(&data[0], payload, length) == 0, true);

    wxRemoveFile(path);
}

QSP_TEST(read_of_a_missing_file_fails_without_touching_the_buffer)
{
    std::vector<char> data;
    data.push_back('x');

    QSP_CHECK_BOOL(QSPFileIO::Read(TempPath(wxT("qspgui_io_not_here.bin")), data), false);
    /* Cleared rather than left holding whatever the caller had: the callers
       test the size, and a stale byte would be read as a one-byte world. */
    QSP_CHECK_INT((long)data.size(), 0);
}

QSP_TEST(an_empty_file_reads_as_empty_rather_than_as_a_failure)
{
    /* The difference matters: the callers treat a failure as "could not read"
       and an empty result as "nothing in it", and both then decline to load. */
    wxString path(TempPath(wxT("qspgui_io_empty.bin")));
    QSP_CHECK_BOOL(QSPFileIO::Write(path, NULL, 0), true);

    std::vector<char> data;
    QSP_CHECK_BOOL(QSPFileIO::Read(path, data), true);
    QSP_CHECK_INT((long)data.size(), 0);

    wxRemoveFile(path);
}

QSP_TEST(read_of_a_directory_fails)
{
    /* A directory exists, so wxFileExists is not the check that saves us here;
       opening it is. */
    std::vector<char> data;
    QSP_CHECK_BOOL(QSPFileIO::Read(wxFileName::GetTempDir(), data), false);
}

QSP_TEST(write_reports_whether_it_could_open_the_file)
{
    wxFileName missingDir(wxFileName::GetTempDir(), wxT("file.bin"));
    missingDir.AppendDir(wxT("qspgui_no_such_dir"));

    const char byte = 'x';
    QSP_CHECK_BOOL(QSPFileIO::Write(missingDir.GetFullPath(), &byte, 1), false);
}

QSP_TEST(write_takes_a_vector_as_well_as_a_pointer)
{
    wxString path(TempPath(wxT("qspgui_io_vector.bin")));
    std::vector<char> out;
    out.push_back('a');
    out.push_back('b');

    QSP_CHECK_BOOL(QSPFileIO::Write(path, out), true);

    std::vector<char> back;
    QSP_CHECK_BOOL(QSPFileIO::Read(path, back), true);
    QSP_CHECK_INT((long)back.size(), 2);

    /* An empty vector must not dereference element zero */
    std::vector<char> nothing;
    QSP_CHECK_BOOL(QSPFileIO::Write(path, nothing), true);
    QSP_CHECK_BOOL(QSPFileIO::Read(path, back), true);
    QSP_CHECK_INT((long)back.size(), 0);

    wxRemoveFile(path);
}
