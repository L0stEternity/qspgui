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
    m_isShellReady = false;
    m_toUseHtml = false;
    m_toScroll = false;
    m_font = *wxNORMAL_FONT;
    m_linkColor = *wxBLUE;
    m_backColor = wxPanel::GetBackgroundColour();
    m_fontColor = wxPanel::GetForegroundColour();

    m_isShellRequested = false;
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
            m_shellUrl = wxT("https://") QSP_SHELL_HOST wxT("/") QSP_SHELL_FILE;
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
        wxT("body{background-color:var(--qsp-bg);color:var(--qsp-fg);")
        wxT("font-family:var(--qsp-font);font-size:var(--qsp-size);")
        wxT("background-image:var(--qsp-bgimg);background-repeat:no-repeat;")
        wxT("background-position:center center;background-size:contain;")
        wxT("background-attachment:fixed;overflow-y:auto;overflow-x:hidden;}\n")
        wxT("a{color:var(--qsp-link);}\n")
        wxT("#qsp-content{padding:5px;}\n")
        wxT("img,video{max-width:100%;height:auto;}\n")
        wxT("</style></head>\n")
        wxT("<body><div id=\"qsp-content\"></div>\n")
        wxT("<script>\n")
        wxT("(function(){\n")
        wxT("var content=document.getElementById('qsp-content');\n")
        wxT("var base=document.getElementById('qsp-base');\n")
        /* The host channel can be a moment late on startup; an exception here
           would take the rest of the shell script down with it. */
        wxT("function qspPost(m){try{window.qspHost.postMessage(m);}catch(err){}}\n")
        wxT("window.qspSetBase=function(href){base.href=href;};\n")
        wxT("window.qspSetStyle=function(name,value){")
        wxT("document.documentElement.style.setProperty(name,value);};\n")
        /* $SETMAINDESCHEAD. Games written for a persistent-document player put
           their stylesheet here once and never repeat it, so it has to live in
           the head and survive the content rebuilds - dropping it back into the
           body would lose it on the very next refresh. */
        wxT("var qspHeadNodes=[],qspHeadHtml=null;\n")
        wxT("window.qspSetHead=function(html){\n")
        wxT("  if(qspHeadHtml===html)return;\n")
        wxT("  qspHeadHtml=html;\n")
        wxT("  for(var i=0;i<qspHeadNodes.length;i++){\n")
        wxT("    var o=qspHeadNodes[i];if(o.parentNode)o.parentNode.removeChild(o);}\n")
        wxT("  qspHeadNodes=[];\n")
        wxT("  var holder=document.createElement('div');\n")
        wxT("  holder.innerHTML=html;\n")
        wxT("  while(holder.firstChild){\n")
        wxT("    var n=holder.firstChild;holder.removeChild(n);\n")
        wxT("    document.head.appendChild(n);qspHeadNodes.push(n);}\n")
        wxT("};\n")
        /* Games rebuild their whole description on every refresh (the usual
           GOSUB chain), so the incoming HTML nearly always differs in some
           small way - a clock, a counter - while the bulk of it, images
           included, is unchanged. Assigning innerHTML would destroy and
           recreate every node, and a recreated <img> is re-fetched, re-decoded
           and re-laid-out; the browser paints the gap before it finishes. That
           is the flicker. So reconcile the existing tree against the new one
           and touch only what actually differs. */
        wxT("function qspSameNode(a,b){return a.nodeType===b.nodeType&&a.nodeName===b.nodeName;}\n")
        wxT("function qspMorphAttrs(t,s){\n")
        wxT("  var i,a,sa=s.attributes,ta=t.attributes;\n")
        /* Setting src to its current value still restarts a load, so compare first. */
        wxT("  for(i=sa.length-1;i>=0;i--){a=sa[i];\n")
        wxT("    if(t.getAttribute(a.name)!==a.value)t.setAttribute(a.name,a.value);}\n")
        wxT("  for(i=ta.length-1;i>=0;i--){a=ta[i];\n")
        wxT("    if(!s.hasAttribute(a.name))t.removeAttribute(a.name);}\n")
        wxT("}\n")
        wxT("function qspMorph(target,source){\n")
        wxT("  var tc=target.firstChild,sc=source.firstChild,tn,sn;\n")
        wxT("  while(sc){\n")
        wxT("    sn=sc.nextSibling;\n")
        wxT("    if(!tc){target.appendChild(sc);sc=sn;continue;}\n")
        wxT("    tn=tc.nextSibling;\n")
        wxT("    if(qspSameNode(tc,sc)){\n")
        wxT("      if(tc.nodeType===3||tc.nodeType===8){\n")
        wxT("        if(tc.nodeValue!==sc.nodeValue)tc.nodeValue=sc.nodeValue;\n")
        wxT("      }else{qspMorphAttrs(tc,sc);qspMorph(tc,sc);}\n")
        wxT("    }else{target.replaceChild(sc,tc);}\n")
        wxT("    tc=tn;sc=sn;\n")
        wxT("  }\n")
        wxT("  while(tc){tn=tc.nextSibling;target.removeChild(tc);tc=tn;}\n")
        wxT("}\n")
        wxT("var qspStage=document.createElement('div');\n")
        wxT("function qspMediaKey(el){\n")
        wxT("  var t=el.tagName;\n")
        wxT("  if(t!=='IMG'&&t!=='VIDEO'&&t!=='AUDIO'&&t!=='SOURCE')return null;\n")
        wxT("  return t+'|'+(el.getAttribute('src')||'');\n")
        wxT("}\n")
        /* Matching children by position alone is not enough: these games shift
           the node sequence constantly (a clock, a stat, an extra <br>), and a
           single insertion early on makes every later pair mismatch, so live
           images get thrown away and rebuilt. The tags carry no width/height,
           so a rebuilt image has no intrinsic size until it decodes - the
           layout collapses for a frame and snaps back. That is the blink.
           Match media by src instead and move the live element into place. */
        wxT("function qspReuseMedia(){\n")
        wxT("  var live={},i,k,keep,el;\n")
        wxT("  var have=content.querySelectorAll('img,video,audio,source');\n")
        wxT("  for(i=0;i<have.length;i++){k=qspMediaKey(have[i]);if(k&&!live[k])live[k]=have[i];}\n")
        wxT("  var want=qspStage.querySelectorAll('img,video,audio,source');\n")
        wxT("  for(i=0;i<want.length;i++){\n")
        wxT("    el=want[i];k=qspMediaKey(el);\n")
        wxT("    if(!k)continue;\n")
        wxT("    keep=live[k];\n")
        wxT("    if(!keep||!el.parentNode)continue;\n")
        wxT("    live[k]=null;\n") // never reuse the same live node twice
        wxT("    qspMorphAttrs(keep,el);\n")
        wxT("    el.parentNode.replaceChild(keep,el);\n")
        wxT("  }\n")
        wxT("}\n")
        wxT("function qspApply(html,toBottom){\n")
        wxT("  qspStage.innerHTML=html;\n")
        wxT("  qspReuseMedia();\n")
        wxT("  qspMorph(content,qspStage);\n")
        wxT("  document.body.scrollTop=toBottom?document.body.scrollHeight:0;\n")
        wxT("  document.documentElement.scrollTop=toBottom?document.documentElement.scrollHeight:0;\n")
        wxT("}\n")
        /* A single action can drive several refreshes through its GOSUB chain,
           and each one painted separately is a separate visible blink. Collapse
           a burst into the final state. A timer rather than requestAnimationFrame
           so updates still land while the pane is hidden or occluded. */
        wxT("var qspPending=null,qspTimer=0;\n")
        wxT("window.qspSetContent=function(html,toBottom){\n")
        wxT("  qspPending={h:html,b:toBottom};\n")
        wxT("  if(qspTimer)return;\n")
        wxT("  qspTimer=setTimeout(function(){\n")
        wxT("    qspTimer=0;var p=qspPending;qspPending=null;\n")
        wxT("    if(p)qspApply(p.h,p.b);\n")
        wxT("  },16);\n")
        wxT("};\n")
        wxT("window.qspScrollTo=function(anchor){\n")
        wxT("  var el=anchor?document.getElementById(anchor):null;\n")
        wxT("  if(!el&&anchor){var n=document.getElementsByName(anchor);el=n.length?n[0]:null;}\n")
        wxT("  if(el)el.scrollIntoView(true);else document.documentElement.scrollTop=0;\n")
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
               name over DNS and its requests just hang. Reloading the shell
               happens once per game load, not per location. */
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

