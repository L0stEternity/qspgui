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

#ifndef QSPGUI_TESTING_H
    #define QSPGUI_TESTING_H

    #include <wx/string.h>
    #include <vector>

    /* A test runner small enough not to be a dependency.

       The player links no test framework and adding one would mean another
       FetchContent, another licence and another thing to keep building on
       three platforms - for a suite that only needs "run these functions and
       say which ones were wrong". A failing check records itself and lets the
       case carry on, so one run reports every broken expectation rather than
       only the first. */
    namespace qsptest
    {
        typedef void (*CaseFn)();

        struct Case
        {
            const char *name;
            CaseFn fn;
        };

        /* A function-local static, so registration works however the linker
           decides to order the translation units */
        std::vector<Case> &Registry();

        struct Register
        {
            Register(const char *name, CaseFn fn);
        };

        void Fail(const char *file, int line, const wxString &message);

        /* Named so a failure reads as what was expected against what arrived */
        void CheckEqual(const char *file, int line, const char *expr,
                        const wxString &actual, const wxString &expected);
        void CheckEqual(const char *file, int line, const char *expr,
                        long actual, long expected);
        void CheckEqual(const char *file, int line, const char *expr,
                        bool actual, bool expected);
        void CheckTrue(const char *file, int line, const char *expr, bool value);

        /* Non-zero when anything failed, which is the process exit code */
        int RunAll(const wxString &filter);
    }

    #define QSP_TEST(name)                                                    \
        static void name();                                                   \
        static qsptest::Register qsp_register_##name(#name, &name);           \
        static void name()

    #define QSP_CHECK(expr) \
        qsptest::CheckTrue(__FILE__, __LINE__, #expr, (expr))

    /* The cast on the expected side is what keeps a char literal, an int and a
       wxString from each picking a different overload than the actual does. */
    #define QSP_CHECK_STR(actual, expected) \
        qsptest::CheckEqual(__FILE__, __LINE__, #actual, wxString(actual), wxString(expected))

    #define QSP_CHECK_INT(actual, expected) \
        qsptest::CheckEqual(__FILE__, __LINE__, #actual, (long)(actual), (long)(expected))

    #define QSP_CHECK_BOOL(actual, expected) \
        qsptest::CheckEqual(__FILE__, __LINE__, #actual, (bool)(actual), (bool)(expected))

#endif
