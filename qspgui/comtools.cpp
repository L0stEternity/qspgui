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

#include "comtools.h"

#include <wx/file.h>
#include <wx/filefn.h>
#include <qsp_default.h>
#ifdef __WINDOWS__
    #include <io.h>
#endif

wxString QSPPaths::ComposeContained(const wxString &baseDir, const wxString &relativePath)
{
    if (baseDir.IsEmpty() || relativePath.IsEmpty())
        return wxEmptyString;

    /* wxPATH_DOS throughout, not the native format: a game written on Windows
       uses backslashes and is expected to run everywhere, so its separators
       have to be understood on every platform rather than treated as ordinary
       characters in a file name. */
    wxFileName fullPath(baseDir + relativePath, wxPATH_DOS);
    fullPath.MakeAbsolute();
    wxString normalizedPath(fullPath.GetFullPath());
    if (normalizedPath.StartsWith(baseDir))
        return normalizedPath;

    return wxEmptyString;
}

bool QSPPaths::IsContained(const wxString &baseDir, const wxString &path)
{
    /* Empty is how the engine asks for a file dialog rather than naming a
       file, so it is not an escape attempt. */
    if (path.IsEmpty())
        return true;
    if (baseDir.IsEmpty())
        return false;

    wxFileName fullPath(path);
    fullPath.MakeAbsolute();
    return fullPath.GetFullPath().StartsWith(baseDir);
}

bool QSPFileIO::Read(const wxString &path, std::vector<char> &data)
{
    data.clear();
    wxFile file(path, wxFile::read);
    if (!file.IsOpened()) return false;

    wxFileOffset length = file.Length();
    /* Length() answers wxInvalidOffset for anything that isn't a plain file */
    if (length == wxInvalidOffset) return false;
    if (length == 0) return true;
    /* The engine takes sizes as int, so a file it could never address is
       rejected here rather than wrapping into a small allocation */
    if (length > (wxFileOffset)INT_MAX) return false;

    data.resize((size_t)length);
    return file.Read(&data[0], data.size()) == (ssize_t)data.size();
}

bool QSPFileIO::Read(const wxString &path, std::vector<char> &data,
                     std::atomic<wxFileOffset> &done, std::atomic<wxFileOffset> &total)
{
    /* Big enough that the syscalls are not what costs, small enough that a
       40 MB file still reports forty times on the way through */
    const size_t chunkSize = 1024 * 1024;

    data.clear();
    done = 0;
    total = 0;

    wxFile file(path, wxFile::read);
    if (!file.IsOpened()) return false;

    wxFileOffset length = file.Length();
    if (length == wxInvalidOffset) return false;
    total = length;
    if (length == 0) return true;
    if (length > (wxFileOffset)INT_MAX) return false;

    data.resize((size_t)length);
    size_t offset = 0;
    while (offset < data.size())
    {
        size_t want = wxMin(chunkSize, data.size() - offset);
        ssize_t got = file.Read(&data[offset], want);
        if (got != (ssize_t)want)
        {
            /* A short read is a broken file, not a shorter one: the caller
               asked for the whole thing and must not be handed a prefix. */
            data.clear();
            return false;
        }
        offset += want;
        done = (wxFileOffset)offset;
    }
    return true;
}

bool QSPFileIO::Write(const wxString &path, const void *data, size_t size)
{
    /* Written beside the target and swapped in only once it is complete.
       Opening the target itself for writing empties it first, so a crash or a
       full disk halfway through a save used to cost the old save as well. */
    wxString tempPath(path + wxT(".tmp"));
    {
        wxFile file(tempPath, wxFile::write);
        if (!file.IsOpened()) return false;
        bool isWritten = (size == 0 || file.Write(data, size) == size);
#ifdef __WINDOWS__
        /* The swap below is journaled and the data is not, so without this a
           power cut can leave the name pointing at blocks never written.
           wxFile::Flush is no help: it only syncs where there is fsync. */
        if (isWritten) isWritten = (_commit(file.fd()) == 0);
#endif
        if (!isWritten)
        {
            file.Close();
            wxRemoveFile(tempPath);
            return false;
        }
    }
    /* ReplaceFile on Windows, so the old save is whole until the new one is */
    if (wxRenameFile(tempPath, path, true)) return true;
    wxRemoveFile(tempPath);
    return false;
}

