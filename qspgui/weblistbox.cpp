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

#include "weblistbox.h"
#include "comtools.h"

#include <wx/filename.h>
#include <wx/html/htmlwin.h>

#define QSP_LIST_SHELL_FILE wxT("qspgui_list.html")

wxIMPLEMENT_CLASS(QSPWebListBox, QSPWebPane);

QSPWebListBox::QSPWebListBox(wxWindow *parent, wxWindowID id, ListBoxType type) :
    QSPWebPane(parent, id)
{
    m_type = type;
    m_toUseHtml = false;
    m_toShowNums = false;
    m_selection = wxNOT_FOUND;
    m_itemsGen = 0;
    m_font = *wxNORMAL_FONT;
    m_linkColor = *wxBLUE;
    m_backColor = wxPanel::GetBackgroundColour();
    m_fontColor = wxPanel::GetForegroundColour();

    /* Both list kinds share one document; the behaviour that differs is
       decided per event from the mode the host sets. One shell file means one
       cached document rather than two. */
    InitShell(QSP_LIST_SHELL_FILE, BuildShellDocument());
    SetPaneName(type == LB_EXTENDED ? wxT("actions") : wxT("objects"));
}

/* ------------------------------------------------------------------ */
/* The document                                                        */
/* ------------------------------------------------------------------ */

