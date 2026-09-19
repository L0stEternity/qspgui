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

#include "devjson.h"

/* ------------------------------------------------------------------ */
/* JSON reader                                                         */
/* ------------------------------------------------------------------ */

bool QSPJsonReader::SkipSpace(const wxString &text, size_t &pos)
{
    while (pos < text.length())
    {
        wxUniChar ch = text[pos];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n')
            return true;
        ++pos;
    }
    return false;
}

/* Copies the next value verbatim, whatever its type. Nested containers are
   tracked by depth so that an object or an array can be handed back as raw
   text and decoded later only if some command actually needs it. */
bool QSPJsonReader::ReadToken(const wxString &text, size_t &pos, wxString &token)
{
    if (!SkipSpace(text, pos)) return false;

    size_t start = pos;
    wxUniChar ch = text[pos];
    if (ch == '"')
    {
        ++pos;
        while (pos < text.length())
        {
            wxUniChar cur = text[pos++];
            if (cur == '\\')
            {
                if (pos >= text.length()) return false;
                ++pos;
            }
            else if (cur == '"')
            {
                token = text.Mid(start, pos - start);
                return true;
            }
        }
        return false;
    }

    if (ch == '{' || ch == '[')
    {
        int depth = 0;
        bool inString = false;
        while (pos < text.length())
        {
            wxUniChar cur = text[pos++];
            if (inString)
            {
                if (cur == '\\')
                {
                    if (pos >= text.length()) return false;
                    ++pos;
                }
                else if (cur == '"')
                    inString = false;
            }
            else if (cur == '"')
                inString = true;
            else if (cur == '{' || cur == '[')
                ++depth;
            else if (cur == '}' || cur == ']')
            {
                if (--depth == 0)
                {
                    token = text.Mid(start, pos - start);
                    return true;
                }
            }
        }
        return false;
    }

    while (pos < text.length())
    {
        wxUniChar cur = text[pos];
        if (cur == ',' || cur == '}' || cur == ']' ||
            cur == ' ' || cur == '\t' || cur == '\r' || cur == '\n')
            break;
        ++pos;
    }
    if (pos == start) return false;
    token = text.Mid(start, pos - start);
    return true;
}

bool QSPJsonReader::Parse(const wxString &text)
{
    m_fields.clear();

    size_t pos = 0;
    if (!SkipSpace(text, pos)) return false;
    if (text[pos] != '{') return false;
    ++pos;

    if (!SkipSpace(text, pos)) return false;
    if (text[pos] == '}') return true;

    for (;;)
    {
        wxString keyToken;
        if (!ReadToken(text, pos, keyToken)) return false;
        wxString key;
        if (!DecodeString(keyToken, key)) return false;

        if (!SkipSpace(text, pos)) return false;
        if (text[pos] != ':') return false;
        ++pos;

        wxString valueToken;
        if (!ReadToken(text, pos, valueToken)) return false;
        m_fields[key] = valueToken;

        if (!SkipSpace(text, pos)) return false;
        wxUniChar ch = text[pos++];
        if (ch == '}') return true;
        if (ch != ',') return false;
    }
}

