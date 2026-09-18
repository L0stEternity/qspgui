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
#include <wx/arrstr.h>

/* One shell document per kind of pane, all served from the directory behind
   the shell host that QSPWebPane sets up. */
#define QSP_SHELL_FILE wxT("qspgui_shell.html")

wxIMPLEMENT_CLASS(QSPWebTextBox, QSPWebPane);
wxDEFINE_EVENT(wxEVT_QSP_SCRIPT_CALL, QSPScriptCallEvent);

/* The conversions all panes share now live on QSPWebUtil; these keep the call
   sites below reading the way they did. */
static inline wxString ToJsString(const wxString& str) { return QSPWebUtil::ToJsString(str); }
static inline wxString ToJsArray(const wxArrayString& items) { return QSPWebUtil::ToJsArray(items); }
static inline wxString FromUriComponent(const wxString& str) { return QSPWebUtil::FromUriComponent(str); }
static inline wxString ToCssColor(const wxColour& color) { return QSPWebUtil::ToCssColor(color); }

QSPWebTextBox::QSPWebTextBox(wxWindow *parent, wxWindowID id) :
    QSPWebPane(parent, id)
{
    m_isUpdatePending = false;
    m_updateDepth = 0;
    m_toUseHtml = false;
    m_toScroll = false;
    m_font = *wxNORMAL_FONT;
    m_linkColor = *wxBLUE;
    m_backColor = wxPanel::GetBackgroundColour();
    m_fontColor = wxPanel::GetForegroundColour();

    /* The base has already created the view and started about:blank; this is
       what it will load once the backend is up. */
    InitShell(QSP_SHELL_FILE, BuildShellDocument());
}







/* The document this pane runs. Handed to the base, which writes it out and
   serves it: written once per run rather than shipped as a data file, so the
   renderer stays self-contained and cannot get out of sync with the binary.
   It is split into several literals only because MSVC caps the length of a
   single one. */