wxString QSPWebListBox::BuildShellDocument()
{
    static const wxChar *documentHead =
        wxT("<!DOCTYPE html>\n")
        wxT("<html><head><meta charset=\"utf-8\">\n")
        wxT("<base id=\"qsp-base\" href=\"\">\n")
        wxT("<style>\n")
        wxT(":root{--qsp-bg:#e0e0e0;--qsp-fg:#000000;--qsp-link:#0000ff;")
        wxT("--qsp-font:sans-serif;--qsp-size:12pt;")
        wxT("--qsp-sel-bg:#99c9ef;--qsp-sel-fg:#000000;}\n")
        wxT("html,body{margin:0;padding:0;height:100%;}\n")
        wxT("body{background-color:var(--qsp-bg);color:var(--qsp-fg);")
        wxT("font-family:var(--qsp-font);font-size:var(--qsp-size);")
        wxT("overflow-x:hidden;overflow-y:auto;")
        /* A list is chrome, not prose: selecting its text with the mouse only
           ever gets in the way of clicking the item. */
        wxT("-webkit-user-select:none;user-select:none;}\n")
        wxT("#qsp-list{display:block;padding:2px 0;}\n")
        wxT(".qsp-item{display:flex;align-items:center;gap:6px;")
        wxT("padding:3px 6px;cursor:pointer;}\n")
        /* The row's own icon only: a picture inside the item's HTML keeps the
           size the game gave it, as it would in the main description. */
        wxT(".qsp-item>img{max-height:2.5em;max-width:25%;height:auto;flex:0 0 auto;}\n")
        wxT(".qsp-text img,.qsp-text video{max-width:100%;}\n")
        wxT(".qsp-text img:not([height]),.qsp-text video:not([height]){height:auto;}\n")
        QSP_LEGACY_FONT_SIZES
        wxT(".qsp-num{flex:0 0 auto;opacity:0.65;}\n")
        wxT(".qsp-text{flex:1 1 auto;min-width:0;overflow-wrap:break-word;}\n")
        /* The selection colours are worked out by the host from the page's own
           ones; see BuildStyleScript for why they cannot simply be the
           desktop's Highlight and HighlightText. */
        wxT(".qsp-item.qsp-sel{background:var(--qsp-sel-bg);color:var(--qsp-sel-fg);}\n")
        wxT(".qsp-item.qsp-sel a{color:var(--qsp-sel-fg);}\n")
        wxT(".qsp-item a{color:var(--qsp-link);}\n")
        /* A game's stylesheet can restyle the lists too; it is inserted here
           so it always wins over the rules above. */
        wxT("</style>\n")
        wxT("<style id=\"qsp-user-css\"></style>\n")
        wxT("</head>\n")
        wxT("<body><div id=\"qsp-list\"></div>\n")
        wxT("<script>\n")
        wxT("(function(){\n");

    static const wxChar *documentCore =
        wxT("var list=document.getElementById('qsp-list');\n")
        wxT("var base=document.getElementById('qsp-base');\n")
        /* gen is the number of the list on screen, sent back with every click
           so the host can tell a click on a list it has already replaced */
        wxT("var sel=-1,extended=false,gen=0;\n")
        wxT("function qspPost(m){try{window.qspHost.postMessage(m);return true;}")
        wxT("catch(err){return false;}}\n")
        wxT("window.qspSetBase=function(href){base.href=href;};\n")
        wxT("window.qspSetPane=function(name){extended=(name==='actions');};\n")
        wxT("window.qspSetStyle=function(s){\n")
        wxT("  var r=document.documentElement.style;\n")
        wxT("  r.setProperty('--qsp-bg',s.bg);r.setProperty('--qsp-fg',s.fg);\n")
        wxT("  r.setProperty('--qsp-link',s.link);r.setProperty('--qsp-font',s.font);\n")
        wxT("  r.setProperty('--qsp-size',s.size);\n")
        wxT("  r.setProperty('--qsp-sel-bg',s.selbg);r.setProperty('--qsp-sel-fg',s.selfg);\n")
        wxT("};\n")
        wxT("function paintSelection(){\n")
        wxT("  var nodes=list.children,i;\n")
        wxT("  for(i=0;i<nodes.length;i++)\n")
        wxT("    nodes[i].classList.toggle('qsp-sel',i===sel);\n")
        wxT("}\n")
        wxT("window.qspSelect=function(index){\n")
        wxT("  if(index===sel)return;\n")
        wxT("  sel=index;\n")
        wxT("  paintSelection();\n")
        wxT("};\n")
        /* items is [{img,text},...]; the host has already decided whether the
           text is the game's HTML or an escaped plain string. */
        wxT("window.qspSetItems=function(items,showNums,itemsGen){\n")
        wxT("  var frag=document.createDocumentFragment(),i,row,num,img,text;\n")
        wxT("  gen=itemsGen;\n")
        wxT("  for(i=0;i<items.length;i++){\n")
        wxT("    row=document.createElement('div');\n")
        wxT("    row.className='qsp-item';\n")
        wxT("    row.setAttribute('data-index',i);\n")
        wxT("    if(showNums&&i<9){\n")
        wxT("      num=document.createElement('span');\n")
        wxT("      num.className='qsp-num';\n")
        wxT("      num.textContent='['+(i+1)+']';\n")
        wxT("      row.appendChild(num);\n")
        wxT("    }\n")
        wxT("    if(items[i].img){\n")
        wxT("      img=document.createElement('img');\n")
        wxT("      img.src=items[i].img;\n")
        wxT("      img.alt='';\n")
        wxT("      row.appendChild(img);\n")
        wxT("    }\n")
        wxT("    text=document.createElement('span');\n")
        wxT("    text.className='qsp-text';\n")
        wxT("    text.innerHTML=items[i].text;\n")
        wxT("    row.appendChild(text);\n")
        wxT("    frag.appendChild(row);\n")
        wxT("  }\n")
        wxT("  list.textContent='';\n")
        wxT("  list.appendChild(frag);\n")
        wxT("  if(sel>=items.length)sel=-1;\n")
        wxT("  paintSelection();\n")
        wxT("  list.scrollTop=0;\n")
        wxT("};\n")
        /* Fully declarative, exactly as in the description panes: the host
           sends the whole current list every time and this rebuilds it only
           when it actually changed, so a refresh never re-fetches a sheet that
           is already applied. The inline block sits after the files, so it
           wins over them. */
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
        wxT("};\n");

    static const wxChar *documentInput =
        wxT("function rowIndex(node){\n")
        wxT("  while(node&&node!==list&&!node.classList.contains('qsp-item'))")
        wxT("node=node.parentElement;\n")
        wxT("  if(!node||node===list)return -1;\n")
        wxT("  var value=parseInt(node.getAttribute('data-index'),10);\n")
        wxT("  return isNaN(value)?-1:value;\n")
        wxT("}\n")
        /* The action list selects on hover and runs on a single click; the
           object list selects on click. That is what the classic renderer
           does, and readers are used to it. */
        wxT("list.addEventListener('mousemove',function(e){\n")
        wxT("  if(!extended)return;\n")
        wxT("  var index=rowIndex(e.target);\n")
        wxT("  if(index<0||index===sel)return;\n")
        wxT("  sel=index;\n")
        wxT("  paintSelection();\n")
        wxT("  qspPost('S'+gen+'|'+index);\n")
        wxT("},false);\n")
        /* Running an action always says which row first. The host runs the
           engine's selected action, and the row painted here is not always
           that one - a hover the host had to drop while the game was busy
           leaves them apart - so it is set again, which costs nothing when it
           already agrees. */
        wxT("function runAction(index){\n")
        wxT("  qspPost('S'+gen+'|'+index);\n")
        wxT("  qspPost('A'+gen+'|'+index);\n")
        wxT("}\n")
        wxT("list.addEventListener('click',function(e){\n")
        /* A link inside an item is the game's own and keeps its meaning */
        wxT("  var anchor=e.target;\n")
        wxT("  while(anchor&&anchor!==list&&anchor.tagName!=='A')")
        wxT("anchor=anchor.parentElement;\n")
        wxT("  if(anchor&&anchor.tagName==='A'){\n")
        wxT("    e.preventDefault();\n")
        wxT("    var raw=anchor.getAttribute('href');\n")
        wxT("    if(raw!==null){qspPost('L'+gen+'|'+raw);return;}\n")
        wxT("  }\n")
        wxT("  var index=rowIndex(e.target);\n")
        wxT("  if(index<0)return;\n")
        wxT("  if(extended){sel=index;paintSelection();runAction(index);return;}\n")
        wxT("  if(index!==sel){sel=index;paintSelection();qspPost('S'+gen+'|'+index);}\n")
        wxT("},false);\n")
        /* Enter runs the selected action, the way it does in the classic list.
           Not Alt-Enter: that is the window mode key, and the classic list
           never sees it either - the accelerator takes it first. */
        wxT("document.addEventListener('keydown',function(e){\n")
        wxT("  if(e.keyCode!==13||e.altKey||!extended||sel<0)return;\n")
        wxT("  e.preventDefault();\n")
        wxT("  runAction(sel);\n")
        wxT("},false);\n");

    wxString shell;
    shell << documentHead << documentCore << documentInput;
    shell << QSPWebPane::GetInputScript() << QSPWebPane::GetDiagnosticsScript();
    return shell;
}

