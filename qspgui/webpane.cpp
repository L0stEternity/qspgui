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

#include "webpane.h"
#include "comtools.h"
#include "devprofile.h"

#include <wx/html/htmlwin.h>
#include <wx/filename.h>
#include <wx/filesys.h>
#include <wx/ffile.h>
#include <string>

#ifdef QSPGUI_HAVE_WEBVIEW2_SDK
    #include <WebView2.h>
#endif

wxIMPLEMENT_CLASS(QSPWebPane, wxPanel);
wxDEFINE_EVENT(wxEVT_QSP_SCRIPT_DIAG, QSPScriptDiagEvent);

bool QSPWebPane::ms_isDevMode = false;

/* ------------------------------------------------------------------ */
/* Conversions                                                         */
/* ------------------------------------------------------------------ */

wxString QSPWebUtil::ToJsString(const wxString& str)
{
    wxString out;
    out.Alloc(str.Length() + 16);
    out << wxT('"');
    for (wxString::const_iterator i = str.begin(); i != str.end(); ++i)
    {
        wxUniChar ch = *i;
        switch (ch.GetValue())
        {
        case wxT('"'):  out << wxT("\\\""); break;
        case wxT('\\'): out << wxT("\\\\"); break;
        case wxT('\n'): out << wxT("\\n"); break;
        case wxT('\r'): out << wxT("\\r"); break;
        case wxT('\t'): out << wxT("\\t"); break;
        default:
            /* U+2028 and U+2029 terminate a line in JS but not in JSON, so a
               value carrying one would end the statement it sits in. */
            if (ch.GetValue() < 0x20 || ch.GetValue() == 0x2028 || ch.GetValue() == 0x2029)
                out << wxString::Format(wxT("\\u%04X"), (unsigned int)ch.GetValue());
            else
                out << ch;
            break;
        }
    }
    out << wxT('"');
    return out;
}

wxString QSPWebUtil::ToJsArray(const wxArrayString& items)
{
    wxString out(wxT("["));
    for (size_t i = 0; i < items.GetCount(); ++i)
    {
        if (i) out << wxT(',');
        out << ToJsString(items[i]);
    }
    out << wxT(']');
    return out;
}

namespace
{
    int HexDigit(wxUniChar ch)
    {
        if (ch >= wxT('0') && ch <= wxT('9')) return (int)(ch.GetValue() - wxT('0'));
        if (ch >= wxT('a') && ch <= wxT('f')) return (int)(ch.GetValue() - wxT('a')) + 10;
        if (ch >= wxT('A') && ch <= wxT('F')) return (int)(ch.GetValue() - wxT('A')) + 10;
        return -1;
    }
}

/* encodeURIComponent only ever emits ASCII, with every byte of the UTF-8 form
   of anything else written out as %XX - so the decoded bytes can be handed
   straight back to FromUTF8. */
wxString QSPWebUtil::FromUriComponent(const wxString& str)
{
    std::string bytes;
    bytes.reserve(str.Length());
    for (size_t i = 0; i < str.Length(); ++i)
    {
        wxUniChar ch = str[i];
        if (ch == wxT('%') && i + 2 < str.Length())
        {
            int high = HexDigit(str[i + 1]), low = HexDigit(str[i + 2]);
            if (high >= 0 && low >= 0)
            {
                bytes += (char)(unsigned char)((high << 4) | low);
                i += 2;
                continue;
            }
        }
        if (ch.GetValue() < 0x80)
            bytes += (char)ch.GetValue();
        else
            bytes += wxString(ch).ToUTF8().data();
    }
    return wxString::FromUTF8(bytes.c_str(), bytes.length());
}

wxString QSPWebUtil::ToCssColor(const wxColour& color)
{
    return wxT("#") + QSPTools::GetHexColor(color);
}

/* The shell is fetched through the browser's cache, so a build that changes it
   would otherwise keep being served the previous version - leaving the pane
   with a document that has none of the functions the host calls into. Tagging
   the URL with a hash of the contents busts that, and only when the shell
   actually changed. */
wxString QSPWebUtil::HashOf(const wxString& str)
{
    wxUint32 hash = 2166136261u;
    for (wxString::const_iterator i = str.begin(); i != str.end(); ++i)
    {
        hash ^= (wxUint32)(*i).GetValue();
        hash *= 16777619u;
    }
    return wxString::Format(wxT("%08x"), hash);
}