wxString QSPWebTextBox::BuildShellDocument()
{
    static const wxChar *shellDocument =
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
        wxT("</style>\n")
        /* The game's stylesheets are inserted just above this element and its
           own inline CSS goes inside it, so both always win over the built-in
           rules and the inline block always wins over the files. */
        wxT("<style id=\"qsp-user-css\"></style>\n")
        wxT("</head>\n")
        wxT("<body>\n")
        wxT("<div id=\"qsp-l0\" class=\"qsp-layer\"></div>\n")
        wxT("<div id=\"qsp-l1\" class=\"qsp-layer qsp-hidden\"></div>\n")
        wxT("<script>\n")
        wxT("(function(){\n");

    static const wxChar *shellCore =
        wxT("var layers=[document.getElementById('qsp-l0'),document.getElementById('qsp-l1')];\n")
        wxT("var base=document.getElementById('qsp-base');\n")
        wxT("var active=0,token=0,refreshHooks=[];\n")
        /* The host channel can be a moment late on startup; an exception here
           would take the rest of the shell script down with it. */
        wxT("function qspPost(m){try{window.qspHost.postMessage(m);return true;}")
        wxT("catch(err){return false;}}\n")
        wxT("window.qspSetBase=function(href){base.href=href;};\n")
        wxT("function applyStyle(s){\n")
        wxT("  if(!s)return;\n")
        wxT("  var r=document.documentElement.style;\n")
        wxT("  r.setProperty('--qsp-bg',s.bg);r.setProperty('--qsp-fg',s.fg);\n")
        wxT("  r.setProperty('--qsp-link',s.link);r.setProperty('--qsp-font',s.font);\n")
        wxT("  r.setProperty('--qsp-size',s.size);r.setProperty('--qsp-bgimg',s.bgimg);\n")
        wxT("}\n")
        /* innerHTML never runs a <script>, so a description written in HTML
           mode would silently drop its code. Re-creating each element makes it
           execute, and doing it once the layer is on screen means the code can
           measure and alter what the player is actually looking at. */
        wxT("function runScripts(root){\n")
        wxT("  var list=root.querySelectorAll('script'),i,j,old,el,attr;\n")
        wxT("  for(i=0;i<list.length;i++){\n")
        wxT("    old=list[i];\n")
        wxT("    if(!old.parentNode)continue;\n")
        wxT("    el=document.createElement('script');\n")
        wxT("    for(j=0;j<old.attributes.length;j++){attr=old.attributes[j];")
        wxT("el.setAttribute(attr.name,attr.value);}\n")
        wxT("    el.text=old.textContent;\n")
        wxT("    old.parentNode.replaceChild(el,old);\n")
        wxT("  }\n")
        wxT("}\n")
        /* One hook throwing must not stop the others, but it must not vanish
           either - qspReport is hoisted from the diagnostics block below. */
        wxT("function notifyRefresh(){\n")
        wxT("  for(var i=0;i<refreshHooks.length;i++){\n")
        wxT("    try{refreshHooks[i]();}\n")
        wxT("    catch(err){qspReport('e',(err&&err.stack)?err.stack:String(err),'onRefresh');}\n")
        wxT("  }\n")
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
        wxT("    runScripts(back);\n")
        wxT("    notifyRefresh();\n")
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
        wxT("};\n");

    static const wxChar *shellAssets =
        /* Stylesheets are fully declarative: the host sends the whole current
           list every time and this rebuilds it only when it actually changed,
           so a refresh never re-fetches a sheet that is already applied. */
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
        /* Scripts are not: running the same code again on every refresh would
           be wrong, so a script runs when it appears and when its text or its
           file list changes, and never otherwise. */
        wxT("var jsNodes=[],jsKey=null,jsInline=null,inlineNode=null;\n")
        wxT("function runInline(code){\n")
        wxT("  if(inlineNode&&inlineNode.parentNode)")
        wxT("inlineNode.parentNode.removeChild(inlineNode);\n")
        wxT("  inlineNode=null;\n")
        wxT("  if(!code)return;\n")
        /* An appended element runs in global scope, unlike eval() in here, so
           the game's own top-level declarations end up where it expects. */
        wxT("  var el=document.createElement('script');\n")
        wxT("  el.text=code;\n")
        wxT("  document.head.appendChild(el);\n")
        wxT("  inlineNode=el;\n")
        wxT("}\n")
        wxT("window.qspSetScripts=function(code,files){\n")
        wxT("  var key=files.join('\\n'),i,el,pending=0,done=false;\n")
        wxT("  var finish=function(){\n")
        wxT("    if(done)return;\n")
        wxT("    done=true;\n")
        wxT("    if(code!==jsInline){jsInline=code;runInline(code);}\n")
        wxT("  };\n")
        wxT("  if(key!==jsKey){\n")
        wxT("    jsKey=key;\n")
        /* The inline block nearly always calls into the files, so it has to
           re-run when they do - and wait for them, because a script inserted
           from code is async unless we say otherwise. */
        wxT("    jsInline=null;\n")
        wxT("    for(i=0;i<jsNodes.length;i++)")
        wxT("if(jsNodes[i].parentNode)jsNodes[i].parentNode.removeChild(jsNodes[i]);\n")
        wxT("    jsNodes=[];\n")
        wxT("    for(i=0;i<files.length;i++){\n")
        wxT("      el=document.createElement('script');\n")
        wxT("      el.async=false;\n")
        wxT("      ++pending;\n")
        wxT("      el.onload=el.onerror=function(){if(--pending===0)finish();};\n")
        wxT("      el.src=files[i];\n")
        wxT("      document.head.appendChild(el);\n")
        wxT("      jsNodes.push(el);\n")
        wxT("    }\n")
        wxT("  }\n")
        wxT("  if(!pending)finish();\n")
        wxT("};\n");

    static const wxChar *shellBridge =
        /* Everything the game's JS asks of the engine is a round trip through
           the host, so every call answers with a promise. */
        wxT("var callSeq=0,calls={};\n")
        wxT("window.qspResolve=function(id,isOk,value,isNum){\n")
        wxT("  var call=calls[id];\n")
        wxT("  if(!call)return;\n")
        wxT("  delete calls[id];\n")
        wxT("  if(isOk)call.res(isNum?Number(value):value);else call.rej(new Error(value));\n")
        wxT("};\n")
        wxT("function hostCall(op,args){\n")
        wxT("  return new Promise(function(res,rej){\n")
        wxT("    var id=++callSeq,parts=['C'+id,op],i,a;\n")
        wxT("    for(i=0;i<args.length;i++){\n")
        wxT("      a=args[i];\n")
        wxT("      parts.push(encodeURIComponent(a===undefined||a===null?'':String(a)));\n")
        wxT("    }\n")
        wxT("    calls[id]={res:res,rej:rej};\n")
        /* Never leave a promise pending because the channel was not there. */
        wxT("    if(!qspPost(parts.join('|'))){\n")
        wxT("      delete calls[id];\n")
        wxT("      rej(new Error('QSP host is unavailable'));\n")
        wxT("    }\n")
        wxT("  });\n")
        wxT("}\n")
        wxT("window.qsp={\n")
        /* 'main' or 'vars': the panes are separate documents and the game's JS
           runs in both, so this is how a script tells which one it is in. */
        wxT("  pane:'main',\n")
        wxT("  exec:function(code){return hostCall('exec',[code]);},\n")
        wxT("  eval:function(expr){return hostCall('eval',[expr]);},\n")
        wxT("  evalNum:function(expr){return hostCall('evalnum',[expr]);},\n")
        wxT("  execLoc:function(name){return hostCall('loc',[name]);},\n")
        wxT("  getVar:function(name,index){")
        wxT("return hostCall('get',[name,index===undefined?0:index]);},\n")
        /* setVar(name, value) writes item 0; setVar(name, index, value) writes
           the item at a number or a string key, exactly like $name[index]. */
        wxT("  setVar:function(name,a,b){\n")
        wxT("    return arguments.length<3?hostCall('set',[name,0,a])")
        wxT(":hostCall('set',[name,a,b]);\n")
        wxT("  },\n")
        wxT("  addVar:function(name,value){return hostCall('add',[name,value]);},\n")
        wxT("  getVarSize:function(name){return hostCall('size',[name]);},\n")
        wxT("  indexOf:function(name,key){return hostCall('index',[name,key]);},\n")
        wxT("  onRefresh:function(fn){if(typeof fn==='function')refreshHooks.push(fn);},\n")
        wxT("  offRefresh:function(fn){\n")
        wxT("    var i=refreshHooks.indexOf(fn);\n")
        wxT("    if(i>=0)refreshHooks.splice(i,1);\n")
        wxT("  }\n")
        wxT("};\n")
        wxT("window.qspSetPane=function(name){window.qsp.pane=name;};\n")
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
        wxT("");

    wxString shell;
    shell << shellDocument << shellCore << shellAssets << shellBridge;
    /* Key forwarding, the context-menu policy and the diagnostics hook are the
       same in every pane, so they come from the base - which also closes the
       document's IIFE and announces that the shell is live. */
    shell << QSPWebPane::GetInputScript() << QSPWebPane::GetDiagnosticsScript();
    return shell;
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

wxString QSPWebTextBox::BuildUserStylesScript() const
{
    return wxString::Format(wxT("qspSetStyles(%s,%s);"),
        ToJsString(m_userCss).wx_str(),
        ToJsArray(m_userCssFiles).wx_str());
}

wxString QSPWebTextBox::BuildUserScriptsScript() const
{
    return wxString::Format(wxT("qspSetScripts(%s,%s);"),
        ToJsString(m_userJs).wx_str(),
        ToJsArray(m_userJsFiles).wx_str());
}

/* The host always sends the whole current state and lets the document work out
   whether anything changed, because a game folder switch reloads the shell and
   any bookkeeping kept out here would then be one step ahead of the page. */
void QSPWebTextBox::SetUserStyles(const wxString& inlineCss, const wxArrayString& files)
{
    if (m_userCss == inlineCss && m_userCssFiles == files) return;
    m_userCss = inlineCss;
    m_userCssFiles = files;
    RunScript(BuildUserStylesScript());
}

void QSPWebTextBox::SetUserScripts(const wxString& inlineJs, const wxArrayString& files)
{
    if (m_userJs == inlineJs && m_userJsFiles == files) return;
    m_userJs = inlineJs;
    m_userJsFiles = files;
    RunScript(BuildUserScriptsScript());
}

void QSPWebTextBox::ResolveScriptCall(long callId, bool isOk, const wxString& value, bool isNum)
{
    RunScript(wxString::Format(wxT("qspResolve(%ld,%s,%s,%s);"),
        callId,
        isOk ? wxT("true") : wxT("false"),
        ToJsString(value).wx_str(),
        isNum ? wxT("true") : wxT("false")));
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


/* The document is new, so its own idea of what is already applied is empty:
   hand it everything before the first content update, so the game's styles are
   in place by the time anything is painted. */
void QSPWebTextBox::OnShellReady()
{
    RunScript(BuildUserStylesScript());
    RunScript(BuildUserScriptsScript());

    m_isUpdatePending = true;
    Flush();
}

/* Everything the base does not claim for itself. Links are re-emitted as the
   same wxHtmlLinkEvent the classic renderer produces, so
   QSPFrame::OnLinkClicked keeps working verbatim. */
void QSPWebTextBox::OnPaneMessage(const wxString& message)
{
    if (message[0] == wxT('C'))
    {
        /* "C<id>|<op>|<arg>|<arg>...", every argument percent-encoded so no
           payload can ever contain the separator. */
        wxArrayString parts(wxSplit(message.Mid(1), wxT('|'), (wxChar)0));
        long callId = 0;
        if (parts.GetCount() < 2 || !parts[0].ToLong(&callId)) return;

        wxArrayString args;
        for (size_t i = 2; i < parts.GetCount(); ++i)
            args.Add(FromUriComponent(parts[i]));

        QSPScriptCallEvent *callEvent = new QSPScriptCallEvent(wxEVT_QSP_SCRIPT_CALL, GetId());
        callEvent->SetCallId(callId);
        callEvent->SetOp(parts[1]);
        callEvent->SetArgs(args);
        callEvent->SetEventObject(this);
        /* Queued, not sent: running engine code from inside the browser's own
           message callback would re-enter the view while it is mid-dispatch.

           Queued on the pane itself and left to propagate: a command event
           climbs the parent chain until something handles it, so a pane
           sitting inside a message dialog reaches the frame just the same.
           Handing it straight to the parent would strand every call such a
           pane makes, and a call nobody answers is a promise the game waits
           on for good. */
        QueueEvent(callEvent);
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
