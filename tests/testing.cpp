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

#include "testing.h"

#include <wx/init.h>
#include <wx/log.h>
#include <wx/filename.h>
#include <stdio.h>

namespace qsptest
{
    namespace
    {
        int g_failuresInCase = 0;
        const char *g_currentCase = "";

        void Report(const char *file, int line, const wxString &message)
        {
            ++g_failuresInCase;
            /* Just the file name: the full build path makes the output
               unreadable and says nothing the name doesn't. */
            wxString shortFile(wxFileName(wxString::FromUTF8(file)).GetFullName());
            fprintf(stderr, "  FAIL %s:%d  %s\n",
                    (const char *)shortFile.utf8_str(), line,
                    (const char *)message.utf8_str());
        }

        wxString Quote(const wxString &text)
        {
            return wxT("\"") + text + wxT("\"");
        }
    }

    std::vector<Case> &Registry()
    {
        static std::vector<Case> cases;
        return cases;
    }

    Register::Register(const char *name, CaseFn fn)
    {
        Case testCase;
        testCase.name = name;
        testCase.fn = fn;
        Registry().push_back(testCase);
    }

    void Fail(const char *file, int line, const wxString &message)
    {
        Report(file, line, message);
    }

    void CheckEqual(const char *file, int line, const char *expr,
                    const wxString &actual, const wxString &expected)
    {
        if (actual == expected) return;
        Report(file, line, wxString::Format(wxT("%s\n         expected %s\n         actual   %s"),
                                            wxString::FromUTF8(expr), Quote(expected), Quote(actual)));
    }

    void CheckEqual(const char *file, int line, const char *expr, long actual, long expected)
    {
        if (actual == expected) return;
        Report(file, line, wxString::Format(wxT("%s\n         expected %ld\n         actual   %ld"),
                                            wxString::FromUTF8(expr), expected, actual));
    }

    void CheckEqual(const char *file, int line, const char *expr, bool actual, bool expected)
    {
        if (actual == expected) return;
        Report(file, line, wxString::Format(wxT("%s\n         expected %s\n         actual   %s"),
                                            wxString::FromUTF8(expr),
                                            expected ? wxT("true") : wxT("false"),
                                            actual ? wxT("true") : wxT("false")));
    }

    void CheckTrue(const char *file, int line, const char *expr, bool value)
    {
        if (value) return;
        Report(file, line, wxString::Format(wxT("%s is false"), wxString::FromUTF8(expr)));
    }

    int RunAll(const wxString &filter)
    {
        const std::vector<Case> &cases = Registry();
        int failedCases = 0, ranCases = 0;

        for (size_t i = 0; i < cases.size(); ++i)
        {
            wxString name(wxString::FromUTF8(cases[i].name));
            if (!filter.IsEmpty() && !name.Contains(filter)) continue;

            ++ranCases;
            g_currentCase = cases[i].name;
            g_failuresInCase = 0;
            fprintf(stderr, "%s\n", cases[i].name);
            cases[i].fn();
            if (g_failuresInCase) ++failedCases;
        }

        fprintf(stderr, "\n%d case(s) run, %d failed\n", ranCases, failedCases);
        if (!ranCases && !filter.IsEmpty())
        {
            fprintf(stderr, "no case matched \"%s\"\n", (const char *)filter.utf8_str());
            return 2;
        }
        return failedCases ? 1 : 0;
    }
}

int main(int argc, char **argv)
{
    /* wxString, wxFileName and wxFileConfig all want the library initialised,
       and this is the console-app way in - there is no wxApp here. */
    wxInitializer initializer(argc, argv);
    if (!initializer.IsOk())
    {
        fprintf(stderr, "cannot initialise wxWidgets\n");
        return 2;
    }

    /* Several cases exercise the paths that fail on purpose - a missing file,
       a directory opened as a file - and wxWidgets logs each one. That noise
       reads as if the suite were breaking, so the log is off and the checks
       speak for themselves. */
    wxLog::EnableLogging(false);

    wxString filter;
    if (argc > 1) filter = wxString::FromUTF8(argv[1]);
    return qsptest::RunAll(filter);
}