/* Every failed attempt serializes the whole state and runs the game's ONGSAVE
   again, so the buffer starts at the last save's size plus headroom: a game with
   a multi-megabyte save pays for one pass, not two, after its first save.
   The count is capped because the loop's exit depends on the engine reporting a
   size that eventually fits, and a bug there would otherwise hang the player
   instead of failing the save. */
bool QSPGameState::Save(std::vector<char> &data, bool toRefreshUI)
{
    static int lastSize = 0;
    const int maxAttempts = 8;
    QSP_BOOL refresh = toRefreshUI ? QSP_TRUE : QSP_FALSE;
    int size = 64 * 1024;
    if (lastSize + lastSize / 8 > size) size = lastSize + lastSize / 8;

    data.resize((size_t)size);
    for (int attempt = 0; attempt < maxAttempts; ++attempt)
    {
        if (QSPSaveGameAsData(&data[0], &size, refresh))
        {
            data.resize((size_t)size);
            lastSize = size;
            return true;
        }
        /* Zero means the save itself failed, not that the buffer was small */
        if (size <= 0) break;
        data.resize((size_t)size);
    }
    data.clear();
    return false;
}

void QSPTools::LaunchDefaultBrowser(const wxString& url)
{
    /* Validate URLs, don't allow opening files & directories */
    bool canOpen;
    const wxURI uri(url);

    if (uri.HasScheme())
    {
        canOpen = uri.GetScheme() == wxT("http")
            || uri.GetScheme() == wxT("https")
            || uri.GetScheme() == wxT("mailto");
    }
    else
    {
        canOpen = !wxFileExists(url) && !wxDirExists(url);
    }

    if (canOpen)
        wxLaunchDefaultBrowser(url);
}

wxString QSPTools::GetHexColor(const wxColour& color)
{
    return wxString::Format(wxT("%.2X%.2X%.2X"), (int)color.Red(), (int)color.Green(), (int)color.Blue());
}

unsigned long QSPTools::PackColor(const wxColour& color)
{
    return ((unsigned long)color.Blue() << 16) | ((unsigned long)color.Green() << 8) | color.Red();
}

wxString QSPTools::HtmlizeWhitespaces(const wxString& str)
{
    wxString::const_iterator i;
    wxChar ch, quote;
    wxString out;
    size_t j, linepos = 0;
    bool isLastSpace = true;
    for (i = str.begin(); i != str.end(); ++i)
    {
        switch (ch = *i)
        {
        case wxT('<'):
            quote = 0;
            while (i != str.end())
            {
                ch = *i;
                if (quote)
                {
                    if (ch == wxT('\\'))
                    {
                        if (++i == str.end()) break;
                        ch = *i;
                        if (ch == quote)
                        {
                            switch (ch)
                            {
                            case wxT('"'):
                                out << wxT("&quot;");
                                break;
                            case wxT('\''):
                                out << wxT("&apos;");
                                break;
                            }
                            ++i;
                            continue;
                        }
                        out << wxT('\\');
                    }
                    switch (ch)
                    {
                    case wxT('&'):
                        out << wxT("&amp;");
                        break;
                    case wxT('<'):
                        out << wxT("&lt;");
                        break;
                    case wxT('>'):
                        out << wxT("&gt;");
                        break;
                    default:
                        if (ch == quote)
                            quote = 0;
                        out << ch;
                        break;
                    }
                }
                else
                {
                    out << ch;
                    if (ch == wxT('>'))
                        break;
                    else if (ch == wxT('"') || ch == wxT('\''))
                        quote = ch;
                }
                ++i;
            }
            if (i == str.end()) return out;
            isLastSpace = true;
            break;
        case wxT(' '):
            if (isLastSpace)
                out << wxT("&nbsp;");
            else
                out << wxT(' ');
            isLastSpace = !isLastSpace;
            ++linepos;
            break;
        case wxT('\r'):
            break;
        case wxT('\n'):
            out << wxT("<br />");
            isLastSpace = true;
            linepos = 0;
            break;
        case wxT('\t'):
            for (j = 4 - linepos % 4; j > 0; --j)
            {
                if (isLastSpace)
                    out << wxT("&nbsp;");
                else
                    out << wxT(' ');
                isLastSpace = !isLastSpace;
            }
            linepos += 4 - linepos % 4;
            break;
        default:
            out << ch;
            isLastSpace = false;
            ++linepos;
            break;
        }
    }
    return out;
}

