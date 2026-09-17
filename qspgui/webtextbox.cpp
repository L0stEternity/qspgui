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

#include "webtextbox.h"
#include "comtools.h"
#include <wx/html/htmlwin.h>
#include <wx/filename.h>
#include <wx/filesys.h>
#include <wx/ffile.h>

#ifdef QSPGUI_HAVE_WEBVIEW2_SDK
    #include <WebView2.h>
#endif

/* Virtual hosts used by the Edge backend. Mapping a host to a folder keeps
   relative paths contained inside that folder (the URL parser collapses "..",
   so a game cannot walk out of its own directory) and gives media a real
   https origin, which file:// does not - seeking in <video> needs it. */
#define QSP_SHELL_HOST wxT("qsp.shell")
#define QSP_GAME_HOST  wxT("qsp.game")
#define QSP_SHELL_DIR  wxT("qspgui_web")
#define QSP_SHELL_FILE wxT("qspgui_shell.html")

wxIMPLEMENT_CLASS(QSPWebTextBox, wxPanel);

BEGIN_EVENT_TABLE(QSPWebTextBox, wxPanel)
    EVT_SIZE(QSPWebTextBox::OnSize)
END_EVENT_TABLE()

namespace
{
    /* Encode an arbitrary string as a JS string literal, including the quotes.
       Everything outside a conservative safe set is escaped as \uXXXX so we
       never have to reason about the script's own encoding. */
    wxString ToJsString(const wxString& str)
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

    wxString ToCssColor(const wxColour& color)
    {
        return wxT("#") + QSPTools::GetHexColor(color);
    }

    /* The shell is fetched through the browser's cache, so a build that
       changes it would otherwise keep being served the previous version -
       leaving the pane with a document that has no qspUpdate in it. Tagging
       the URL with a hash of the contents busts that, and only when the shell
       actually changed. */
    wxString HashOf(const wxString& str)
    {
        wxUint32 hash = 2166136261u;
        for (wxString::const_iterator i = str.begin(); i != str.end(); ++i)
        {
            hash ^= (wxUint32)(*i).GetValue();
            hash *= 16777619u;
        }
        return wxString::Format(wxT("%08x"), hash);
    }

    /* Turn a local directory into a URL usable as a document base. */
    wxString ToFileUrl(const wxString& dirPath)
    {
        if (dirPath.IsEmpty()) return wxEmptyString;
        wxFileName dir(dirPath, wxPATH_NATIVE);
        dir.MakeAbsolute();
        return wxFileSystem::FileNameToURL(dir);
    }
}

QSPWebTextBox::QSPWebTextBox(wxWindow *parent, wxWindowID id) :
    wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxNO_BORDER)
{
    m_pathProvider = NULL;
    m_isShellRequested = false;
    m_isShellReady = false;
    m_isUpdatePending = false;
    m_updateDepth = 0;
    m_toUseHtml = false;
    m_toScroll = false;
    m_font = *wxNORMAL_FONT;
    m_linkColor = *wxBLUE;
    m_backColor = wxPanel::GetBackgroundColour();
    m_fontColor = wxPanel::GetForegroundColour();

    WriteShellFile();

    m_view = wxWebView::New();
    /* Keep the browser's own pre-paint colour in sync with the app so neither
       the first frame nor a resize flashes white. Must precede Create(). */
    m_view->SetBackgroundColour(m_backColor);

    /* Created on a blank page, not the shell: the script message handler has
       to be registered before the document that uses it starts loading. */
    m_view->Create(this, wxID_ANY, wxT("about:blank"), wxDefaultPosition, wxDefaultSize);
    m_view->EnableContextMenu(false);
    m_view->EnableAccessToDevTools(false);
    m_view->AddScriptMessageHandler(wxT("qspHost"));

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_view, 1, wxEXPAND);
    SetSizer(sizer);

    m_view->Bind(wxEVT_WEBVIEW_LOADED, &QSPWebTextBox::OnWebViewLoaded, this);
    m_view->Bind(wxEVT_WEBVIEW_NAVIGATING, &QSPWebTextBox::OnWebViewNavigating, this);
    m_view->Bind(wxEVT_WEBVIEW_ERROR, &QSPWebTextBox::OnWebViewError, this);
    m_view->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &QSPWebTextBox::OnScriptMessage, this);

    /* The shell is not loaded yet: its virtual host can only be registered
       once the backend exists, which is signalled by about:blank finishing. */
    m_view->LoadURL(wxT("about:blank"));
}