wxString QSPWebUtil::ToFileUrl(const wxString& dirPath)
{
    if (dirPath.IsEmpty()) return wxEmptyString;
    wxFileName dir(dirPath, wxPATH_NATIVE);
    dir.MakeAbsolute();
    return wxFileSystem::FileNameToURL(dir);
}

/* ------------------------------------------------------------------ */
/* Shared shell fragments                                              */
/* ------------------------------------------------------------------ */

/* Keys pressed inside a browser control never reach wxWidgets, which would
   otherwise silently disable the 1-9 action hotkeys, Space, and
   Escape-to-exit-fullscreen. */
wxString QSPWebPane::GetInputScript()
{
    wxString script =
        /* Without the Edge SDK the browser still owns F5, so the page swallows
           it here - the frame gets it back below as a quick save. */
        wxT("document.addEventListener('keydown',function(e){\n")
        wxT("  if(e.keyCode===116)e.preventDefault();\n")
        wxT("},false);\n")
        wxT("document.addEventListener('keyup',function(e){\n")
        wxT("  var code=e.keyCode;\n")
        wxT("  if(code>=96&&code<=105)code-=48;\n") /* numpad digits -> plain digits */
        wxT("  var mods=(e.ctrlKey?1:0)|(e.shiftKey?2:0)|(e.altKey?4:0);\n")
        wxT("  qspPost('K'+code+'|'+mods);\n")
        wxT("},false);\n");

    /* A right-click menu offering "Reload" and "View source" in the middle of
       a game is not what a reader expects, so it is suppressed - except in dev
       mode, where on the backends with no devtools API it is the only way in.
       The shell URL carries its own hash, so the two variants cannot be served
       to one another from the browser cache. */
    if (!ms_isDevMode)
        script << wxT("document.addEventListener('contextmenu',function(e){e.preventDefault();},false);\n");

    return script;
}

/* Game-supplied JS is a supported feature, so a game's script failing has to
   be visible somewhere. console.error and console.warn are wrapped rather than
   replaced, so the devtools console still shows them as it would. */
wxString QSPWebPane::GetDiagnosticsScript()
{
    return
        wxT("function qspReport(kind,text,where){\n")
        wxT("  qspPost('E'+kind+'|'+encodeURIComponent(String(text))")
        wxT("+'|'+encodeURIComponent(where||''));\n")
        wxT("}\n")
        wxT("window.addEventListener('error',function(e){\n")
        /* A failed <script>/<img>/<video> fetch raises this on the element
           itself, where there is no message - only the URL that did not load,
           which is the useful half anyway. */
        wxT("  if(e.target&&e.target!==window&&e.target.tagName){\n")
        wxT("    qspReport('r','Failed to load '+e.target.tagName.toLowerCase(),")
        wxT("e.target.src||e.target.href||'');\n")
        wxT("    return;\n")
        wxT("  }\n")
        wxT("  var at=e.filename?e.filename+':'+e.lineno+':'+e.colno:'';\n")
        wxT("  qspReport('e',(e.error&&e.error.stack)?e.error.stack:e.message,at);\n")
        wxT("},true);\n")
        wxT("window.addEventListener('unhandledrejection',function(e){\n")
        wxT("  var r=e.reason;\n")
        wxT("  qspReport('e',(r&&r.stack)?r.stack:String(r),'unhandled rejection');\n")
        wxT("},false);\n")
        wxT("['error','warn'].forEach(function(name){\n")
        wxT("  var original=console[name];\n")
        wxT("  console[name]=function(){\n")
        wxT("    try{qspReport(name==='error'?'e':'w',")
        wxT("Array.prototype.join.call(arguments,' '),'console');}catch(err){}\n")
        wxT("    if(original)original.apply(console,arguments);\n")
        wxT("  };\n")
        wxT("});\n")
        wxT("qspPost('R');\n")
        wxT("})();\n")
        wxT("</script></body></html>\n");
}

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

