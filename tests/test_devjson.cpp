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

/* The development API's hand-written JSON. It sits directly on a socket, so a
   malformed or hostile line has to come back as "no" rather than as a crash or
   a silently wrong value - and whatever the builder writes has to survive
   being read back. */

#include "testing.h"
#include "../qspgui/devjson.h"

QSP_TEST(reader_takes_the_shape_the_protocol_uses)
{
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"exec\"}")), true);
    QSP_CHECK_STR(reader.GetString(wxT("method")), wxT("exec"));
    QSP_CHECK_INT(reader.GetInt(wxT("id")), 7);
    QSP_CHECK_BOOL(reader.Has(wxT("method")), true);
    QSP_CHECK_BOOL(reader.Has(wxT("params")), false);
}

QSP_TEST(reader_answers_defaults_for_keys_that_are_not_there)
{
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("{}")), true);
    QSP_CHECK_STR(reader.GetString(wxT("loc"), wxT("fallback")), wxT("fallback"));
    QSP_CHECK_INT(reader.GetInt(wxT("line"), 12), 12);
    QSP_CHECK_BOOL(reader.GetBool(wxT("keepState"), true), true);
}

QSP_TEST(reader_handles_whitespace_and_nesting)
{
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("  { \"a\" : 1 , \"b\" : { \"c\" : 2 } , \"d\" : [1,2] }  ")), true);
    QSP_CHECK_INT(reader.GetInt(wxT("a")), 1);
    /* A nested object is kept verbatim: the protocol never needs to walk into
       one, and not parsing it is what keeps this small. */
    QSP_CHECK_STR(reader.GetRaw(wxT("b")), wxT("{ \"c\" : 2 }"));
    QSP_CHECK_INT((long)reader.GetStringArray(wxT("d")).size(), 2);
}

QSP_TEST(reader_decodes_escapes)
{
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("{\"s\":\"a\\\"b\\\\c\\nd\\u0041\"}")), true);
    QSP_CHECK_STR(reader.GetString(wxT("s")), wxT("a\"b\\c\ndA"));
}

QSP_TEST(reader_is_not_confused_by_braces_inside_strings)
{
    /* The token scanner has to know it is inside a string, or a value like
       this ends the object early and the rest of the line is lost. */
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("{\"code\":\"IF A: {GT 'x'}\",\"id\":3}")), true);
    QSP_CHECK_STR(reader.GetString(wxT("code")), wxT("IF A: {GT 'x'}"));
    QSP_CHECK_INT(reader.GetInt(wxT("id")), 3);
}

QSP_TEST(reader_refuses_malformed_input_rather_than_guessing)
{
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("")), false);
    QSP_CHECK_BOOL(reader.Parse(wxT("not json at all")), false);
    QSP_CHECK_BOOL(reader.Parse(wxT("[1,2,3]")), false);
    QSP_CHECK_BOOL(reader.Parse(wxT("{\"a\":1")), false);
    QSP_CHECK_BOOL(reader.Parse(wxT("{\"unterminated\":\"oops")), false);
}

QSP_TEST(reader_reads_arrays_of_strings)
{
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("{\"names\":[\"$A\",\"B\",\"C\"]}")), true);
    std::vector<wxString> names = reader.GetStringArray(wxT("names"));
    QSP_CHECK_INT((long)names.size(), 3);
    if (names.size() == 3)
    {
        QSP_CHECK_STR(names[0], wxT("$A"));
        QSP_CHECK_STR(names[2], wxT("C"));
    }
    /* An absent key is an empty list, not a failure */
    QSP_CHECK_INT((long)reader.GetStringArray(wxT("missing")).size(), 0);
}

QSP_TEST(reader_reads_arrays_of_objects_verbatim)
{
    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(wxT("{\"vars\":[{\"name\":\"A\"},{\"name\":\"B\"}]}")), true);
    std::vector<wxString> items = reader.GetRawArray(wxT("vars"));
    QSP_CHECK_INT((long)items.size(), 2);
    if (items.size() == 2)
    {
        QSPJsonReader first;
        QSP_CHECK_BOOL(first.Parse(items[0]), true);
        QSP_CHECK_STR(first.GetString(wxT("name")), wxT("A"));
    }
}

QSP_TEST(builder_writes_what_the_reader_reads_back)
{
    QSPJsonBuilder builder;
    builder.StartObject();
    builder.Member(wxT("loc"), wxT("room"));
    builder.MemberInt(wxT("line"), 42);
    builder.MemberBool(wxT("ok"), true);
    builder.Key(wxT("items"));
    builder.StartArray();
    builder.ValueString(wxT("one"));
    builder.ValueInt(2);
    builder.EndArray();
    builder.EndObject();

    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(builder.GetText()), true);
    QSP_CHECK_STR(reader.GetString(wxT("loc")), wxT("room"));
    QSP_CHECK_INT(reader.GetInt(wxT("line")), 42);
    QSP_CHECK_BOOL(reader.GetBool(wxT("ok")), true);
    QSP_CHECK_INT((long)reader.GetStringArray(wxT("items")).size(), 2);
}

QSP_TEST(builder_escapes_what_would_otherwise_break_the_line)
{
    /* One object per line is the whole framing, so a value carrying a newline
       would split a message in two if it were not escaped. */
    QSP_CHECK_STR(QSPJsonBuilder::Escape(wxT("a\nb")), wxT("a\\nb"));
    QSP_CHECK_STR(QSPJsonBuilder::Escape(wxT("a\"b")), wxT("a\\\"b"));
    QSP_CHECK_STR(QSPJsonBuilder::Escape(wxT("a\\b")), wxT("a\\\\b"));
    QSP_CHECK_STR(QSPJsonBuilder::Escape(wxT("a\tb")), wxT("a\\tb"));
}

QSP_TEST(builder_round_trips_an_error_message_with_quotes_in_it)
{
    /* Engine errors quote the offending line, so this is the everyday case
       rather than a corner one. */
    wxString message(wxT("[23] Location 'it''s' not found!\r\nline 1"));

    QSPJsonBuilder builder;
    builder.StartObject();
    builder.Member(wxT("message"), message);
    builder.EndObject();

    QSP_CHECK(!builder.GetText().Contains(wxT("\n")));

    QSPJsonReader reader;
    QSP_CHECK_BOOL(reader.Parse(builder.GetText()), true);
    QSP_CHECK_STR(reader.GetString(wxT("message")), message);
}