bool QSPJsonReader::DecodeString(const wxString &token, wxString &result)
{
    result.Clear();
    if (token.length() < 2 || token[0] != '"') return false;

    for (size_t i = 1; i + 1 < token.length(); ++i)
    {
        wxUniChar ch = token[i];
        if (ch != '\\')
        {
            result += ch;
            continue;
        }
        /* The escaped character has to sit before the closing quote; a
           backslash right in front of it means the token was never terminated */
        if (++i + 1 >= token.length()) return false;
        wxUniChar esc = token[i];
        switch (esc.GetValue())
        {
        case '"': result += '"'; break;
        case '\\': result += '\\'; break;
        case '/': result += '/'; break;
        case 'b': result += '\b'; break;
        case 'f': result += '\f'; break;
        case 'n': result += '\n'; break;
        case 'r': result += '\r'; break;
        case 't': result += '\t'; break;
        case 'u':
            {
                if (i + 4 >= token.length()) return false;
                unsigned long code = 0;
                for (int digit = 0; digit < 4; ++digit)
                {
                    wxUniChar hex = token[++i];
                    code <<= 4;
                    if (hex >= '0' && hex <= '9') code |= (unsigned long)(hex.GetValue() - '0');
                    else if (hex >= 'a' && hex <= 'f') code |= (unsigned long)(hex.GetValue() - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') code |= (unsigned long)(hex.GetValue() - 'A' + 10);
                    else return false;
                }
                result += wxUniChar((wxUint32)code);
                break;
            }
        default:
            return false;
        }
    }
    return true;
}

bool QSPJsonReader::Has(const wxString &key) const
{
    return m_fields.find(key) != m_fields.end();
}

wxString QSPJsonReader::GetRaw(const wxString &key) const
{
    std::map<wxString, wxString>::const_iterator it = m_fields.find(key);
    if (it == m_fields.end()) return wxEmptyString;
    return it->second;
}

wxString QSPJsonReader::GetString(const wxString &key, const wxString &defValue) const
{
    wxString token = GetRaw(key);
    if (token.IsEmpty()) return defValue;
    if (token[0] != '"') return token; /* tolerate a bare number or keyword */

    wxString value;
    if (!DecodeString(token, value)) return defValue;
    return value;
}

long QSPJsonReader::GetInt(const wxString &key, long defValue) const
{
    wxString token = GetRaw(key);
    if (token.IsEmpty()) return defValue;
    if (token[0] == '"')
    {
        wxString decoded;
        if (!DecodeString(token, decoded)) return defValue;
        token = decoded;
    }
    long value;
    if (!token.ToLong(&value)) return defValue;
    return value;
}

bool QSPJsonReader::GetBool(const wxString &key, bool defValue) const
{
    wxString token = GetRaw(key);
    if (token.IsEmpty()) return defValue;
    if (token == wxT("true")) return true;
    if (token == wxT("false")) return false;
    return GetInt(key, defValue ? 1 : 0) != 0;
}

/* Splits an array into its item tokens, verbatim. Strings keep their quotes;
   objects and nested arrays come back whole, to be parsed by the caller. */
std::vector<wxString> QSPJsonReader::GetRawArray(const wxString &key) const
{
    std::vector<wxString> items;
    wxString token = GetRaw(key);
    if (token.length() < 2 || token[0] != '[') return items;

    size_t pos = 1;
    if (!SkipSpace(token, pos)) return items;
    if (token[pos] == ']') return items;

    for (;;)
    {
        wxString itemToken;
        if (!ReadToken(token, pos, itemToken)) break;
        items.push_back(itemToken);

        if (!SkipSpace(token, pos)) break;
        wxUniChar ch = token[pos++];
        if (ch != ',') break;
    }
    return items;
}

std::vector<wxString> QSPJsonReader::GetStringArray(const wxString &key) const
{
    std::vector<wxString> items;
    wxString token = GetRaw(key);
    if (token.length() < 2 || token[0] != '[') return items;

    size_t pos = 1;
    if (!SkipSpace(token, pos)) return items;
    if (token[pos] == ']') return items;

    for (;;)
    {
        wxString itemToken;
        if (!ReadToken(token, pos, itemToken)) break;
        if (!itemToken.IsEmpty() && itemToken[0] == '"')
        {
            wxString item;
            if (DecodeString(itemToken, item))
                items.push_back(item);
        }
        else
            items.push_back(itemToken);

        if (!SkipSpace(token, pos)) break;
        wxUniChar ch = token[pos++];
        if (ch != ',') break;
    }
    return items;
}

/* ------------------------------------------------------------------ */
/* JSON builder                                                        */
/* ------------------------------------------------------------------ */

wxString QSPJsonBuilder::Escape(const wxString &text)
{
    wxString out;
    out.reserve(text.length() + 8);
    for (wxString::const_iterator it = text.begin(); it != text.end(); ++it)
    {
        wxUniChar ch = *it;
        switch (ch.GetValue())
        {
        case '"': out += wxT("\\\""); break;
        case '\\': out += wxT("\\\\"); break;
        case '\b': out += wxT("\\b"); break;
        case '\f': out += wxT("\\f"); break;
        case '\n': out += wxT("\\n"); break;
        case '\r': out += wxT("\\r"); break;
        case '\t': out += wxT("\\t"); break;
        default:
            if (ch.GetValue() < 0x20)
                out += wxString::Format(wxT("\\u%04x"), (unsigned int)ch.GetValue());
            else
                out += ch;
            break;
        }
    }
    return out;
}

void QSPJsonBuilder::Separate()
{
    if (m_needComma) m_out += wxT(",");
}

void QSPJsonBuilder::StartObject()
{
    Separate();
    m_out += wxT("{");
    m_needComma = false;
}

void QSPJsonBuilder::EndObject()
{
    m_out += wxT("}");
    m_needComma = true;
}

void QSPJsonBuilder::StartArray()
{
    Separate();
    m_out += wxT("[");
    m_needComma = false;
}

void QSPJsonBuilder::EndArray()
{
    m_out += wxT("]");
    m_needComma = true;
}

void QSPJsonBuilder::Key(const wxString &key)
{
    Separate();
    m_out += wxT("\"") + Escape(key) + wxT("\":");
    m_needComma = false;
}

void QSPJsonBuilder::ValueString(const wxString &value)
{
    Separate();
    m_out += wxT("\"") + Escape(value) + wxT("\"");
    m_needComma = true;
}

void QSPJsonBuilder::ValueInt(long value)
{
    Separate();
    m_out += wxString::Format(wxT("%ld"), value);
    m_needComma = true;
}

/* A hit count can pass 2^31 in a single runaway loop, and long is 32 bits on
   Windows, so counters go out through this rather than ValueInt. */
void QSPJsonBuilder::ValueInt64(wxLongLong_t value)
{
    Separate();
    m_out += wxString::Format(wxT("%") wxLongLongFmtSpec wxT("d"), value);
    m_needComma = true;
}

/* Timings, rounded on the way out: a profile is thousands of numbers, and
   nobody reads past the third decimal of a millisecond. Values that are not
   finite - which no measurement here produces, but a division might - go out
   as null rather than as the JSON-invalid "nan". */
void QSPJsonBuilder::ValueDouble(double value, int decimals)
{
    Separate();
    if (value != value || value > 1e308 || value < -1e308)
    {
        m_out += wxT("null");
    }
    else
    {
        /* Format honours the C library's locale, and half of Europe writes
           0,010 - which is two JSON values, not one. The separator is put back
           by hand rather than by switching the locale, which is process-wide
           and would be felt by everything else the player is doing. */
        wxString number(wxString::Format(wxT("%.*f"), decimals, value));
        number.Replace(wxT(","), wxT("."));
        m_out += number;
    }
    m_needComma = true;
}

void QSPJsonBuilder::ValueBool(bool value)
{
    Separate();
    m_out += (value ? wxT("true") : wxT("false"));
    m_needComma = true;
}

void QSPJsonBuilder::ValueNull()
{
    Separate();
    m_out += wxT("null");
    m_needComma = true;
}

void QSPJsonBuilder::ValueRaw(const wxString &json)
{
    Separate();
    m_out += json;
    m_needComma = true;
}

void QSPJsonBuilder::Member(const wxString &key, const wxString &value)
{
    Key(key);
    ValueString(value);
}

void QSPJsonBuilder::MemberInt(const wxString &key, long value)
{
    Key(key);
    ValueInt(value);
}

void QSPJsonBuilder::MemberInt64(const wxString &key, wxLongLong_t value)
{
    Key(key);
    ValueInt64(value);
}

void QSPJsonBuilder::MemberDouble(const wxString &key, double value, int decimals)
{
    Key(key);
    ValueDouble(value, decimals);
}

void QSPJsonBuilder::MemberBool(const wxString &key, bool value)
{
    Key(key);
    ValueBool(value);
}