QSPWebPane::QSPWebPane(wxWindow *parent, wxWindowID id) :
    wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxNO_BORDER)
{
    m_pathProvider = NULL;
    m_isShellRequested = false;
    m_isShellReady = false;

    m_view = wxWebView::New();
    /* Keep the browser's own pre-paint colour in sync with the app so neither
       the first frame nor a resize flashes white. Must precede Create(). */
    m_view->SetBackgroundColour(wxPanel::GetBackgroundColour());

    /* Created on a blank page, not the shell: the script message handler has
       to be registered before the document that uses it starts loading. */
    m_view->Create(this, wxID_ANY, wxT("about:blank"), wxDefaultPosition, wxDefaultSize);
    /* The context menu is what opens the devtools on the backends that have no
       API for it, so the two travel together. */
    m_view->EnableContextMenu(ms_isDevMode);
    m_view->EnableAccessToDevTools(ms_isDevMode);
    m_view->AddScriptMessageHandler(wxT("qspHost"));

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_view, 1, wxEXPAND);
    SetSizer(sizer);

    m_view->Bind(wxEVT_WEBVIEW_LOADED, &QSPWebPane::OnWebViewLoaded, this);
    m_view->Bind(wxEVT_WEBVIEW_NAVIGATING, &QSPWebPane::OnWebViewNavigating, this);
    m_view->Bind(wxEVT_WEBVIEW_ERROR, &QSPWebPane::OnWebViewError, this);
    m_view->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &QSPWebPane::OnScriptMessage, this);
    Bind(wxEVT_SIZE, &QSPWebPane::OnSize, this);

    /* Nothing is navigated here. The subclass has not supplied its document
       yet - it does that from its own constructor body, which runs after this
       one - and the whole startup sequence hangs off about:blank finishing.
       Starting it before the shell is known races the subclass for it. */
}

/* The subclass is ready. Writing the document has to come first, because the
   about:blank load below is what eventually asks for it.

   about:blank rather than the shell directly: the shell is served over a
   virtual host, and that mapping can only be registered once the backend
   exists - which is exactly what the first LOADED event signals. */
void QSPWebPane::InitShell(const wxString& fileName, const wxString& document)
{
    m_shellFile = fileName;
    WriteShellFile(document);
    m_view->LoadURL(wxT("about:blank"));
}

void QSPWebPane::OnSize(wxSizeEvent& event)
{
    event.Skip();
    if (m_view) m_view->SetSize(GetClientSize());
}

/* ------------------------------------------------------------------ */
/* Shell hosting                                                       */
/* ------------------------------------------------------------------ */

/* Its own directory, so the virtual host exposes only the shells and not the
   whole user config folder. */
wxString QSPWebPane::GetShellDir() const
{
    return wxFileName(QSPTools::GetConfigPath(QSP_SHELL_DIR, m_shellFile)).GetPath();
}

wxString QSPWebPane::GetShellPath() const
{
    return QSPTools::GetConfigPath(QSP_SHELL_DIR, m_shellFile);
}

/* Written once per run rather than shipped as a data file, so the renderer
   stays self-contained and cannot get out of sync with the binary. */
void QSPWebPane::WriteShellFile(const wxString& document)
{
    wxString path(GetShellPath());
    wxFileName::Mkdir(wxFileName(path).GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    wxFFile file(path, wxT("wb"));
    if (file.IsOpened())
        file.Write(document, wxConvUTF8);

    /* Built from the document rather than from the build date, so an unchanged
       shell keeps its cached copy */
    m_shellUrl = wxT("https://") QSP_SHELL_HOST wxT("/") + m_shellFile +
                 wxT("?v=") + QSPWebUtil::HashOf(document);
}

#ifdef QSPGUI_HAVE_WEBVIEW2_SDK
/* The browser's own shortcuts mean nothing in a game: F5 and Ctrl-R would
   reload the shell out from under it, and the player's keys are forwarded to
   the frame, where F5 is quick save. */
static void DisableBrowserAccelerators(wxWebView *view)
{
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(view->GetNativeBackend());
    if (!webView2) return;

    ICoreWebView2Settings *settings = NULL;
    if (FAILED(webView2->get_Settings(&settings)) || !settings) return;

    ICoreWebView2Settings3 *settings3 = NULL;
    if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&settings3))))
    {
        settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
        settings3->Release();
    }
    settings->Release();
}
#endif

/* Serve the shell over a virtual host as well, so the document and the game
   assets share an https-style origin. A file:// document is not allowed to
   pull resources from a virtual host, which silently breaks every image. */
