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

#ifndef DEVJSON_H
    #define DEVJSON_H

    #include <wx/string.h>
    #include <map>
    #include <vector>

    /* Minimal JSON support for the development API. wxWidgets ships neither a
       reader nor a writer, and the protocol only ever nests one level deep, so
       incoming objects are split into raw per-key tokens and decoded on demand
       instead of being parsed into a full document tree.

       Kept apart from the server so it can be exercised on its own - a
       hand-written parser sitting on a socket is exactly the code that wants
       tests. Nothing here touches wxWidgets beyond wxString. */

    class QSPJsonReader
    {
    public:
        bool Parse(const wxString &text);

        bool Has(const wxString &key) const;
        wxString GetString(const wxString &key, const wxString &defValue = wxEmptyString) const;
        long GetInt(const wxString &key, long defValue = 0) const;
        bool GetBool(const wxString &key, bool defValue = false) const;
        /* Verbatim token, for values that are themselves objects or arrays */
        wxString GetRaw(const wxString &key) const;
        std::vector<wxString> GetStringArray(const wxString &key) const;
        /* Array items kept verbatim, for arrays of objects */
        std::vector<wxString> GetRawArray(const wxString &key) const;

        static bool DecodeString(const wxString &token, wxString &result);

    private:
        static bool SkipSpace(const wxString &text, size_t &pos);
        static bool ReadToken(const wxString &text, size_t &pos, wxString &token);

        std::map<wxString, wxString> m_fields;
    };

    class QSPJsonBuilder
    {
    public:
        QSPJsonBuilder() : m_needComma(false) {}

        void StartObject();
        void EndObject();
        void StartArray();
        void EndArray();
        void Key(const wxString &key);
        void ValueString(const wxString &value);
        void ValueInt(long value);
        void ValueInt64(wxLongLong_t value);
        void ValueDouble(double value, int decimals = 3);
        void ValueBool(bool value);
        void ValueNull();
        void ValueRaw(const wxString &json);

        void Member(const wxString &key, const wxString &value);
        void MemberInt(const wxString &key, long value);
        void MemberInt64(const wxString &key, wxLongLong_t value);
        void MemberDouble(const wxString &key, double value, int decimals = 3);
        void MemberBool(const wxString &key, bool value);

        const wxString &GetText() const { return m_out; }

        static wxString Escape(const wxString &text);

    private:
        void Separate();

        wxString m_out;
        bool m_needComma;
    };

#endif
