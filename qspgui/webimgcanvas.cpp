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

#include "webimgcanvas.h"
#include "comtools.h"

#include <wx/filename.h>

#define QSP_IMG_SHELL_FILE wxT("qspgui_image.html")

wxIMPLEMENT_CLASS(QSPWebImgCanvas, QSPWebPane);

QSPWebImgCanvas::QSPWebImgCanvas(wxWindow *parent, wxWindowID id) :
    QSPWebPane(parent, id)
{
    m_backColor = wxPanel::GetBackgroundColour();
    SetPaneName(wxT("image"));
    InitShell(QSP_IMG_SHELL_FILE, BuildShellDocument());
}

/* object-fit:contain is the whole of what QSPImgCanvas does by hand - decode,
   measure, scale to fit, centre, and redo it all on every resize. Doing it in
   CSS also means it keeps working for a format wxImage has never heard of. */
wxString QSPWebImgCanvas::BuildShellDocument()
{
    static const wxChar *document =
        wxT("<!DOCTYPE html>\n")
        wxT("<html><head><meta charset=\"utf-8\">\n")
        wxT("<base id=\"qsp-base\" href=\"\">\n")
        wxT("<style>\n")
        wxT(":root{--qsp-bg:#e0e0e0;}\n")
        wxT("html,body{margin:0;padding:0;height:100%;overflow:hidden;}\n")
        wxT("body{background-color:var(--qsp-bg);}\n")
        /* The incoming picture is built hidden on top of the current one and
           they trade places once it has decoded, for the same reason the
           description pane has two layers: replacing a src in place tears the
           old picture down first and the browser paints the gap. */
        wxT(".qsp-img{position:absolute;left:0;top:0;width:100%;height:100%;")
        wxT("object-fit:contain;object-position:center;}\n")
        wxT(".qsp-hidden{visibility:hidden;}\n")
        wxT("</style>\n")
        /* A game's stylesheets are inserted just above this element and its own
           inline CSS goes inside it, so both always win over the rules above. */
        wxT("<style id=\"qsp-user-css\"></style>\n")
        wxT("</head>\n")
        wxT("<body>\n")
        wxT("<script>\n")
        wxT("(function(){\n")
        wxT("var base=document.getElementById('qsp-base');\n")
        wxT("var front=null,token=0;\n")
        wxT("var videoExt=/\\.(webm|mp4|m4v|ogv|mov)(\\?|#|$)/i;\n")
        wxT("function qspPost(m){try{window.qspHost.postMessage(m);return true;}")
        wxT("catch(err){return false;}}\n")
        wxT("window.qspSetBase=function(href){base.href=href;};\n")
        wxT("window.qspSetPane=function(){};\n")
        wxT("window.qspSetStyle=function(bg){\n")
        wxT("  document.documentElement.style.setProperty('--qsp-bg',bg);\n")
        wxT("};\n")
        /* Declarative, like the other panes: the whole current list every time,
           rebuilt only when it changed. */
        wxT("var userStyle=document.getElementById('qsp-user-css');\n")
        wxT("var cssNodes=[],cssKey=null;\n")
        wxT("window.qspSetStyles=function(css,files){\n")
        wxT("  var key=files.join('\\n'),i,link;\n")
        wxT("  if(key!==cssKey){\n")
        wxT("    cssKey=key;\n")
        wxT("    for(i=0;i<cssNodes.length;i++)")
        wxT("if(cssNodes[i].parentNode)cssNodes[i].parentNode.removeChild(cssNodes[i]);\n")
        wxT("    cssNodes=[];\n")
        wxT("    for(i=0;i<files.length;i++){\n")
        wxT("      link=document.createElement('link');\n")
        wxT("      link.rel='stylesheet';link.href=files[i];\n")
        wxT("      document.head.insertBefore(link,userStyle);\n")
        wxT("      cssNodes.push(link);\n")
        wxT("    }\n")
        wxT("  }\n")
        wxT("  if(userStyle.textContent!==css)userStyle.textContent=css;\n")
        wxT("};\n")
        /* Every call gets its own element, so nothing a stale call left behind -
           a handler, a pending swap, a half-loaded src - can touch the picture
           on screen. A cached image both reports complete and fires load, so the
           swap is armed twice and has to be one-shot: the old version reused two
           fixed elements and a second swap hid the picture it had just shown. */
        wxT("function qspDrop(el){\n")
        wxT("  if(!el)return;\n")
        wxT("  el.onload=el.onerror=el.onloadeddata=null;\n")
        wxT("  if(el.tagName==='VIDEO'){try{el.pause();}catch(e){}\n")
        wxT("    el.removeAttribute('src');try{el.load();}catch(e){}}\n")
        wxT("  else el.removeAttribute('src');\n")
        wxT("  if(el.parentNode)el.parentNode.removeChild(el);\n")
        wxT("}\n")
        wxT("var pending=null;\n")
        wxT("window.qspShow=function(src){\n")
        wxT("  var mine=++token;\n")
        /* A newer picture has been asked for; whatever was still loading goes. */
        wxT("  qspDrop(pending);pending=null;\n")
        wxT("  if(!src){qspDrop(front);front=null;return;}\n")
        wxT("  var el,done=false;\n")
        wxT("  if(videoExt.test(src)){\n")
        /* A picture, not a clip: silent and looping, like an animated image.
           Muted is also what lets it autoplay without a user gesture. */
        wxT("    el=document.createElement('video');\n")
        wxT("    el.muted=true;el.loop=true;el.autoplay=true;el.playsInline=true;\n")
        wxT("    el.setAttribute('muted','');el.className='qsp-img qsp-video qsp-hidden';\n")
        wxT("  }else{\n")
        wxT("    el=document.createElement('img');\n")
        wxT("    el.alt='';el.className='qsp-img qsp-hidden';\n")
        wxT("  }\n")
        wxT("  var swap=function(){\n")
        wxT("    if(done||mine!==token)return;\n")
        wxT("    done=true;pending=null;\n")
        wxT("    el.onload=el.onerror=el.onloadeddata=null;\n")
        wxT("    el.classList.remove('qsp-hidden');\n")
        wxT("    var old=front;front=el;\n")
        wxT("    qspDrop(old);\n")
        wxT("    if(el.tagName==='VIDEO'){var p=el.play();if(p&&p.catch)p.catch(function(){});}\n")
        wxT("  };\n")
        wxT("  var arm=function(){requestAnimationFrame(swap);};\n")
        /* rAF does not run while the pane is hidden, and a picture asked for
           then must still be there when the pane comes back. */
        wxT("  var armNow=function(){if(document.hidden)swap();else arm();};\n")
        wxT("  if(el.tagName==='VIDEO')el.onloadeddata=el.onerror=armNow;\n")
        wxT("  else el.onload=el.onerror=armNow;\n")
        wxT("  pending=el;\n")
        wxT("  document.body.appendChild(el);\n")
        wxT("  el.src=src;\n")
        /* A cached picture is complete the moment src is assigned and may
           never fire load, so the swap has to be armed for that too. */
        wxT("  if(el.tagName==='IMG'&&el.complete)armNow();\n")
        wxT("};\n");

    wxString shell(document);
    shell << QSPWebPane::GetInputScript() << QSPWebPane::GetDiagnosticsScript();
    return shell;
}