/* ------------------------------------------------------------------ */
/* Contents                                                            */
/* ------------------------------------------------------------------ */

void QSPWebListBox::BeginItems()
{
    m_newImages.Clear();
    m_newDescs.Clear();
}

void QSPWebListBox::AddItem(const wxString& image, const wxString& desc)
{
    m_newImages.Add(image);
    m_newDescs.Add(desc);
}

/* Games call SHOWACTS and SHOWOBJS constantly, nearly always with the list
   already holding what is being set - so the comparison is what keeps this
   from rebuilding the DOM on every action. */
void QSPWebListBox::EndItems()
{
    if (m_images == m_newImages && m_descs == m_newDescs) return;

    m_images = m_newImages;
    m_descs = m_newDescs;
    if ((int)m_descs.GetCount() <= m_selection) m_selection = wxNOT_FOUND;
    /* Only a new list moves the number. Re-rendering the same one - HTML mode,
       the hotkey numbers, a new page - leaves every row where it was, so a
       click made on it still means what it meant. The number moves even when
       the script cannot be delivered yet: the page that will get the items is
       a new one, and it will be sent them afresh. */
    ++m_itemsGen;
    SendItems();
}

void QSPWebListBox::SendItems()
{
    RunScript(BuildItemsScript());
}

wxString QSPWebListBox::BuildItemsScript() const
{
    wxString script(wxT("qspSetItems(["));
    for (size_t i = 0; i < m_descs.GetCount(); ++i)
    {
        if (i) script << wxT(',');
        /* The same treatment the description panes give their text: a game in
           HTML mode writes its own markup, otherwise the text is escaped and
           its whitespace preserved. */
        wxString text(QSPTools::HtmlizeWhitespaces(
            m_toUseHtml ? m_descs[i] : QSPTools::ProceedAsPlain(m_descs[i])));
        script << wxT("{img:") << QSPWebUtil::ToJsString(m_images[i])
               << wxT(",text:") << QSPWebUtil::ToJsString(text) << wxT('}');
    }
    script << wxT("],") << (m_toShowNums ? wxT("true") : wxT("false"))
           << wxT(',') << m_itemsGen << wxT(");");
    return script;
}