wxString QSPTools::ProceedAsPlain(const wxString& str)
{
    wxString::const_iterator i;
    wxChar ch;
    wxString out;
    for (i = str.begin(); i != str.end(); ++i)
    {
        switch (ch = *i)
        {
        case wxT('<'):
            out << wxT("&lt;");
            break;
        case wxT('>'):
            out << wxT("&gt;");
            break;
        case wxT('&'):
            out << wxT("&amp;");
            break;
        default:
            out << ch;
            break;
        }
    }
    return out;
}

wxString QSPTools::GetAppPath(const wxString &path, const wxString &file)
{
    wxFileName appFullPath(wxStandardPaths::Get().GetExecutablePath());
    wxFileName appPath(appFullPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + path, file);
    return appPath.GetFullPath();
}

wxString QSPTools::GetResourcePath(const wxString &path, const wxString &file)
{
    wxPathList resourcePathList;
    resourcePathList.AddEnvList(wxT("XDG_DATA_DIRS"));
    resourcePathList.Add(wxStandardPaths::Get().GetResourcesDir());

    wxArrayString prefixes;
    prefixes.Add(QSP_APPNAME);
    prefixes.Add(wxEmptyString);

    for (wxPathList::iterator it = resourcePathList.begin(); it != resourcePathList.end(); ++it)
    {
        for (wxArrayString::iterator prefixIt = prefixes.begin(); prefixIt != prefixes.end(); ++prefixIt)
        {
            wxFileName resourcePath(*it, file); /* directory & file names are separated */
            if (!prefixIt->IsEmpty())
                resourcePath.Assign(resourcePath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + *prefixIt, file);

            if (!path.IsEmpty())
                resourcePath.Assign(resourcePath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + path, file);

            if (resourcePath.Exists())
                return resourcePath.GetFullPath();
        }
    }

    return GetAppPath(path, file);
}

wxString QSPTools::GetConfigPath(const wxString &path, const wxString &file)
{
    wxFileName configPath(wxStandardPaths::Get().GetUserDir(wxStandardPathsBase::Dir_Config), file);

    if (!path.IsEmpty())
        configPath.Assign(configPath.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR) + path, file);

    return configPath.GetFullPath();
}

wxString QSPTools::GetPlatform()
{
    wxOperatingSystemId osId = wxPlatformInfo::Get().GetOperatingSystemId();

    const wxChar* string = wxT("Unknown");
    if (osId & wxOS_WINDOWS)
        string = wxT("Windows");
    else if (osId & wxOS_MAC)
        string = wxT("MacOS");
    else if (osId & wxOS_UNIX_LINUX)
        string = wxT("Linux");
    else if (osId & wxOS_UNIX)
        string = wxT("Unix");

    return string;
}

wxString QSPTools::GetVersion(const wxString& libVersion)
{
    return wxString::Format(wxT("%s (classic)"), libVersion.wx_str());
}