bool QSPWebPane::SetupShellHost()
{
#ifdef QSPGUI_HAVE_WEBVIEW2_SDK
    DisableBrowserAccelerators(m_view);
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(m_view->GetNativeBackend());
    ICoreWebView2_3 *webView2_3 = NULL;
    if (webView2 && SUCCEEDED(webView2->QueryInterface(IID_PPV_ARGS(&webView2_3))))
    {
        HRESULT hr = webView2_3->SetVirtualHostNameToFolderMapping(
            QSP_SHELL_HOST, GetShellDir().wc_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        webView2_3->Release();
        if (SUCCEEDED(hr)) return true;
    }
#endif
    /* Without the Edge SDK we stay on file://, which works for the document
       itself; only the virtual-host containment is lost. */
    m_shellUrl = wxFileSystem::FileNameToURL(wxFileName(GetShellPath()));
    return false;
}

void QSPWebPane::LoadShell()
{
    /* InitShell is what starts about:blank, so the file is always set by the
       time this can run. The guard is here because getting that order wrong
       costs a pane that stays blank for good: nothing else is ever navigated,
       so there is no second LOADED event to recover on. Logged rather than
       asserted - asserts are live in this project's release builds, and a
       dialog in a player's hands is worse than a line in the log. */
    if (m_shellFile.IsEmpty())
    {
        wxLogError(wxT("QSPWebPane: InitShell() must run before the pane loads"));
        return;
    }
    if (m_isShellRequested) return;
    m_isShellRequested = true;
    SetupShellHost();
    m_view->LoadURL(m_shellUrl);
}

void QSPWebPane::SetPathProvider(PathProvider *provider)
{
    m_pathProvider = provider;
    SetupGameFolderAccess();
}

void QSPWebPane::SetupGameFolderAccess()
{
    if (!m_pathProvider) return;

    wxString gameDir(m_pathProvider->GetGamePath());
    if (gameDir == m_gameDir) return; // already set up for this folder

#ifdef QSPGUI_HAVE_WEBVIEW2_SDK
    /* The game folder usually becomes known while the backend is still coming
       up. Leave m_gameDir untouched in that case so this runs again once the
       shell has loaded, instead of silently settling for the file:// fallback. */
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(m_view->GetNativeBackend());
    if (!webView2) return;

    ICoreWebView2_3 *webView2_3 = NULL;
    if (SUCCEEDED(webView2->QueryInterface(IID_PPV_ARGS(&webView2_3))))
    {
        bool isMapped = false;
        if (!gameDir.IsEmpty())
        {
            /* GetGamePath() keeps a trailing separator, which the mapping does
               not accept. */
            wxString folder(gameDir);
            while (folder.Length() > 1 && wxFileName::IsPathSeparator(folder.Last()))
                folder.RemoveLast();

            isMapped = SUCCEEDED(webView2_3->SetVirtualHostNameToFolderMapping(
                QSP_GAME_HOST, folder.wc_str(),
                COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW));
        }
        else
        {
            webView2_3->ClearVirtualHostNameToFolderMapping(QSP_GAME_HOST);
        }
        webView2_3->Release();

        if (isMapped)
        {
            m_gameDir = gameDir;
            m_baseUrl = wxT("https://") QSP_GAME_HOST wxT("/");
            /* A mapping only applies to documents loaded after it is
               registered; an existing document keeps trying to resolve the
               name over DNS and its requests just hang. This is the only
               navigation after startup, and it happens once per game load -
               never per location. */
            if (m_isShellRequested)
            {
                m_isShellReady = false;
                m_view->LoadURL(m_shellUrl);
            }
            return;
        }
    }
#endif

    m_gameDir = gameDir;
    m_baseUrl = QSPWebUtil::ToFileUrl(gameDir);
    if (m_isShellReady)
        RunScript(wxString::Format(wxT("qspSetBase(%s);"), QSPWebUtil::ToJsString(m_baseUrl)));
}

void QSPWebPane::RunScript(const wxString& script)
{
    /* Always async: QSP callbacks run inside engine script execution, and the
       synchronous variant pumps a nested message loop, which re-enters it. */
    if (m_isShellReady && m_view && !script.IsEmpty())
    {
        /* Timed for the profiler. Being async, what this measures is the cost
           of handing the script over - marshalling a document that has grown
           to a megabyte still shows up, the browser's own render does not. The
           bytes are the more telling half. */
        QSPDev::ProfScope scriptScope(QSPDev::Prof_Script);
        scriptScope.AddBytes((long long)script.length());
        m_view->RunScriptAsync(script);
    }
}

void QSPWebPane::SetPaneName(const wxString& name)
{
    if (m_paneName == name) return;
    m_paneName = name;
    RunScript(wxString::Format(wxT("qspSetPane(%s);"), QSPWebUtil::ToJsString(name)));
}

/* wxWebView can allow the devtools but has no portable way to open them, so
   this reaches the Edge backend directly. Elsewhere the context menu that dev
   mode leaves enabled is the way in. */
void QSPWebPane::ShowDevTools()
{
    if (!ms_isDevMode || !m_view) return;
#ifdef QSPGUI_HAVE_WEBVIEW2_SDK
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(m_view->GetNativeBackend());
    if (webView2) webView2->OpenDevToolsWindow();
#endif
}

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

void QSPWebPane::OnWebViewLoaded(wxWebViewEvent& event)
{
    if (!m_isShellRequested)
    {
        /* about:blank is up, so the backend exists and can take the mapping. */
        LoadShell();
        return;
    }
    if (event.GetURL() != m_shellUrl) return;

    m_isShellReady = true;
    /* Retry the mapping in case the backend was not up when the game folder
       first became known. It is a no-op once the folder is already set up, so
       this cannot loop on the reload it may trigger. */
    SetupGameFolderAccess();
    if (!m_isShellReady) return; // a reload was started; this document is going away

    if (!m_baseUrl.IsEmpty())
        RunScript(wxString::Format(wxT("qspSetBase(%s);"), QSPWebUtil::ToJsString(m_baseUrl)));
    if (!m_paneName.IsEmpty())
        RunScript(wxString::Format(wxT("qspSetPane(%s);"), QSPWebUtil::ToJsString(m_paneName)));

    OnShellReady();
}

/* Only the shell may ever load here. A navigation blanks the view before the
   replacement paints, and that blank frame is exactly the flash this renderer
   exists to avoid, so links go through the script message channel instead. */
void QSPWebPane::OnWebViewNavigating(wxWebViewEvent& event)
{
    if (event.GetURL() != m_shellUrl && event.GetURL() != wxT("about:blank"))
        event.Veto();
}

void QSPWebPane::OnWebViewError(wxWebViewEvent& WXUNUSED(event))
{
    /* Swallowed on purpose: a missing game asset must not interrupt play. */
}

void QSPWebPane::OnScriptMessage(wxWebViewEvent& event)
{
    wxString message(event.GetString());
    if (message.IsEmpty()) return;

    if (message[0] == wxT('R'))
    {
        /* The document's own script has run; anything sent before this would
           have gone to a document with no functions in it. */
        m_isShellReady = true;
        return;
    }
    if (message[0] == wxT('K'))
    {
        long code = 0, mods = 0;
        wxString payload(message.Mid(1));
        if (!payload.BeforeFirst(wxT('|')).ToLong(&code)) return;
        payload.AfterFirst(wxT('|')).ToLong(&mods);
        /* Browser key codes are wx key codes only by accident: the digits and
           escape line up, the function keys don't. */
        if (code >= 112 && code <= 123) code = WXK_F1 + (code - 112);

        wxKeyEvent keyEvent(wxEVT_KEY_UP);
        keyEvent.m_keyCode = code;
        keyEvent.SetControlDown((mods & 1) != 0);
        keyEvent.SetShiftDown((mods & 2) != 0);
        keyEvent.SetAltDown((mods & 4) != 0);
        keyEvent.SetEventObject(this);
        keyEvent.SetId(GetId());
        GetParent()->GetEventHandler()->ProcessEvent(keyEvent);
        return;
    }
    if (message[0] == wxT('E'))
    {
        /* "E<kind><text>|<where>", both parts percent-encoded */
        if (message.Length() < 2) return;
        wxChar kind = message[1];
        wxString payload(message.Mid(2));

        QSPScriptDiagEvent *diagEvent = new QSPScriptDiagEvent(wxEVT_QSP_SCRIPT_DIAG, GetId());
        diagEvent->SetKind(kind == wxT('w') ? wxT("warning")
                         : kind == wxT('r') ? wxT("resource")
                         : wxT("error"));
        diagEvent->SetText(QSPWebUtil::FromUriComponent(payload.BeforeFirst(wxT('|'))));
        diagEvent->SetWhere(QSPWebUtil::FromUriComponent(payload.AfterFirst(wxT('|'))));
        diagEvent->SetPane(m_paneName);
        diagEvent->SetEventObject(this);
        /* Queued and left to propagate: the frame may not be the direct
           parent, and nothing here may re-enter the view while it is still
           dispatching its own message. */
        QueueEvent(diagEvent);
        return;
    }

    OnPaneMessage(message);
}