void QSPWebTextBox::PushContent()
{
    wxString text(QSPTools::HtmlizeWhitespaces(m_toUseHtml ? m_text : QSPTools::ProceedAsPlain(m_text)));
    RunScript(wxString::Format(wxT("qspSetContent(%s,%s);"),
        ToJsString(text).wx_str(),
        m_toScroll ? wxT("true") : wxT("false")));
}

void QSPWebTextBox::PushStyle()
{
    wxString script;
    script << wxT("qspSetStyle('--qsp-bg',") << ToJsString(ToCssColor(m_backColor)) << wxT(");");
    script << wxT("qspSetStyle('--qsp-fg',") << ToJsString(ToCssColor(m_fontColor)) << wxT(");");
    script << wxT("qspSetStyle('--qsp-link',") << ToJsString(ToCssColor(m_linkColor)) << wxT(");");
    script << wxT("qspSetStyle('--qsp-font',") << ToJsString(m_font.GetFaceName()) << wxT(");");
    script << wxT("qspSetStyle('--qsp-size',") << ToJsString(wxString::Format(wxT("%dpt"), m_font.GetPointSize())) << wxT(");");

    wxString bgImage(wxT("none"));
    if (!m_backImagePath.IsEmpty())
    {
        /* Resolved against the document base, same as any other game asset. */
        wxString escaped(m_backImagePath);
        escaped.Replace(wxT("'"), wxT("\\'"));
        bgImage = wxT("url('") + escaped + wxT("')");
    }
    script << wxT("qspSetStyle('--qsp-bgimg',") << ToJsString(bgImage) << wxT(");");

    RunScript(script);
}