/* The page's colours belong to the game and the desktop's highlight belongs to
   the desktop, and the two have no reason to agree: a dark player theme gives
   a dark Highlight, and a game that paints its lists white with black text
   then loses the row under the mouse entirely - dark on dark, with the game's
   own black text still on top of it. Tinting the page's own background towards
   the highlight keeps the selected row obviously selected while it stays on
   the same side of the page's contrast, so whatever colour the game chose for
   its text still reads on it. */
wxColour QSPWebListBox::SelectionColor() const
{
    wxColour highlight(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT));
    if (!highlight.IsOk()) return m_backColor;

    /* Three parts page to two parts highlight: enough of a shift to be seen at
       a glance, not enough to take the row out of the page's own range. */
    return wxColour((m_backColor.Red()   * 3 + highlight.Red()   * 2) / 5,
                    (m_backColor.Green() * 3 + highlight.Green() * 2) / 5,
                    (m_backColor.Blue()  * 3 + highlight.Blue()  * 2) / 5);
}

wxString QSPWebListBox::BuildStyleScript() const
{
    /* Point size rather than pixels, so the lists scale with the font setting
       exactly as the description panes do. */
    return wxString::Format(
        wxT("qspSetStyle({bg:%s,fg:%s,link:%s,font:%s,size:%s,selbg:%s,selfg:%s});"),
        QSPWebUtil::ToJsString(QSPWebUtil::ToCssColor(m_backColor)),
        QSPWebUtil::ToJsString(QSPWebUtil::ToCssColor(m_fontColor)),
        QSPWebUtil::ToJsString(QSPWebUtil::ToCssColor(m_linkColor)),
        QSPWebUtil::ToJsString(m_font.GetFaceName()),
        QSPWebUtil::ToJsString(wxString::Format(wxT("%dpt"), m_font.GetPointSize())),
        QSPWebUtil::ToJsString(QSPWebUtil::ToCssColor(SelectionColor())),
        QSPWebUtil::ToJsString(QSPWebUtil::ToCssColor(m_fontColor)));
}

wxString QSPWebListBox::BuildUserStylesScript() const
{
    return wxString::Format(wxT("qspSetStyles(%s,%s);"),
                            QSPWebUtil::ToJsString(m_userCss),
                            QSPWebUtil::ToJsArray(m_userCssFiles));
}

void QSPWebListBox::SetUserStyles(const wxString& inlineCss, const wxArrayString& files)
{
    if (m_userCss == inlineCss && m_userCssFiles == files) return;

    m_userCss = inlineCss;
    m_userCssFiles = files;
    RunScript(BuildUserStylesScript());
}

void QSPWebListBox::RefreshUI()
{
    RunScript(BuildStyleScript());
}

