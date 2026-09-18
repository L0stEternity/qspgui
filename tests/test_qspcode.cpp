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

/* QSPCode turns a value that came from outside the game - the JS bridge, the
   development API - into a line of QSP the engine will run. It is the boundary
   where a string stops being data and starts being code, so these cases are
   about the ways a value could escape its literal and become something the
   game never asked to execute. */

#include "testing.h"
#include "../qspgui/comtools.h"

QSP_TEST(literal_wraps_a_plain_value_in_quotes)
{
    QSP_CHECK_STR(QSPCode::ToQspLiteral(wxT("hello")), wxT("'hello'"));
    QSP_CHECK_STR(QSPCode::ToQspLiteral(wxT("")), wxT("''"));
}

QSP_TEST(literal_doubles_embedded_quotes)
{
    /* Doubling is how QSP escapes a quote, so a value ending the literal early
       and appending its own code cannot get through. */
    QSP_CHECK_STR(QSPCode::ToQspLiteral(wxT("it's")), wxT("'it''s'"));
    QSP_CHECK_STR(QSPCode::ToQspLiteral(wxT("'")), wxT("''''"));
    QSP_CHECK_STR(QSPCode::ToQspLiteral(wxT("' & KILLALL & '")), wxT("''' & KILLALL & '''"));
}

QSP_TEST(literal_defuses_expression_substitution)
{
    /* "<<...>>" inside a literal is evaluated by the engine, so a value
       carrying one would run as code. It is lifted out into a REPLACE over a
       sentinel, and the "<<" is rebuilt from two separate literals - which
       concatenate only after both have been read, too late to substitute. */
    wxString code(QSPCode::ToQspLiteral(wxT("2 << 3")));
    QSP_CHECK(!code.Contains(wxT("<<")));
    QSP_CHECK(code.StartsWith(wxT("REPLACE(")));
    QSP_CHECK(code.Contains(wxT("'<' & '<'")));

    /* A value already containing the sentinel must not collide with it */
    wxString awkward(QSPCode::ToQspLiteral(wxT("@@QSPLT@@ and << too")));
    QSP_CHECK(!awkward.Contains(wxT("<<")));
    QSP_CHECK(awkward.StartsWith(wxT("REPLACE(")));
}

QSP_TEST(literal_leaves_a_single_angle_bracket_alone)
{
    /* One "<" is not a substitution, and paying for a REPLACE on every value
       containing a comparison would be wasteful. */
    QSP_CHECK_STR(QSPCode::ToQspLiteral(wxT("a < b")), wxT("'a < b'"));
}

QSP_TEST(var_names_accept_the_forms_a_game_would_write)
{
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("GOLD")), true);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("$NAME")), true);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("%TUPLE")), true);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("player_gold2")), true);
}

QSP_TEST(var_names_reject_anything_that_could_become_code)
{
    /* Each of these would turn "<name> = <value>" into more than one
       statement, or into an expression of the caller's choosing. */
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("$")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("A & KILLALL")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("A=1 & B")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("A'")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("A[0]")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("A B")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("A\r\nGT 'x'")), false);
    /* A leading digit is a number, not a name */
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("1A")), false);
    QSP_CHECK_BOOL(QSPCode::IsValidVarName(wxT("$1")), false);
}

QSP_TEST(index_uses_the_three_forms_a_game_would_write)
{
    QSP_CHECK_STR(QSPCode::ToQspIndex(wxT("3"), false), wxT("[3]"));
    QSP_CHECK_STR(QSPCode::ToQspIndex(wxT("0"), false), wxT("[0]"));
    QSP_CHECK_STR(QSPCode::ToQspIndex(wxT("key"), false), wxT("['key']"));
    /* Appending ignores whatever index was passed */
    QSP_CHECK_STR(QSPCode::ToQspIndex(wxT("3"), true), wxT("[]"));
    QSP_CHECK_STR(QSPCode::ToQspIndex(wxT(""), true), wxT("[]"));
}

QSP_TEST(index_escapes_a_string_key_like_any_other_value)
{
    QSP_CHECK_STR(QSPCode::ToQspIndex(wxT("it's"), false), wxT("['it''s']"));
    /* A negative number is not an index; it falls through to a string key
       rather than being pasted in as arithmetic. */
    QSP_CHECK_STR(QSPCode::ToQspIndex(wxT("-1"), false), wxT("['-1']"));
}

QSP_TEST(assignment_types_the_value_from_the_name_prefix)
{
    wxString code, error;

    QSP_CHECK_BOOL(QSPCode::BuildAssignment(wxT("$NAME"), wxT("0"), wxT("Bob"), false, &code, &error), true);
    QSP_CHECK_STR(code, wxT("$NAME[0] = 'Bob'"));

    QSP_CHECK_BOOL(QSPCode::BuildAssignment(wxT("GOLD"), wxT("0"), wxT("42"), false, &code, &error), true);
    QSP_CHECK_STR(code, wxT("GOLD[0] = 42"));

    QSP_CHECK_BOOL(QSPCode::BuildAssignment(wxT("$LOG"), wxT(""), wxT("line"), true, &code, &error), true);
    QSP_CHECK_STR(code, wxT("$LOG[] = 'line'"));
}

QSP_TEST(assignment_refuses_what_the_engine_could_not_take)
{
    wxString code, error;

    /* A numeric variable cannot hold a string, and saying so here is clearer
       than an error dialog from the engine. */
    QSP_CHECK_BOOL(QSPCode::BuildAssignment(wxT("GOLD"), wxT("0"), wxT("lots"), false, &code, &error), false);
    QSP_CHECK(!error.IsEmpty());

    /* Tuples have no literal form to generate */
    QSP_CHECK_BOOL(QSPCode::BuildAssignment(wxT("%T"), wxT("0"), wxT("1"), false, &code, &error), false);
    QSP_CHECK(!error.IsEmpty());

    /* And a name that is really code is rejected before anything is built */
    QSP_CHECK_BOOL(QSPCode::BuildAssignment(wxT("A & KILLALL"), wxT("0"), wxT("1"), false, &code, &error), false);
    QSP_CHECK(!error.IsEmpty());
}

QSP_TEST(assignment_survives_a_value_that_is_all_metacharacters)
{
    wxString code, error;
    QSP_CHECK_BOOL(QSPCode::BuildAssignment(wxT("$X"), wxT("0"),
                                            wxT("' & GT 'boom' & '<<A>>"), false, &code, &error), true);
    /* Nothing of the payload may survive as syntax: the quote is doubled and
       the substitution is gone. */
    QSP_CHECK(!code.Contains(wxT("<<")));
    QSP_CHECK(code.StartsWith(wxT("$X[0] = ")));
}