namespace
{

/* The parts of a perspective are separated by '|', but a '|' inside a pane's
   name or caption is escaped as "\|" - so a split has to look at what comes
   before the separator, not only at the separator. The escapes are left in
   place, so joining the parts back with '|' reproduces the original. */
wxArrayString SplitPerspective(const wxString &perspective)
{
    wxArrayString parts;
    wxString current;
    bool isEscaped = false;
    for (size_t i = 0; i < perspective.Length(); ++i)
    {
        wxChar ch = perspective[i];
        if (isEscaped)
        {
            current += ch;
            isEscaped = false;
        }
        else if (ch == wxT('\\'))
        {
            current += ch;
            isEscaped = true;
        }
        else if (ch == wxT('|'))
        {
            parts.Add(current);
            current.Clear();
        }
        else
            current += ch;
    }
    parts.Add(current);
    return parts;
}

/* "dock_size(direction,layer,row)=size". The key is the coordinates as they
   were written, which is enough to match a dock across calls. */
bool ParseDockSize(const wxString &part, wxString *key, int *direction, int *size)
{
    if (!part.StartsWith(wxT("dock_size(")))
        return false;

    wxString coords(part.AfterFirst(wxT('(')).BeforeFirst(wxT(')')));
    long dirValue, sizeValue;
    if (!coords.BeforeFirst(wxT(',')).ToLong(&dirValue)) return false;
    if (!part.AfterFirst(wxT('=')).ToLong(&sizeValue)) return false;

    *key = coords;
    *direction = (int)dirValue;
    *size = (int)sizeValue;
    return true;
}

/* A pane's entry: its name, and the coordinates of the dock holding it in the
   same form ParseDockSize reports. */
bool ParsePaneDock(const wxString &part, wxString *name, wxString *key)
{
    if (part.StartsWith(wxT("dock_size(")))
        return false;

    wxString rest(part), paneName, direction, layer, row;
    while (!rest.IsEmpty())
    {
        wxString field(rest.BeforeFirst(wxT(';')));
        rest = rest.AfterFirst(wxT(';'));
        wxString fieldName(field.BeforeFirst(wxT('=')));
        wxString fieldValue(field.AfterFirst(wxT('=')));
        if (fieldName == wxT("name")) paneName = fieldValue;
        else if (fieldName == wxT("dir")) direction = fieldValue;
        else if (fieldName == wxT("layer")) layer = fieldValue;
        else if (fieldName == wxT("row")) row = fieldValue;
    }
    if (paneName.IsEmpty() || direction.IsEmpty() || layer.IsEmpty() || row.IsEmpty())
        return false;

    *name = paneName;
    *key = direction + wxT(",") + layer + wxT(",") + row;
    return true;
}

} // anonymous namespace

void QSPDockLayout::Reset()
{
    m_fractions.clear();
    m_applied.clear();
}

wxString QSPDockLayout::Rescale(const wxString &perspective, const wxSize &oldSize, const wxSize &newSize,
                                const wxArrayString &fixedPanes)
{
    if (oldSize.GetWidth() < 1 || oldSize.GetHeight() < 1 ||
        newSize.GetWidth() < 1 || newSize.GetHeight() < 1)
        return perspective;

    wxArrayString parts(SplitPerspective(perspective));
    wxArrayString fixedDocks;
    for (size_t i = 0; i < parts.GetCount(); ++i)
    {
        wxString name, key;
        if (ParsePaneDock(parts[i], &name, &key) && fixedPanes.Index(name) != wxNOT_FOUND)
            fixedDocks.Add(key);
    }

    bool isChanged = false;
    for (size_t i = 0; i < parts.GetCount(); ++i)
    {
        wxString key;
        int direction, size;
        if (!ParseDockSize(parts[i], &key, &direction, &size)) continue;
        /* 1 top, 2 right, 3 bottom, 4 left. 5 is the centre, which has no
           size of its own - it is whatever the others leave. */
        if (direction < 1 || direction > 4) continue;
        if (fixedDocks.Index(key) != wxNOT_FOUND) continue;

        bool isVertical = (direction == 1 || direction == 3);
        int oldDim = (isVertical ? oldSize.GetHeight() : oldSize.GetWidth());
        int newDim = (isVertical ? newSize.GetHeight() : newSize.GetWidth());

        std::map<wxString, double>::const_iterator fraction = m_fractions.find(key);
        std::map<wxString, int>::const_iterator applied = m_applied.find(key);
        /* Anything but the size we last wrote means the sash was dragged, so
           the share the user left it at is the one to keep from now on. */
        double share = ((fraction == m_fractions.end() || applied == m_applied.end() || applied->second != size)
            ? (double)size / oldDim
            : fraction->second);

        int scaled = (int)(share * newDim + 0.5);
        if (scaled < 1) scaled = 1;
        m_fractions[key] = share;
        m_applied[key] = scaled;
        if (scaled != size)
        {
            parts[i] = wxString::Format(wxT("dock_size(%s)=%d"), key, scaled);
            isChanged = true;
        }
    }
    if (!isChanged) return perspective;

    wxString result;
    for (size_t i = 0; i < parts.GetCount(); ++i)
    {
        if (i) result += wxT("|");
        result += parts[i];
    }
    return result;
}