/* Serve the shell over a virtual host as well, so the document and the game
   assets share an https-style origin. A file:// document is not allowed to
   pull resources from a virtual host, which silently breaks every image. */
bool QSPWebTextBox::SetupShellHost()
{
#ifdef QSPGUI_HAVE_WEBVIEW2_SDK
    ICoreWebView2 *webView2 = static_cast<ICoreWebView2 *>(m_view->GetNativeBackend());
    ICoreWebView2_3 *webView2_3 = NULL;
    if (webView2 && SUCCEEDED(webView2->QueryInterface(IID_PPV_ARGS(&webView2_3))))
    {
        HRESULT hr = webView2_3->SetVirtualHostNameToFolderMapping(
            QSP_SHELL_HOST, GetShellDir().wc_str(),
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
        webView2_3->Release();
        if (SUCCEEDED(hr))
        {
            m_shellUrl = wxT("https://") QSP_SHELL_HOST wxT("/") QSP_SHELL_FILE
                         wxT("?v=") + m_shellVersion;
            return true;
        }
    }
#endif
    /* Without the Edge SDK we stay on file://, which works for the document
       itself; only the virtual-host containment is lost. */
    m_shellUrl = wxFileSystem::FileNameToURL(wxFileName(GetShellPath()));
    return false;
}

void QSPWebTextBox::LoadShell()
{
    if (m_isShellRequested) return;
    m_isShellRequested = true;
    SetupShellHost();
    m_view->LoadURL(m_shellUrl);
}

/* Its own directory, so the virtual host exposes only the shell and not the
   whole user config folder. */
wxString QSPWebTextBox::GetShellDir() const
{
    return wxFileName(QSPTools::GetConfigPath(QSP_SHELL_DIR, QSP_SHELL_FILE)).GetPath();
}

wxString QSPWebTextBox::GetShellPath() const
{
    return QSPTools::GetConfigPath(QSP_SHELL_DIR, QSP_SHELL_FILE);
}

/* The shell is written once per run rather than shipped as a data file so the
   renderer stays self-contained and can't get out of sync with the binary. */
void QSPWebTextBox::WriteShellFile()
{
    static const wxChar *shell =
        wxT("<!DOCTYPE html>\n")
        wxT("<html><head><meta charset=\"utf-8\">\n")
        wxT("<base id=\"qsp-base\" href=\"\">\n")
        wxT("<style>\n")
        wxT(":root{--qsp-bg:#e0e0e0;--qsp-fg:#000000;--qsp-link:#0000ff;")
        wxT("--qsp-font:sans-serif;--qsp-size:12pt;--qsp-bgimg:none;}\n")
        wxT("html,body{margin:0;padding:0;height:100%;}\n")
        wxT("body{position:relative;background-color:var(--qsp-bg);color:var(--qsp-fg);")
        wxT("font-family:var(--qsp-font);font-size:var(--qsp-size);")
        wxT("background-image:var(--qsp-bgimg);background-repeat:no-repeat;")
        wxT("background-position:center center;background-size:contain;")
        wxT("background-attachment:fixed;overflow:hidden;}\n")
        wxT("a{color:var(--qsp-link);}\n")
        /* The two layers sit exactly on top of each other and differ only in
           which one is visible. A hidden layer is still laid out - that is the
           point, its images load and its height is known - so the swap is a
           pure visibility change with nothing left to compute or fetch. */
        wxT(".qsp-layer{position:absolute;left:0;top:0;right:0;bottom:0;")
        wxT("overflow-y:auto;overflow-x:hidden;padding:5px;box-sizing:border-box;}\n")
        wxT(".qsp-hidden{visibility:hidden;}\n")
        wxT("img,video{max-width:100%;height:auto;}\n")
        wxT("</style></head>\n")
        wxT("<body>\n")
        wxT("<div id=\"qsp-l0\" class=\"qsp-layer\"></div>\n")
        wxT("<div id=\"qsp-l1\" class=\"qsp-layer qsp-hidden\"></div>\n")
        wxT("<script>\n")
        wxT("(function(){\n")
        wxT("var layers=[document.getElementById('qsp-l0'),document.getElementById('qsp-l1')];\n")
        wxT("var base=document.getElementById('qsp-base');\n")
        wxT("var active=0,token=0;\n")
        /* The host channel can be a moment late on startup; an exception here
           would take the rest of the shell script down with it. */
        wxT("function qspPost(m){try{window.qspHost.postMessage(m);}catch(err){}}\n")
        wxT("window.qspSetBase=function(href){base.href=href;};\n")
        wxT("function applyStyle(s){\n")
        wxT("  if(!s)return;\n")
        wxT("  var r=document.documentElement.style;\n")
        wxT("  r.setProperty('--qsp-bg',s.bg);r.setProperty('--qsp-fg',s.fg);\n")
        wxT("  r.setProperty('--qsp-link',s.link);r.setProperty('--qsp-font',s.font);\n")
        wxT("  r.setProperty('--qsp-size',s.size);r.setProperty('--qsp-bgimg',s.bgimg);\n")
        wxT("}\n")
        /* Nothing reaches the screen until it is finished. The new markup goes
           into the hidden layer, we wait for its media to become usable, and
           only then do the layers trade places - so the pane never shows a
           blank frame or a half-decoded image, it goes straight from one
           finished state to the next. */
        wxT("window.qspUpdate=function(html,toBottom,style){\n")
        wxT("  var mine=++token;\n")
        wxT("  var back=layers[1-active];\n")
        wxT("  back.innerHTML=html;\n")
        wxT("  var media=back.querySelectorAll('img,video'),waits=[],i,m;\n")
        wxT("  for(i=0;i<media.length;i++){\n")
        wxT("    m=media[i];\n")
        wxT("    if(m.tagName==='IMG'){if(m.complete)continue;}\n")
        wxT("    else if(m.readyState>=1)continue;\n")
        wxT("    waits.push(new Promise(function(res){\n")
        wxT("      var ev=this.tagName==='IMG'?'load':'loadedmetadata';\n")
        wxT("      this.addEventListener(ev,res,{once:true});\n")
        wxT("      this.addEventListener('error',res,{once:true});\n")
        wxT("    }.bind(m)));\n")
        wxT("  }\n")
        wxT("  var swap=function(){\n")
        /* A newer update has already staged over this one; its swap wins. */
        wxT("    if(mine!==token)return;\n")
        wxT("    applyStyle(style);\n")
        wxT("    back.scrollTop=toBottom?back.scrollHeight:0;\n")
        wxT("    back.classList.remove('qsp-hidden');\n")
        wxT("    layers[active].classList.add('qsp-hidden');\n")
        wxT("    layers[active].innerHTML='';\n")
        wxT("    active=1-active;\n")
        wxT("  };\n")
        wxT("  if(!waits.length){requestAnimationFrame(swap);return;}\n")
        /* A missing or slow asset must not strand the pane on old content. */
        wxT("  var late=setTimeout(swap,400);\n")
        wxT("  Promise.all(waits).then(function(){clearTimeout(late);requestAnimationFrame(swap);});\n")
        wxT("};\n")
        wxT("window.qspScrollTo=function(anchor){\n")
        wxT("  var l=layers[active];\n")
        wxT("  var el=anchor?document.getElementById(anchor):null;\n")
        wxT("  if(!el&&anchor){var n=document.getElementsByName(anchor);el=n.length?n[0]:null;}\n")
        wxT("  if(el)el.scrollIntoView(true);else l.scrollTop=0;\n")
        wxT("};\n")
        /* Links are reported with their *raw* attribute so the host keeps the
           classic player's semantics for "#anchor" and "EXEC:" untouched. */
        wxT("document.addEventListener('click',function(e){\n")
        wxT("  var a=e.target;\n")
        wxT("  while(a&&a.tagName!=='A')a=a.parentElement;\n")
        wxT("  if(!a)return;\n")
        wxT("  e.preventDefault();\n")
        wxT("  var raw=a.getAttribute('href');\n")
        wxT("  if(raw!==null)qspPost('L'+raw);\n")
        wxT("},true);\n")
        /* Keys pressed inside the browser never reach wxWidgets, so the action
           hotkeys (1-9, space) and fullscreen escape would be dead here unless
           we hand them back to the frame ourselves. */
        wxT("document.addEventListener('keyup',function(e){\n")
        wxT("  var code=e.keyCode;\n")
        wxT("  if(code>=96&&code<=105)code-=48;\n") // numpad digits -> plain digits
        wxT("  var mods=(e.ctrlKey?1:0)|(e.shiftKey?2:0)|(e.altKey?4:0);\n")
        wxT("  qspPost('K'+code+'|'+mods);\n")
        wxT("},false);\n")
        wxT("document.addEventListener('contextmenu',function(e){e.preventDefault();},false);\n")
        wxT("qspPost('R');\n")
        wxT("})();\n")
        wxT("</script></body></html>\n");

    m_shellVersion = HashOf(shell);

    wxString path(GetShellPath());
    wxFileName::Mkdir(wxFileName(path).GetPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

    wxFFile file(path, wxT("wb"));
    if (file.IsOpened())
        file.Write(shell, wxConvUTF8);
}

void QSPWebTextBox::SetPathProvider(PathProvider *provider)
{
    m_pathProvider = provider;
    SetupGameFolderAccess();
}

/* Point the document at the current game folder. On Edge this goes through a
   virtual host so the mapping itself enforces containment; elsewhere we fall
   back to a file:// base, which does not. */
void QSPWebTextBox::SetupGameFolderAccess()
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
            /* GetGamePath() keeps a trailing separator, which the mapping
               does not accept. */
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
    m_baseUrl = ToFileUrl(gameDir);
    if (m_isShellReady)
        RunScript(wxString::Format(wxT("qspSetBase(%s);"), ToJsString(m_baseUrl).wx_str()));
}

void QSPWebTextBox::RunScript(const wxString& script)
{
    /* Always async: QSP callbacks run inside engine script execution, and the
       synchronous variant pumps a nested message loop, which re-enters it. */
    if (m_isShellReady && m_view)
        m_view->RunScriptAsync(script);
}

wxString QSPWebTextBox::BuildStyleObject() const
{
    wxString bgImage(wxT("none"));
    if (!m_backImagePath.IsEmpty())
    {
        /* Resolved against the document base, same as any other game asset. */
        wxString escaped(m_backImagePath);
        escaped.Replace(wxT("\\"), wxT("\\\\"));
        escaped.Replace(wxT("'"), wxT("\\'"));
        bgImage = wxT("url('") + escaped + wxT("')");
    }

    wxString style;
    style << wxT("{bg:")    << ToJsString(ToCssColor(m_backColor))
          << wxT(",fg:")    << ToJsString(ToCssColor(m_fontColor))
          << wxT(",link:")  << ToJsString(ToCssColor(m_linkColor))
          << wxT(",font:")  << ToJsString(m_font.GetFaceName())
          << wxT(",size:")  << ToJsString(wxString::Format(wxT("%dpt"), m_font.GetPointSize()))
          << wxT(",bgimg:") << ToJsString(bgImage)
          << wxT("}");
    return style;
}

wxString QSPWebTextBox::BuildUpdateScript() const
{
    wxString text(QSPTools::HtmlizeWhitespaces(m_toUseHtml ? m_text : QSPTools::ProceedAsPlain(m_text)));
    return wxString::Format(wxT("qspUpdate(%s,%s,%s);"),
        ToJsString(text).wx_str(),
        m_toScroll ? wxT("true") : wxT("false"),
        BuildStyleObject().wx_str());
}

/* Text, colours and font arrive one at a time during a refresh; collecting
   them into a single update keeps the pane from staging the same content
   several times over. */
void QSPWebTextBox::MarkDirty()
{
    if (m_isUpdatePending) return;
    m_isUpdatePending = true;
    CallAfter(&QSPWebTextBox::Flush);
}

void QSPWebTextBox::Flush()
{
    if (!m_isUpdatePending) return;
    if (m_updateDepth > 0) return;  // EndUpdate will come back here
    if (!m_isShellReady) return;    // the shell's load handler will come back here

    m_isUpdatePending = false;
    RunScript(BuildUpdateScript());
}

void QSPWebTextBox::BeginUpdate()
{
    ++m_updateDepth;
}

void QSPWebTextBox::EndUpdate()
{
    if (m_updateDepth <= 0) return;
    if (--m_updateDepth > 0) return;
    if (m_isUpdatePending) CallAfter(&QSPWebTextBox::Flush);
}

void QSPWebTextBox::RefreshUI()
{
    MarkDirty();
}

void QSPWebTextBox::SetIsHtml(bool isHtml)
{
    if (m_toUseHtml != isHtml)
    {
        m_toUseHtml = isHtml;
        MarkDirty();
    }
}

void QSPWebTextBox::SetText(const wxString& text, bool toScroll)
{
    if (m_text != text)
    {
        /* Mirrors the classic renderer: only keep the view pinned to the
           bottom when the new text is an extension of the old one. */
        if (toScroll)
        {
            if (m_text.IsEmpty() || !text.StartsWith(m_text))
                toScroll = false;
        }
        m_text = text;
        m_toScroll = toScroll;
        SetupGameFolderAccess();
        MarkDirty();
    }
}

void QSPWebTextBox::LoadBackImage(const wxString& imagePath)
{
    if (m_backImagePath != imagePath)
    {
        m_backImagePath = imagePath;
        SetupGameFolderAccess();
        MarkDirty();
    }
}

void QSPWebTextBox::LoadPage(const wxString& location)
{
    wxString anchor(location);
    if (anchor.StartsWith(wxT("#")))
        anchor = anchor.Mid(1);
    RunScript(wxString::Format(wxT("qspScrollTo(%s);"), ToJsString(anchor).wx_str()));
}

void QSPWebTextBox::SetTextFont(const wxFont& font)
{
    int fontSize = font.GetPointSize();
    wxString fontName(font.GetFaceName());
    if (!m_font.GetFaceName().IsSameAs(fontName, false) || m_font.GetPointSize() != fontSize)
    {
        m_font = font;
        MarkDirty();
    }
}

void QSPWebTextBox::SetLinkColor(const wxColour& clr)
{
    if (m_linkColor != clr)
    {
        m_linkColor = clr;
        MarkDirty();
    }
}

bool QSPWebTextBox::SetBackgroundColour(const wxColour& colour)
{
    /* The panel itself shows through while the browser is still coming up
       and during resizes, so it has to carry the same colour. */
    bool result = wxPanel::SetBackgroundColour(colour);
    if (m_backColor != colour)
    {
        m_backColor = colour;
        if (m_view) m_view->SetBackgroundColour(colour);
        MarkDirty();
    }
    return result;
}

bool QSPWebTextBox::SetForegroundColour(const wxColour& colour)
{
    bool result = wxPanel::SetForegroundColour(colour);
    if (m_fontColor != colour)
    {
        m_fontColor = colour;
        MarkDirty();
    }
    return result;
}

void QSPWebTextBox::OnSize(wxSizeEvent& event)
{
    if (GetSizer()) Layout();
    event.Skip();
}

void QSPWebTextBox::OnWebViewLoaded(wxWebViewEvent& event)
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
        RunScript(wxString::Format(wxT("qspSetBase(%s);"), ToJsString(m_baseUrl).wx_str()));

    m_isUpdatePending = true;
    Flush();
}

