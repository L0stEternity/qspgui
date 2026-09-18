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

#ifndef TOOLS_H
    #define TOOLS_H

    #include <wx/wx.h>
    #include <wx/filename.h>
    #include <wx/stdpaths.h>
    #include <wx/scopeguard.h>
    #include <wx/filefn.h>
    #include <wx/uri.h>
    #include <vector>

    #define QSP_APPNAME wxT("qspgui")
    #define QSP_CONFIG wxT("qspgui.cfg")
    #define QSP_TRANSLATIONS wxT("langs")
    #define QSP_SOUNDPLUGINS wxT("sound")
    #define QSP_MIDISOUNDFONT wxT("midi.sf2")

    #define QSP_LATESTVERAPI wxT("https://api.github.com/repos/QSPFoundation/qspgui/releases/latest")
    #define QSP_LATESTVERPAGE wxT("https://github.com/QSPFoundation/qspgui/releases/latest")

    class QSPTools
    {
    public:
        static void LaunchDefaultBrowser(const wxString& url);
        static wxString GetHexColor(const wxColour& color);
        /* QSP's own packed form, 0xBBGGRR - what $BCOLOR and friends hold and
           what wxColour's packed constructor reads back. */
        static unsigned long PackColor(const wxColour& color);
        static wxString HtmlizeWhitespaces(const wxString& str);
        static wxString ProceedAsPlain(const wxString& str);
        static wxString GetAppPath(const wxString &path = wxEmptyString, const wxString &file = wxEmptyString);
        static wxString GetResourcePath(const wxString &path = wxEmptyString, const wxString &file = wxEmptyString);
        static wxString GetConfigPath(const wxString &path = wxEmptyString, const wxString &file = wxEmptyString);
        static wxString GetPlatform();
        static wxString GetVersion(const wxString& libVersion);
    };

    /* Keeping a game inside its own folder. A game names its media with paths
       it writes itself, so "../../../etc/passwd" is a path the engine will
       happily hand over - these are what stops it.

       Both take a base directory that is already absolute and ends in a
       separator, which is how the player stores it. */
    class QSPPaths
    {
    public:
        /* An absolute path for a game-supplied relative one, or an empty
           string if it would land outside the base. The resolution is what
           does the work: collapsing ".." first means a path cannot climb out
           and then walk back in through a name that only looks contained. */
        static wxString ComposeContained(const wxString &baseDir, const wxString &relativePath);
        /* Whether an absolute path is inside the base. An empty path is
           allowed, because that is how the engine asks for a dialog. */
        static bool IsContained(const wxString &baseDir, const wxString &path);
    };

    /* Whole-file reads and writes. Game worlds, saved states and snapshots all
       move through memory in one piece, and every site used to open-code the
       same malloc / read / free - each with its own subset of the checks, and
       one of them with an early return that leaked the buffer. */
    class QSPFileIO
    {
    public:
        /* False if the file cannot be opened or comes up short; data is only
           meaningful when it returns true. An empty file reads as an empty
           vector rather than a failure. */
        static bool Read(const wxString &path, std::vector<char> &data);
        static bool Write(const wxString &path, const void *data, size_t size);
        static bool Write(const wxString &path, const std::vector<char> &data)
        {
            return Write(path, data.empty() ? NULL : &data[0], data.size());
        }
    };

    /* Serialising the live session. The engine reports the buffer size it
       needs through the same argument it was handed the current size in, so a
       save is a grow-and-retry rather than one call - shared here because the
       player, the save callbacks and the development API all need it. */
    class QSPGameState
    {
    public:
        /* False leaves the engine's last error set, for the caller to report
           in whatever way suits it. */
        static bool Save(std::vector<char> &data, bool toRefreshUI);
    };

    /* Generating QSP code to hand to QSPExecString. The engine exposes no
       setter, so every write from outside a game - the JS bridge, the
       development API - becomes the assignment a game would have written
       itself, which makes escaping and name validation a shared concern. */
    class QSPCode
    {
    public:
        static wxString ToQspLiteral(const wxString& value);
        static bool IsValidVarName(const wxString& name);
        static wxString ToQspIndex(const wxString& index, bool toAppend);
        static bool BuildAssignment(const wxString& name, const wxString& index, const wxString& value,
                                    bool toAppend, wxString *code, wxString *error);
    };

#endif