/* A QSP single-quoted literal holding an arbitrary value. Quotes double,
   and a literal containing "<<" would be put through the engine's
   substitution pass - so those are lifted out into a REPLACE() over a
   sentinel the value does not contain. "'<' & '<'" concatenates to "<<"
   only after both literals have been read, which is late enough not to be
   substituted, and it costs the same few operations however many of them
   the value has. */
wxString QSPCode::ToQspLiteral(const wxString& value)
{
    wxString text(value), sentinel;
    bool hasSubExpr = text.Contains(wxT("<<"));
    if (hasSubExpr)
    {
        sentinel = wxT("@@QSPLT@@");
        while (text.Contains(sentinel)) sentinel << wxT('#');
        text.Replace(wxT("<<"), sentinel);
    }
    text.Replace(wxT("'"), wxT("''"));
    wxString literal(wxT("'") + text + wxT("'"));
    if (!hasSubExpr) return literal;
    return wxT("REPLACE(") + literal + wxT(",'") + sentinel + wxT("','<' & '<')");
}

/* A name the engine would accept: an optional type prefix, then anything
   that is not one of its delimiters and does not start with a digit.
   Checking it here is what keeps a name from turning the generated
   assignment into arbitrary code. */
bool QSPCode::IsValidVarName(const wxString& name)
{
    static const wxString forbidden(wxT(" \t&'\"()[]=!<>+-/*:,{}\r\n$%#?;`\\"));
    wxString body(name);
    if (body.StartsWith(wxT("$")) || body.StartsWith(wxT("%")))
        body = body.Mid(1);
    if (body.IsEmpty()) return false;
    if (body[0] >= wxT('0') && body[0] <= wxT('9')) return false;
    for (size_t i = 0; i < body.Length(); ++i)
    {
        if (forbidden.Find(body[i]) != wxNOT_FOUND) return false;
    }
    return true;
}

/* "[n]" for a plain index, "[literal]" for a string key, "[]" to append -
   the same three forms a game would write by hand. */
wxString QSPCode::ToQspIndex(const wxString& index, bool toAppend)
{
    long numeric = 0;
    if (toAppend) return wxT("[]");
    if (index.ToLong(&numeric) && numeric >= 0)
        return wxString::Format(wxT("[%ld]"), numeric);
    return wxT("[") + ToQspLiteral(index) + wxT("]");
}

/* The engine exposes no setter, so a write becomes the assignment a game
   would have written itself, run through QSPExecString. */