/* Answers whether there is anything to show, because the caller uses it to
   decide whether to reveal the pane at all - so it cannot wait for the browser
   to fetch the file. Existence is what it can tell synchronously; a file that
   exists but will not decode comes back later as a resource diagnostic, the
   same way a broken image in a description does. */
bool QSPWebImgCanvas::OpenFile(const wxString& fullPath)
{
    wxString path(fullPath);
    if (!path.IsEmpty() && !wxFileExists(path)) path.Clear();
    if (path == m_path) return !m_path.IsEmpty();

    m_path = path;
    Apply();
    return !m_path.IsEmpty();
}

void QSPWebImgCanvas::Apply()
{
    if (!IsShellReady()) return;

    wxString src;
    if (!m_path.IsEmpty())
    {
        /* Relative to the game folder, so it resolves against the document
           base the way every other game asset does - which is what keeps it
           on the virtual host rather than on file://. */
        wxFileName file(m_path);
        wxString gameDir(GetPathProvider() ? GetPathProvider()->GetGamePath() : wxString());
        if (!gameDir.IsEmpty() && file.MakeRelativeTo(gameDir))
            src = file.GetFullPath(wxPATH_UNIX);
        else
            src = QSPWebUtil::ToFileUrl(m_path);
    }
    RunScript(wxString::Format(wxT("qspShow(%s);"), QSPWebUtil::ToJsString(src)));
}

void QSPWebImgCanvas::RefreshUI()
{
    RunScript(wxString::Format(wxT("qspSetStyle(%s);"),
                               QSPWebUtil::ToJsString(QSPWebUtil::ToCssColor(m_backColor))));
}

wxString QSPWebImgCanvas::BuildUserStylesScript() const
{
    return wxString::Format(wxT("qspSetStyles(%s,%s);"),
                            QSPWebUtil::ToJsString(m_userCss),
                            QSPWebUtil::ToJsArray(m_userCssFiles));
}

void QSPWebImgCanvas::SetUserStyles(const wxString& inlineCss, const wxArrayString& files)
{
    if (m_userCss == inlineCss && m_userCssFiles == files) return;

    m_userCss = inlineCss;
    m_userCssFiles = files;
    RunScript(BuildUserStylesScript());
}

/* A fresh document knows nothing of what came before it */
void QSPWebImgCanvas::OnShellReady()
{
    RefreshUI();
    RunScript(BuildUserStylesScript());
    Apply();
}

bool QSPWebImgCanvas::SetBackgroundColour(const wxColour& color)
{
    m_backColor = color;
    /* The browser's own pre-paint colour too, so a resize does not flash the
       default white before the document repaints. */
    if (GetView()) GetView()->SetBackgroundColour(color);
    QSPWebPane::SetBackgroundColour(color);
    RefreshUI();
    return true;
}