/* A fresh document holds nothing, so everything goes in again */
void QSPWebListBox::OnShellReady()
{
    RunScript(BuildStyleScript());
    RunScript(BuildUserStylesScript());
    SendItems();
    if (m_selection != wxNOT_FOUND)
        RunScript(wxString::Format(wxT("qspSelect(%d);"), m_selection));
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

void QSPWebListBox::SetIsHtml(bool isHtml)
{
    if (m_toUseHtml == isHtml) return;
    m_toUseHtml = isHtml;
    SendItems();
}

void QSPWebListBox::SetToShowNums(bool toShow)
{
    if (m_toShowNums == toShow) return;
    m_toShowNums = toShow;
    SendItems();
}

void QSPWebListBox::SetTextFont(const wxFont& font)
{
    if (m_font.GetFaceName().IsSameAs(font.GetFaceName(), false) &&
        m_font.GetPointSize() == font.GetPointSize())
        return;

    m_font = font;
    RunScript(BuildStyleScript());
}

void QSPWebListBox::SetLinkColor(const wxColour& clr)
{
    if (m_linkColor == clr) return;
    m_linkColor = clr;
    RunScript(BuildStyleScript());
}

void QSPWebListBox::SetSelection(int selection)
{
    /* The engine re-asserts the selection on every refresh, so this is on the
       hot path and has to be cheap when nothing moved. */
    if (selection == m_selection) return;
    m_selection = selection;
    RunScript(wxString::Format(wxT("qspSelect(%d);"), selection));
}

bool QSPWebListBox::SetBackgroundColour(const wxColour& colour)
{
    m_backColor = colour;
    /* The browser's own pre-paint colour too, so a resize does not flash white */
    if (GetView()) GetView()->SetBackgroundColour(colour);
    QSPWebPane::SetBackgroundColour(colour);
    RunScript(BuildStyleScript());
    return true;
}

bool QSPWebListBox::SetForegroundColour(const wxColour& colour)
{
    m_fontColor = colour;
    QSPWebPane::SetForegroundColour(colour);
    RunScript(BuildStyleScript());
    return true;
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

void QSPWebListBox::SendListEvent(wxEventType type, int index, long gen)
{
    /* Deferred rather than sent: both handlers run game code, and the engine
       must not be entered from inside the browser's own message callback.

       The list is checked when the event is dispatched, not when the message
       arrived: an event queued ahead of this one can run game code that
       replaces the list in between. */
    CallAfter([this, type, index, gen]()
    {
        if (gen != m_itemsGen)
        {
            /* The page painted a row of a list that is gone; put back the
               selection this side knows about */
            RunScript(wxString::Format(wxT("qspSelect(%d);"), m_selection));
            return;
        }
        /* Kept in step here as well as in the document, so the next
           SetSelection from the engine does not think it is a no-op. */
        m_selection = index;
        /* Raised on the pane and left to propagate, so it reaches the frame
           whatever sits between them */
        wxCommandEvent event(type, GetId());
        event.SetEventObject(this);
        event.SetInt(index);
        GetEventHandler()->ProcessEvent(event);
    });
}

void QSPWebListBox::OnPaneMessage(const wxString& message)
{
    long gen = 0, index = 0;
    wxString rest;
    if (!QSPWebUtil::SplitGen(message.Mid(1), &gen, &rest)) return;

    if (message[0] == wxT('S') || message[0] == wxT('A'))
    {
        if (!rest.ToLong(&index)) return;
        SendListEvent(message[0] == wxT('S') ? wxEVT_COMMAND_LISTBOX_SELECTED
                                             : wxEVT_COMMAND_LISTBOX_DOUBLECLICKED,
                      (int)index, gen);
        return;
    }
    if (message[0] != wxT('L')) return;
    /* Handled on the spot, so checked on the spot: a link in a list that has
       been replaced is a link the reader can no longer see */
    if (gen != m_itemsGen) return;

    /* A link the game put inside an item keeps the meaning it has everywhere
       else, so it is re-emitted as the event QSPFrame::OnLinkClicked takes. */
    wxMouseEvent mouseEvent(wxEVT_LEFT_UP);
    wxHtmlLinkInfo info(rest, wxEmptyString);
    info.SetEvent(&mouseEvent);

    wxHtmlLinkEvent linkEvent(GetId(), info);
    linkEvent.SetEventObject(this);
    GetParent()->GetEventHandler()->ProcessEvent(linkEvent);
}