bool QSPCode::BuildAssignment(const wxString& name, const wxString& index, const wxString& value,
                              bool toAppend, wxString *code, wxString *error)
{
    if (!IsValidVarName(name))
    {
        *error = _("Incorrect variable name");
        return false;
    }
    if (name.StartsWith(wxT("%")))
    {
        *error = _("Tuple variables can't be set from outside the game");
        return false;
    }
    wxString target(name + ToQspIndex(index, toAppend));
    if (name.StartsWith(wxT("$")))
    {
        *code = target + wxT(" = ") + ToQspLiteral(value);
        return true;
    }
    /* No type prefix means a numeric variable, and the engine would reject
       a string outright - say so here rather than through an error dialog. */
    wxLongLong_t numeric = 0;
    if (!value.ToLongLong(&numeric))
    {
        *error = _("A numeric variable can only be set to a number");
        return false;
    }
    *code = target + wxT(" = ") + wxLongLong(numeric).ToString();
    return true;
}

namespace
{
    /* A size is a positive integer, optionally followed by '%'. Anything else
       is ignored rather than guessed at, so the dialog falls back to sizing
       itself for that side. */
    bool ParseMsgSize(const wxString& value, int *size, bool *isPercent)
    {
        wxString number(value);
        bool percent = number.EndsWith(wxT("%"), &number);
        long result;
        if (!number.ToLong(&result) || result <= 0) return false;
        if (percent && result > 100) result = 100;
        *size = (int)wxMin(result, 100000L);
        *isPercent = percent;
        return true;
    }
}

bool QSPMsgOptions::Parse(wxString &text, QSPMsgOptions &options)
{
    static const wxString opener(wxT("<!--modal"));
    static const wxString closer(wxT("-->"));
    size_t length = text.length();
    size_t start = 0;
    while (start < length && wxIsspace(text[start])) ++start;
    if (length - start < opener.length() ||
        text.Mid(start, opener.length()).CmpNoCase(opener) != 0)
        return false;
    size_t bodyStart = start + opener.length();
    /* "<!--modality-->" is some other comment, not this directive */
    if (bodyStart < length && !wxIsspace(text[bodyStart]) && text[bodyStart] != wxT('-'))
        return false;
    size_t bodyEnd = text.find(closer, bodyStart);
    if (bodyEnd == wxString::npos) return false;

    QSPMsgOptions result;
    wxString body(text.Mid(bodyStart, bodyEnd - bodyStart));
    size_t pos = 0, count = body.length();
    while (true)
    {
        while (pos < count && wxIsspace(body[pos])) ++pos;
        if (pos >= count) break;
        size_t keyStart = pos;
        while (pos < count && !wxIsspace(body[pos]) && body[pos] != wxT('=')) ++pos;
        wxString key(body.Mid(keyStart, pos - keyStart).Lower());
        while (pos < count && wxIsspace(body[pos])) ++pos;
        if (pos >= count || body[pos] != wxT('=')) continue;
        ++pos;
        while (pos < count && wxIsspace(body[pos])) ++pos;
        wxString value;
        if (pos < count && (body[pos] == wxT('"') || body[pos] == wxT('\'')))
        {
            wxUniChar quote = body[pos++];
            size_t valueStart = pos;
            while (pos < count && body[pos] != quote) ++pos;
            value = body.Mid(valueStart, pos - valueStart);
            if (pos < count) ++pos;
        }
        else
        {
            size_t valueStart = pos;
            while (pos < count && !wxIsspace(body[pos])) ++pos;
            value = body.Mid(valueStart, pos - valueStart);
        }

        if (key == wxT("w") || key == wxT("width"))
            ParseMsgSize(value, &result.Width, &result.IsWidthPercent);
        else if (key == wxT("h") || key == wxT("height"))
            ParseMsgSize(value, &result.Height, &result.IsHeightPercent);
        else if (key == wxT("title"))
            result.Title = value;
        else if (key == wxT("ok"))
            result.OkLabel = value;
    }

    text = text.Mid(bodyEnd + closer.length());
    /* The line break a game leaves after the directive would otherwise open
       a plain-text message with an empty line */
    size_t textStart = 0;
    while (textStart < text.length() && wxIsspace(text[textStart])) ++textStart;
    text.erase(0, textStart);
    options = result;
    return true;
}