void QSPWebTextBox::PushHead()
{
    RunScript(wxString::Format(wxT("qspSetHead(%s);"), ToJsString(m_headContent).wx_str()));
}

/* Games assign $SETMAINDESCHEAD once and clear the flag that produced it, so
   the markup must be kept rather than re-requested. */
void QSPWebTextBox::SetHeadContent(const wxString& head)
{
    if (m_headContent != head)
    {
        m_headContent = head;
        PushHead();
    }
}

void QSPWebTextBox::RefreshUI()
{
    PushStyle();
    PushHead();
    PushContent();
}

void QSPWebTextBox::SetIsHtml(bool isHtml)
{
    if (m_toUseHtml != isHtml)
    {
        m_toUseHtml = isHtml;
        PushContent();
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
        PushContent();
    }
}

void QSPWebTextBox::LoadBackImage(const wxString& imagePath)
{
    if (m_backImagePath != imagePath)
    {
        m_backImagePath = imagePath;
        SetupGameFolderAccess();
        PushStyle();
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
        PushStyle();
    }
}

void QSPWebTextBox::SetLinkColor(const wxColour& clr)
{
    m_linkColor = clr;
    PushStyle();
}

bool QSPWebTextBox::SetBackgroundColour(const wxColour& colour)
{
    m_backColor = colour;
    /* The panel itself shows through while the browser is still coming up
       and during resizes, so it has to carry the same colour. */
    bool result = wxPanel::SetBackgroundColour(colour);
    if (m_view) m_view->SetBackgroundColour(colour);
    PushStyle();
    return result;
}

bool QSPWebTextBox::SetForegroundColour(const wxColour& colour)
{
    m_fontColor = colour;
    bool result = wxPanel::SetForegroundColour(colour);
    PushStyle();
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
    RefreshUI();
}

/* Only the shell may ever load here. A navigation would destroy the document
   we keep mutating, which is exactly the flicker this port exists to remove,
   so links go through the script message channel instead. */
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