/* Only the shell may ever load here. A navigation blanks the view before the
   replacement paints, and that blank frame is exactly the flash this renderer
   exists to avoid, so links go through the script message channel instead. */
void QSPWebTextBox::OnWebViewNavigating(wxWebViewEvent& event)
{
    if (event.GetURL() != m_shellUrl && event.GetURL() != wxT("about:blank"))
        event.Veto();
}

void QSPWebTextBox::OnWebViewError(wxWebViewEvent& WXUNUSED(event))
{
    /* Swallowed on purpose: a missing game asset must not interrupt play. */
}

/* Re-emit the click as the same wxHtmlLinkEvent the classic renderer produces,
   so QSPFrame::OnLinkClicked keeps working verbatim. */
void QSPWebTextBox::OnScriptMessage(wxWebViewEvent& event)
{
    wxString message(event.GetString());
    if (message.IsEmpty()) return;

    if (message[0] == wxT('R'))
    {
        m_isShellReady = true;
        return;
    }
    if (message[0] == wxT('K'))
    {
        long code = 0, mods = 0;
        wxString payload(message.Mid(1));
        if (!payload.BeforeFirst(wxT('|')).ToLong(&code)) return;
        payload.AfterFirst(wxT('|')).ToLong(&mods);

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
    if (message[0] != wxT('L')) return;

    wxString href(message.Mid(1));

    wxMouseEvent mouseEvent(wxEVT_LEFT_UP);
    wxHtmlLinkInfo info(href, wxEmptyString);
    info.SetEvent(&mouseEvent);

    wxHtmlLinkEvent linkEvent(GetId(), info);
    linkEvent.SetEventObject(this);
    GetParent()->GetEventHandler()->ProcessEvent(linkEvent);
}
