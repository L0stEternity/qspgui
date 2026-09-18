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

#include <limits.h>

#include "frame.h"
#include "comtools.h"
#include "callbacks_gui.h"
#include "devserver.h"

#include "icons/logo.xpm"
#include "icons/logo_big.xpm"
#include "icons/open.xpm"
#include "icons/new.xpm"
#include "icons/exit.xpm"
#include "icons/statusopen.xpm"
#include "icons/statussave.xpm"
#include "icons/windowmode.xpm"
#include "icons/about.xpm"

BEGIN_EVENT_TABLE(QSPFrame, wxFrame)
    EVT_INIT(QSPFrame::OnInit)
    EVT_CLOSE(QSPFrame::OnClose)
    EVT_TIMER(ID_TIMER, QSPFrame::OnTimer)
    EVT_MENU(wxID_EXIT, QSPFrame::OnQuit)
    EVT_MENU(ID_OPENGAME, QSPFrame::OnOpenGame)
    EVT_MENU(ID_NEWGAME, QSPFrame::OnNewGame)
    EVT_MENU(ID_OPENGAMESTAT, QSPFrame::OnOpenGameStat)
    EVT_MENU(ID_SAVEGAMESTAT, QSPFrame::OnSaveGameStat)
    EVT_MENU(ID_QUICKSAVE, QSPFrame::OnQuickSave)
    EVT_MENU(ID_QUICKSAVESLOT, QSPFrame::OnQuickSaveSlot)
    EVT_MENU(ID_QUICKLOADSLOT, QSPFrame::OnQuickLoadSlot)
    EVT_MENU_RANGE(ID_SAVESLOT1, ID_SAVESLOT9, QSPFrame::OnSaveToSlot)
    EVT_MENU_RANGE(ID_LOADSLOT1, ID_LOADSLOT9, QSPFrame::OnLoadFromSlot)
    EVT_MENU_OPEN(QSPFrame::OnMenuOpen)
    EVT_MENU(ID_SELECTFONT, QSPFrame::OnSelectFont)
    EVT_MENU(ID_USEFONTSIZE, QSPFrame::OnUseFontSize)
    EVT_MENU(ID_SELECTFONTCOLOR, QSPFrame::OnSelectFontColor)
    EVT_MENU(ID_SELECTBACKCOLOR, QSPFrame::OnSelectBackColor)
    EVT_MENU(ID_SELECTLINKCOLOR, QSPFrame::OnSelectLinkColor)
    EVT_MENU(ID_USESYSTEMCOLORS, QSPFrame::OnUseSystemColors)
    EVT_SYS_COLOUR_CHANGED(QSPFrame::OnSysColourChanged)
    EVT_MENU(ID_CHECKUPDATESONSTARTUP, QSPFrame::OnCheckUpdatesOnStartup)
    EVT_MENU(ID_SELECTLANG, QSPFrame::OnSelectLang)
    EVT_MENU(ID_TOGGLEWINMODE, QSPFrame::OnToggleWinMode)
    EVT_MENU(ID_TOGGLEOBJS, QSPFrame::OnToggleObjs)
    EVT_MENU(ID_TOGGLEACTS, QSPFrame::OnToggleActs)
    EVT_MENU(ID_TOGGLEDESC, QSPFrame::OnToggleDesc)
    EVT_MENU(ID_TOGGLEINPUT, QSPFrame::OnToggleInput)
    EVT_MENU(ID_TOGGLECAPTIONS, QSPFrame::OnToggleCaptions)
    EVT_MENU(ID_TOGGLEHOTKEYS, QSPFrame::OnToggleHotkeys)
    EVT_MENU(ID_VOLUME0, QSPFrame::OnVolume)
    EVT_MENU(ID_VOLUME20, QSPFrame::OnVolume)
    EVT_MENU(ID_VOLUME40, QSPFrame::OnVolume)
    EVT_MENU(ID_VOLUME60, QSPFrame::OnVolume)
    EVT_MENU(ID_VOLUME80, QSPFrame::OnVolume)
    EVT_MENU(ID_VOLUME100, QSPFrame::OnVolume)
    EVT_MENU(ID_CHECKUPDATES, QSPFrame::OnCheckUpdates)
    EVT_MENU(wxID_ABOUT, QSPFrame::OnAbout)
    EVT_HTML_LINK_CLICKED(ID_MAINDESC, QSPFrame::OnLinkClicked)
    EVT_HTML_LINK_CLICKED(ID_VARSDESC, QSPFrame::OnLinkClicked)
    EVT_LISTBOX(ID_OBJECTS, QSPFrame::OnObjectChange)
    EVT_LISTBOX(ID_ACTIONS, QSPFrame::OnActionChange)
    EVT_LISTBOX_DCLICK(ID_ACTIONS, QSPFrame::OnActionDblClick)
    EVT_TEXT(ID_INPUT, QSPFrame::OnInputTextChange)
    EVT_ENTER(ID_INPUT, QSPFrame::OnInputTextEnter)
    EVT_KEY_UP(QSPFrame::OnKey)
    EVT_MOUSEWHEEL(QSPFrame::OnWheel)
    EVT_LEFT_DOWN(QSPFrame::OnMouseClick)
    EVT_AUI_PANE_CLOSE(QSPFrame::OnPaneClose)
    EVT_DROP_FILES(QSPFrame::OnDropFiles)
END_EVENT_TABLE()

wxIMPLEMENT_CLASS(QSPFrame, wxFrame);

QSPFrame::QSPFrame(const wxString &configPath, QSPTranslationHelper *transHelper) :
    wxFrame(NULL, wxID_ANY, wxEmptyString),
    m_devServer(0),
    m_configDefPath(configPath),
    m_configPath(configPath),
    m_transHelper(transHelper)
{
    wxRegisterId(ID_DUMMY);
    Bind(wxEVT_WEBREQUEST_STATE, &QSPFrame::OnVersionRequestState, this);

    SetIcon(wxICON(logo));
    DragAcceptFiles(true);
    m_timer = new wxTimer(this, ID_TIMER);
    m_menu = new wxMenu;
    // Menu
    wxMenuBar *menuBar = new wxMenuBar;
    m_fileMenu = new wxMenu;
    wxMenuItem *fileOpenItem = new wxMenuItem(m_fileMenu, ID_OPENGAME, wxT("-"));
    fileOpenItem->SetBitmap(wxBitmap(open_xpm));
    m_fileMenu->Append(fileOpenItem);
    wxMenuItem *fileNewItem = new wxMenuItem(m_fileMenu, ID_NEWGAME, wxT("-"));
    fileNewItem->SetBitmap(wxBitmap(new_xpm));
    m_fileMenu->Append(fileNewItem);
    m_fileMenu->AppendSeparator();
    wxMenuItem *fileExitItem = new wxMenuItem(m_fileMenu, wxID_EXIT);
    fileExitItem->SetBitmap(wxBitmap(exit_xpm));
    m_fileMenu->Append(fileExitItem);
    // ------------
    m_gameMenu = new wxMenu;
    wxMenuItem *gameOpenItem = new wxMenuItem(m_gameMenu, ID_OPENGAMESTAT, wxT("-"));
    gameOpenItem->SetBitmap(wxBitmap(statusopen_xpm));
    m_gameMenu->Append(gameOpenItem);
    m_gameMenu->Append(ID_SAVEGAMESTAT, wxT("-"));
    wxMenuItem *gameSaveItem = new wxMenuItem(m_gameMenu, ID_QUICKSAVE, wxT("-"));
    gameSaveItem->SetBitmap(wxBitmap(statussave_xpm));
    m_gameMenu->Append(gameSaveItem);
    m_gameMenu->AppendSeparator();
    m_gameMenu->Append(ID_QUICKSAVESLOT, wxT("-"));
    m_gameMenu->Append(ID_QUICKLOADSLOT, wxT("-"));
    // ------------
    /* The slot labels are the slots' contents, so they are filled in when the
       menu opens rather than here - at this point no game is even loaded. */
    m_saveSlotsMenu = new wxMenu;
    m_loadSlotsMenu = new wxMenu;
    for (int slot = 1; slot <= QSPSaveSlots::Count; ++slot)
    {
        m_saveSlotsMenu->Append(ID_SAVESLOT1 + slot - 1, wxT("-"));
        m_loadSlotsMenu->Append(ID_LOADSLOT1 + slot - 1, wxT("-"));
    }
    m_gameMenu->AppendSeparator();
    m_gameMenu->Append(ID_SAVETOSLOT, wxT("-"), m_saveSlotsMenu);
    m_gameMenu->Append(ID_LOADFROMSLOT, wxT("-"), m_loadSlotsMenu);
    // ------------
    wxMenu *wndsMenu = new wxMenu;
    wndsMenu->Append(ID_TOGGLEOBJS, wxT("-"));
    wndsMenu->Append(ID_TOGGLEACTS, wxT("-"));
    wndsMenu->Append(ID_TOGGLEDESC, wxT("-"));
    wndsMenu->Append(ID_TOGGLEINPUT, wxT("-"));
    wndsMenu->AppendSeparator();
    wndsMenu->Append(ID_TOGGLECAPTIONS, wxT("-"));
    wndsMenu->Append(ID_TOGGLEHOTKEYS, wxT("-"));
    // ------------
    wxMenu *fontMenu = new wxMenu;
    fontMenu->Append(ID_SELECTFONT, wxT("-"));
    fontMenu->AppendCheckItem(ID_USEFONTSIZE, wxT("-"));
    // ------------
    wxMenu *colorsMenu = new wxMenu;
    colorsMenu->Append(ID_SELECTFONTCOLOR, wxT("-"));
    colorsMenu->Append(ID_SELECTBACKCOLOR, wxT("-"));
    colorsMenu->Append(ID_SELECTLINKCOLOR, wxT("-"));
    colorsMenu->AppendSeparator();
    colorsMenu->AppendCheckItem(ID_USESYSTEMCOLORS, wxT("-"));
    // ------------
    wxMenu *volumeMenu = new wxMenu;
    volumeMenu->AppendRadioItem(ID_VOLUME0, wxT("-"));
    volumeMenu->AppendRadioItem(ID_VOLUME20, wxT("-"));
    volumeMenu->AppendRadioItem(ID_VOLUME40, wxT("-"));
    volumeMenu->AppendRadioItem(ID_VOLUME60, wxT("-"));
    volumeMenu->AppendRadioItem(ID_VOLUME80, wxT("-"));
    volumeMenu->AppendRadioItem(ID_VOLUME100, wxT("-"));
    // ------------
    m_settingsMenu = new wxMenu;
    m_settingsMenu->Append(ID_SHOWHIDE, wxT("-"), wndsMenu);
    m_settingsMenu->Append(ID_FONT, wxT("-"), fontMenu);
    m_settingsMenu->Append(ID_COLORS, wxT("-"), colorsMenu);
    m_settingsMenu->Append(ID_VOLUME, wxT("-"), volumeMenu);
    m_settingsMenu->AppendCheckItem(ID_CHECKUPDATESONSTARTUP, wxT("-"));
    m_settingsMenu->AppendSeparator();
    wxMenuItem *settingsWinModeItem = new wxMenuItem(m_settingsMenu, ID_TOGGLEWINMODE, wxT("-"));
    settingsWinModeItem->SetBitmap(wxBitmap(windowmode_xpm));
    m_settingsMenu->Append(settingsWinModeItem);
    m_settingsMenu->AppendSeparator();
    m_settingsMenu->Append(ID_SELECTLANG, wxT("-"));
    // ------------
    wxMenu *helpMenu = new wxMenu;
    helpMenu->Append(ID_CHECKUPDATES, wxT("-"));
    helpMenu->AppendSeparator();
    wxMenuItem *helpAboutItem = new wxMenuItem(helpMenu, wxID_ABOUT, wxT("-"));
    helpAboutItem->SetBitmap(wxBitmap(about_xpm));
    helpMenu->Append(helpAboutItem);
    // ------------
    menuBar->Append(m_fileMenu, wxT("-"));
    menuBar->Append(m_gameMenu, wxT("-"));
    menuBar->Append(m_settingsMenu, wxT("-"));
    menuBar->Append(helpMenu, wxT("-"));
    SetMenuBar(menuBar);
    // --------------------------------------
    m_manager = new wxAuiManager(this);
    m_manager->SetDockSizeConstraint(0.5, 0.5);
    m_imgView = new QSPMainImgCanvas(this, ID_VIEWPIC);
    m_manager->AddPane(m_imgView, wxAuiPaneInfo().Name(wxT("imgview")).MinSize(50, 50).BestSize(150, 150).Top().MaximizeButton().Hide());
    m_desc = new QSPMainTextBox(this, ID_MAINDESC);
    m_manager->AddPane(m_desc, wxAuiPaneInfo().Name(wxT("desc")).CenterPane());
    m_objects = new QSPMainListBox(this, ID_OBJECTS);
    m_manager->AddPane(m_objects, wxAuiPaneInfo().Name(wxT("objs")).MinSize(50, 50).BestSize(100, 100).Right().MaximizeButton());
    m_actions = new QSPMainListBox(this, ID_ACTIONS, LB_EXTENDED);
    m_manager->AddPane(m_actions, wxAuiPaneInfo().Name(wxT("acts")).MinSize(50, 50).BestSize(100, 100).Bottom().MaximizeButton());
    m_vars = new QSPMainTextBox(this, ID_VARSDESC);
    m_manager->AddPane(m_vars, wxAuiPaneInfo().Name(wxT("vars")).MinSize(50, 50).BestSize(100, 100).Bottom().MaximizeButton());
    m_input = new QSPInputBox(this, ID_INPUT);
    m_manager->AddPane(m_input, wxAuiPaneInfo().Name(wxT("input")).MinSize(50, 20).BestSize(100, 20).Bottom().Layer(1));
    // --------------------------------------
    m_desc->SetPathProvider(this);
    m_objects->SetPathProvider(this);
    m_actions->SetPathProvider(this);
    m_vars->SetPathProvider(this);
    /* The image pane needs one too now that it renders in the browser: it is
       what registers the game folder's virtual host and gives the document a
       base for the picture to resolve against. */
    m_imgView->SetPathProvider(this);
    m_desc->SetPaneName(wxT("main"));
    m_vars->SetPaneName(wxT("vars"));
#ifdef QSPGUI_USE_WEBVIEW
    Bind(wxEVT_QSP_SCRIPT_CALL, &QSPFrame::OnScriptCall, this);
    Bind(wxEVT_QSP_SCRIPT_DIAG, &QSPFrame::OnScriptDiag, this);
#endif
    // --------------------------------------
    m_toast = new QSPToast(this);
    // --------------------------------------
    SetMinClientSize(wxSize(450, 300));
    SetOverallVolume(100);
    m_savedGamePath.Clear();
    m_worldPath.Clear();
    m_toQuit = false;
    m_keyPressedWhileDisabled = false;
    m_isGameOpened = false;
    m_isManagerUpdatePending = false;
    m_toUseSystemColors = false;
}

QSPFrame::~QSPFrame()
{
    delete m_devServer;
    m_manager->UnInit();
    delete m_manager;
    delete m_menu;
    delete m_timer;
}

void QSPFrame::SaveSettings()
{
    int x, y, w, h;
    bool isMaximized;
    if (IsFullScreen()) ShowFullScreen(false);
    if (IsIconized()) Iconize(false);
    if ((isMaximized = IsMaximized())) Maximize(false);
    wxFileConfig cfg(wxEmptyString, wxEmptyString, m_configPath);
    cfg.Write(wxT("Colors/BackColor"), (int)QSPTools::PackColor(m_backColor));
    cfg.Write(wxT("Colors/FontColor"), (int)QSPTools::PackColor(m_fontColor));
    cfg.Write(wxT("Colors/LinkColor"), (int)QSPTools::PackColor(m_linkColor));
    cfg.Write(wxT("Font/FontSize"), m_fontSize);
    cfg.Write(wxT("Font/FontName"), m_fontName);
    cfg.Write(wxT("Font/UseFontSize"), m_toUseFontSize);
    cfg.Write(wxT("General/Volume"), m_volume);
    cfg.Write(wxT("General/ShowHotkeys"), m_toShowHotkeys);
    cfg.Write(wxT("General/Panels"), m_manager->SavePerspective());
    cfg.Write(wxT("General/CheckUpdates"), m_toCheckUpdates);
    cfg.Write(wxT("Colors/UseSystemColors"), m_toUseSystemColors);
    m_transHelper->Save(cfg, wxT("General/Language"));
    GetPosition(&x, &y);
    GetClientSize(&w, &h);
    cfg.Write(wxT("Pos/Left"), x);
    cfg.Write(wxT("Pos/Top"), y);
    cfg.Write(wxT("Pos/Width"), w);
    cfg.Write(wxT("Pos/Height"), h);
    cfg.Write(wxT("Pos/Maximize"), isMaximized);
}

void QSPFrame::LoadSettings()
{
    bool toMaximize;
    int x, y, w, h, temp;
    Hide();
    /* Asked before the config is opened, because opening it can create it */
    bool isFirstRun = !wxFileExists(m_configPath);

    wxFileConfig cfg(wxEmptyString, wxEmptyString, m_configPath);
    /* The stored form is QSP's own 0xBBGGRR, which is also what wxColour's
       packed constructor reads - so the defaults have to be built from
       components rather than written as literals. */
    wxColour sysBack, sysFont, sysLink;
    GetAppearanceColors(sysBack, sysFont, sysLink);
    /* On by default, so a first run on a dark desktop looks like one. An
       existing config predates the setting and keeps the colours it has. */
    cfg.Read(wxT("Colors/UseSystemColors"), &m_toUseSystemColors, isFirstRun);
    cfg.Read(wxT("Colors/BackColor"), &temp, (int)QSPTools::PackColor(sysBack));
    m_backColor = (m_toUseSystemColors ? sysBack : wxColour(temp));
    cfg.Read(wxT("Colors/FontColor"), &temp, (int)QSPTools::PackColor(sysFont));
    m_fontColor = (m_toUseSystemColors ? sysFont : wxColour(temp));
    cfg.Read(wxT("Colors/LinkColor"), &temp, (int)QSPTools::PackColor(sysLink));
    m_linkColor = (m_toUseSystemColors ? sysLink : wxColour(temp));
    temp = wxNORMAL_FONT->GetPointSize();
    if (temp < 12) temp = 12;
    cfg.Read(wxT("Font/FontSize"), &m_fontSize, temp);
    cfg.Read(wxT("Font/FontName"), &m_fontName, wxNORMAL_FONT->GetFaceName());
    cfg.Read(wxT("Font/UseFontSize"), &m_toUseFontSize, false);
    cfg.Read(wxT("General/ShowHotkeys"), &m_toShowHotkeys, false);
    cfg.Read(wxT("General/Volume"), &m_volume, 100);
    cfg.Read(wxT("Pos/Left"), &x, 10);
    cfg.Read(wxT("Pos/Top"), &y, 10);
    cfg.Read(wxT("Pos/Width"), &w, 850);
    cfg.Read(wxT("Pos/Height"), &h, 650);
    cfg.Read(wxT("Pos/Maximize"), &toMaximize, false);
    wxString panels(wxT("layout2|") \
        wxT("name=imgview;state=1080035327;dir=1;layer=0;row=0;pos=0;prop=100000;bestw=832;besth=150;minw=50;minh=50;maxw=-1;maxh=-1;floatx=175;floaty=148;floatw=518;floath=372|") \
        wxT("name=desc;state=768;dir=5;layer=0;row=0;pos=0;prop=100000;bestw=613;besth=341;minw=-1;minh=-1;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|") \
        wxT("name=objs;state=6293500;dir=2;layer=0;row=0;pos=0;prop=100000;bestw=213;besth=324;minw=50;minh=50;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|") \
        wxT("name=acts;state=6293500;dir=3;layer=0;row=0;pos=0;prop=117349;bestw=475;besth=185;minw=50;minh=50;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|") \
        wxT("name=vars;state=6293500;dir=3;layer=0;row=0;pos=1;prop=82651;bestw=351;besth=185;minw=50;minh=50;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|") \
        wxT("name=input;state=2099196;dir=3;layer=1;row=0;pos=0;prop=100000;bestw=832;besth=22;minw=50;minh=20;maxw=-1;maxh=-1;floatx=-1;floaty=-1;floatw=-1;floath=-1|") \
        wxT("dock_size(5,0,0)=22|dock_size(2,0,0)=215|dock_size(3,0,0)=204|dock_size(3,1,0)=41|"));
    cfg.Read(wxT("General/Panels"), &panels);
    cfg.Read(wxT("General/CheckUpdates"), &m_toCheckUpdates, true);
    m_transHelper->Load(cfg, wxT("General/Language"));
    // -------------------------------------------------
    SetOverallVolume(m_volume);
    ApplyBackColor(m_backColor);
    ApplyFontColor(m_fontColor);
    ApplyLinkColor(m_linkColor);
    ApplyFontSize(m_fontSize);
    if (!ApplyFontName(m_fontName))
    {
        m_fontName = wxNORMAL_FONT->GetFaceName();
        ApplyFontName(m_fontName);
    }
    RefreshUI();
    m_settingsMenu->Check(ID_USEFONTSIZE, m_toUseFontSize);
    m_settingsMenu->Check(ID_CHECKUPDATESONSTARTUP, m_toCheckUpdates);
    m_settingsMenu->Check(ID_USESYSTEMCOLORS, m_toUseSystemColors);
    m_manager->LoadPerspective(panels);
    m_manager->RestoreMaximizedPane();
    // Check for correct position
    wxSize winSize(ClientToWindowSize(wxSize(w, h)));
    w = winSize.GetWidth();
    h = winSize.GetHeight();
    wxRect dispRect(wxGetClientDisplayRect());
    if (w > dispRect.GetWidth()) w = dispRect.GetWidth();
    if (h > dispRect.GetHeight()) h = dispRect.GetHeight();
    if (x < dispRect.GetLeft()) x = dispRect.GetLeft();
    if (y < dispRect.GetTop()) y = dispRect.GetTop();
    if (x + w - 1 > dispRect.GetRight()) x = dispRect.GetRight() - w + 1;
    if (y + h - 1 > dispRect.GetBottom()) y = dispRect.GetBottom() - h + 1;
    // --------------------------
    SetSize(x, y, w, h);
    ShowPane(ID_VIEWPIC, false);
    ShowPane(ID_ACTIONS, true);
    ShowPane(ID_OBJECTS, true);
    ShowPane(ID_VARSDESC, true);
    ShowPane(ID_INPUT, true);
    ReCreateGUI();
    if (toMaximize) Maximize();
    Show();
    m_manager->Update();
}

void QSPFrame::EnableControls(bool status, bool isExtended)
{
    if (isExtended) m_fileMenu->Enable(ID_OPENGAME, status);
    m_fileMenu->Enable(ID_NEWGAME, status);
    m_gameMenu->Enable(ID_OPENGAMESTAT, status);
    m_gameMenu->Enable(ID_SAVEGAMESTAT, status);
    m_gameMenu->Enable(ID_QUICKSAVE, status);
    m_gameMenu->Enable(ID_QUICKSAVESLOT, status);
    m_gameMenu->Enable(ID_QUICKLOADSLOT, status);
    m_gameMenu->Enable(ID_SAVETOSLOT, status);
    m_gameMenu->Enable(ID_LOADFROMSLOT, status);
    m_settingsMenu->Enable(ID_TOGGLEOBJS, status);
    m_settingsMenu->Enable(ID_TOGGLEACTS, status);
    m_settingsMenu->Enable(ID_TOGGLEDESC, status);
    m_settingsMenu->Enable(ID_TOGGLEINPUT, status);
    m_objects->Enable(status);
    m_actions->Enable(status);
    m_input->SetEditable(status);
    m_toProcessEvents = status;
    m_keyPressedWhileDisabled = false;
}

void QSPFrame::ShowPane(wxWindowID id, bool toShow)
{
    int i;
    wxAuiPaneInfoArray& allPanes = m_manager->GetAllPanes();
    wxAuiPaneInfo *maximizedPane = NULL;
    wxAuiPaneInfo *pane = NULL;
    for (i = (int)allPanes.GetCount() - 1; i >= 0; --i)
    {
        wxAuiPaneInfo &currentPane = allPanes.Item(i);
        wxWindow *paneWindow = currentPane.window;
        if (paneWindow && paneWindow->GetId() == id)
            pane = &currentPane;
        if (currentPane.IsMaximized())
            maximizedPane = &currentPane;
    }
    if (pane)
    {
        if (maximizedPane)
        {
            if (maximizedPane == pane)
            {
                if (!toShow)
                {
                    /* Freezing belongs around an actual relayout and nowhere
                       else. Freeze() recurses into every child and Thaw()
                       follows with a refresh, which ordinary controls satisfy
                       synchronously - but a browser control re-composites on
                       its own schedule, so each needless cycle shows as a
                       blank frame. Games call this on every SHOWSTAT/SHOWOBJS,
                       almost always with the pane already in the state asked
                       for, so freezing up front cost a flash per game tick. */
                    wxON_BLOCK_EXIT_THIS0(QSPFrame::Thaw);
                    Freeze();
                    m_manager->RestorePane(*pane);
                    pane->Hide();
                    m_manager->Update();
                }
            }
            else if (pane->HasFlag(wxAuiPaneInfo::savedHiddenState) == toShow)
                pane->SetFlag(wxAuiPaneInfo::savedHiddenState, !toShow);
        }
        else if (pane->IsShown() != toShow)
        {
            pane->Show(toShow);
            RequestManagerUpdate();
        }
    }
}

/* Every wxAuiManager::Update() tears down and rebuilds the whole dock layout,
   and a single action can toggle several panes. Freeze() hides the half-built
   result for ordinary controls, but a wxWebView is a child window of another
   process and keeps painting through it, so each rebuild shows as a blink with
   duplicated captions. Collapse a burst of toggles into one relayout. */
void QSPFrame::RequestManagerUpdate()
{
    if (m_isManagerUpdatePending) return;
    m_isManagerUpdatePending = true;
    CallAfter(&QSPFrame::DoManagerUpdate);
}

void QSPFrame::DoManagerUpdate()
{
    if (!m_isManagerUpdatePending) return;
    m_isManagerUpdatePending = false;
    wxON_BLOCK_EXIT_THIS0(QSPFrame::Thaw);
    Freeze();
    m_manager->Update();
}

void QSPFrame::ApplyParams()
{
    QSP_BIGINT numVal;
    QSPString strVal;
    wxColour setBackColor, setFontColor, setLinkColor;
    wxString setFontName;
    int setFontSize;
    bool toRefreshUI = false;
    // --------------
    setBackColor = ((QSPGetNumVarValue(QSP_STATIC_STR(QSP_FMT("BCOLOR")), 0, &numVal) && numVal)
        ? wxColour(numVal) : m_backColor);
    if (setBackColor != m_desc->GetBackgroundColour())
    {
        if (ApplyBackColor(setBackColor)) toRefreshUI = true;
    }
    // --------------
    setFontColor = ((QSPGetNumVarValue(QSP_STATIC_STR(QSP_FMT("FCOLOR")), 0, &numVal) && numVal)
        ? wxColour(numVal) : m_fontColor);
    if (setFontColor != m_desc->GetForegroundColour())
    {
        if (ApplyFontColor(setFontColor)) toRefreshUI = true;
    }
    // --------------
    setLinkColor = ((QSPGetNumVarValue(QSP_STATIC_STR(QSP_FMT("LCOLOR")), 0, &numVal) && numVal)
        ? wxColour(numVal) : m_linkColor);
    if (setLinkColor != m_desc->GetLinkColor())
    {
        if (ApplyLinkColor(setLinkColor)) toRefreshUI = true;
    }
    // --------------
    if (m_toUseFontSize)
        setFontSize = m_fontSize;
    else
    {
        setFontSize = ((QSPGetNumVarValue(QSP_STATIC_STR(QSP_FMT("FSIZE")), 0, &numVal) && numVal)
            ? numVal : m_fontSize);
    }
    if (setFontSize != m_desc->GetTextFont().GetPointSize())
    {
        if (ApplyFontSize(setFontSize)) toRefreshUI = true;
    }
    // --------------
    setFontName = ((QSPGetStrVarValue(QSP_STATIC_STR(QSP_FMT("FNAME")), 0, &strVal) && !qspIsEmpty(strVal))
        ? qspToWxString(strVal) : m_fontName);
    if (!setFontName.IsSameAs(m_desc->GetTextFont().GetFaceName(), false))
    {
        if (ApplyFontName(setFontName))
            toRefreshUI = true;
        else if (!m_fontName.IsSameAs(m_desc->GetTextFont().GetFaceName(), false))
        {
            if (ApplyFontName(m_fontName)) toRefreshUI = true;
        }
    }
    // --------------
    if (toRefreshUI) RefreshUI();
}

void QSPFrame::DeleteMenu()
{
    delete m_menu;
    m_menu = new wxMenu;
    m_menuItemId = ID_BEGOFDYNMENU;
}

void QSPFrame::AddMenuItem(const wxString &name, const wxString &imgPath)
{
    Connect(m_menuItemId, wxID_ANY, wxEVT_COMMAND_MENU_SELECTED, wxCommandEventHandler(QSPFrame::OnMenu));
    if (name == wxT("-"))
        m_menu->AppendSeparator();
    else
    {
        wxMenuItem *item = new wxMenuItem(m_menu, m_menuItemId, name);
        wxString imageFullPath(ComposeGamePath(imgPath));
        if (wxFileExists(imageFullPath))
        {
            wxBitmap itemBmp(imageFullPath, wxBITMAP_TYPE_ANY);
            if (itemBmp.Ok()) item->SetBitmap(itemBmp);
        }
        m_menu->Append(item);
    }
    ++m_menuItemId;
}

int QSPFrame::ShowMenu()
{
    m_menuIndex = -1;
    PopupMenu(m_menu);
    return m_menuIndex;
}

void QSPFrame::UpdateGamePath(const wxString &fullPath)
{
    wxFileName fileName(fullPath, wxPATH_DOS);
    m_worldPath = fileName.GetPath(wxPATH_GET_VOLUME | wxPATH_GET_SEPARATOR);
}

/* Keeps the full path of the world file, which UpdateGamePath discards -
   the development API needs it to reload the game from disk. */
void QSPFrame::UpdateGameFile(const wxString &fullPath)
{
    m_gameFilePath = fullPath;
    /* Slots are per game and keyed off the world file, so OPENQST moving the
       session to another world moves the slots with it. */
    m_saveSlots.SetGameFile(fullPath);
    UpdateGamePath(fullPath);
}

bool QSPFrame::StartDevServer(unsigned short port, const wxString &token)
{
    if (!m_devServer)
        m_devServer = new QSPDevServer(this);

    return m_devServer->Start(port, token);
}

wxString QSPFrame::ComposeGamePath(const wxString &relativePath) const
{
    return QSPPaths::ComposeContained(m_worldPath, relativePath);
}

bool QSPFrame::IsValidFullPath(const wxString &path) const
{
    return QSPPaths::IsContained(m_worldPath, path);
}

void QSPFrame::ShowError()
{
    if (m_toQuit) return;
    QSPErrorInfo errorInfo = QSPGetLastErrorData();
    if (!errorInfo.ErrorNum) return; // error is undefined
    if (m_devServer) m_devServer->NotifyError();
    wxString locName(qspToWxString(errorInfo.LocName));
    wxString errorDesc(qspToWxString(errorInfo.ErrorDesc));
    wxString line(qspToWxString(errorInfo.IntLine));
    if (line.IsEmpty())
        line = _("Unknown");

    wxString wxMessage;
    if (!locName.IsEmpty())
        wxMessage = wxString::Format(
            _("Location: %s\nArea: %s\nLine %d: %s\nCode: %d\nDesc: %s"),
            locName.wx_str(),
            (errorInfo.ActIndex < 0 ? _("on visit").wx_str() : _("on action").wx_str()),
            errorInfo.TopLineNum,
            line.wx_str(),
            errorInfo.ErrorNum,
            wxGetTranslation(errorDesc).wx_str()
        );
    else
        wxMessage = wxString::Format(
            _("Line %d: %s\nCode: %d\nDesc: %s"),
            errorInfo.IntLineNum,
            line.wx_str(),
            errorInfo.ErrorNum,
            wxGetTranslation(errorDesc).wx_str()
        );
    QSPMsgDlg dialog(this,
                     wxID_ANY,
                     m_desc->GetBackgroundColour(),
                     m_desc->GetForegroundColour(),
                     m_desc->GetTextFont(),
                     _("Error"),
                     wxMessage,
                     false,
                     this
    );
    bool oldToProcessEvents = m_toProcessEvents;
    m_toProcessEvents = false;
    dialog.ShowModal();
    m_toProcessEvents = oldToProcessEvents;
    if (m_isGameOpened) QSPCallbacks::RefreshInt(QSP_FALSE, QSP_FALSE);
}

void QSPFrame::UpdateTitle()
{
    wxString title(QSP_LOGO);
    #ifdef _DEBUG
        title = wxString::Format(wxT("%s (DEBUG)"), title.wx_str());
    #endif
    if (m_configPath != m_configDefPath)
        title = wxString::Format(wxT("%s [+]"), title.wx_str());
    SetTitle(title);
}

void QSPFrame::ReCreateGUI()
{
    wxMenuBar *menuBar = GetMenuBar();
    UpdateTitle();
    // ------------
    menuBar->SetMenuLabel(0, _("&Quest"));
    menuBar->SetMenuLabel(1, _("&Game"));
    menuBar->SetMenuLabel(2, _("&Settings"));
    menuBar->SetMenuLabel(3, _("&Help"));
    // ------------
    menuBar->SetLabel(ID_OPENGAME, _("&Open game...\tAlt-O"));
    menuBar->SetLabel(ID_NEWGAME, _("&Restart game\tAlt-N"));
    menuBar->SetLabel(wxID_EXIT, _("&Quit\tAlt-X"));
    menuBar->SetLabel(ID_OPENGAMESTAT, _("&Open saved game...\tCtrl-O"));
    menuBar->SetLabel(ID_SAVEGAMESTAT, _("&Save game..."));
    menuBar->SetLabel(ID_QUICKSAVE, _("&Quicksave\tCtrl-S"));
    menuBar->SetLabel(ID_QUICKSAVESLOT, _("Quick save &slot\tF5"));
    menuBar->SetLabel(ID_QUICKLOADSLOT, _("Load quick save s&lot\tF9"));
    menuBar->SetLabel(ID_SAVETOSLOT, _("Save to slo&t"));
    menuBar->SetLabel(ID_LOADFROMSLOT, _("Load from slo&t"));
    menuBar->SetLabel(ID_TOGGLEOBJS, _("&Objects\tCtrl-1"));
    menuBar->SetLabel(ID_TOGGLEACTS, _("&Actions\tCtrl-2"));
    menuBar->SetLabel(ID_TOGGLEDESC, _("A&dditional desc\tCtrl-3"));
    menuBar->SetLabel(ID_TOGGLEINPUT, _("&Input area\tCtrl-4"));
    menuBar->SetLabel(ID_TOGGLECAPTIONS, _("&Captions\tCtrl-5"));
    menuBar->SetLabel(ID_TOGGLEHOTKEYS, _("&Hotkeys for actions\tCtrl-6"));
    menuBar->SetLabel(ID_SHOWHIDE, _("&Show / Hide"));
    menuBar->SetLabel(ID_FONT, _("&Font"));
    menuBar->SetLabel(ID_SELECTFONT, _("Select &font...\tAlt-F"));
    menuBar->SetLabel(ID_USEFONTSIZE, _("&Always use selected font size"));
    menuBar->SetLabel(ID_COLORS, _("&Colors"));
    menuBar->SetLabel(ID_SELECTFONTCOLOR, _("Select font &color...\tAlt-C"));
    menuBar->SetLabel(ID_SELECTBACKCOLOR, _("Select &background color...\tAlt-B"));
    menuBar->SetLabel(ID_SELECTLINKCOLOR, _("Select l&inks color...\tAlt-I"));
    menuBar->SetLabel(ID_USESYSTEMCOLORS, _("Follow s&ystem light / dark theme"));
    menuBar->SetLabel(ID_VOLUME, _("Sound &volume"));
    menuBar->SetLabel(ID_VOLUME0, _("No sound\tAlt-1"));
    menuBar->SetLabel(ID_VOLUME20, _("20%\tAlt-2"));
    menuBar->SetLabel(ID_VOLUME40, _("40%\tAlt-3"));
    menuBar->SetLabel(ID_VOLUME60, _("60%\tAlt-4"));
    menuBar->SetLabel(ID_VOLUME80, _("80%\tAlt-5"));
    menuBar->SetLabel(ID_VOLUME100, _("Initial volume\tAlt-6"));
    menuBar->SetLabel(ID_CHECKUPDATESONSTARTUP, _("Check for updates on startup"));
    menuBar->SetLabel(ID_TOGGLEWINMODE, _("Window / Fullscreen &mode\tAlt-Enter"));
    menuBar->SetLabel(ID_SELECTLANG, _("Select &language...\tAlt-L"));
    menuBar->SetLabel(ID_CHECKUPDATES, _("Check for latest version"));
    menuBar->SetLabel(wxID_ABOUT, _("&About...\tCtrl-H"));
    // --------------------------------------
    m_manager->GetPane(wxT("imgview")).Caption(_("Preview"));
    m_manager->GetPane(wxT("objs")).Caption(_("Objects"));
    m_manager->GetPane(wxT("acts")).Caption(_("Actions"));
    m_manager->GetPane(wxT("vars")).Caption(_("Additional desc"));
    m_manager->GetPane(wxT("input")).Caption(_("Input area"));
    // --------------------------------------
    m_manager->Update();
}

void QSPFrame::RefreshUI()
{
    m_desc->RefreshUI();
    m_objects->RefreshUI();
    m_actions->RefreshUI();
    m_vars->RefreshUI();
    m_input->Refresh();
    m_imgView->RefreshUI();
}

void QSPFrame::ApplyFont(const wxFont& font)
{
    m_desc->SetTextFont(font);
    m_objects->SetTextFont(font);
    m_actions->SetTextFont(font);
    m_vars->SetTextFont(font);
    m_input->SetFont(font);
}

bool QSPFrame::ApplyFontSize(int size)
{
    wxFont font(m_desc->GetTextFont());
    font.SetPointSize(size);
    ApplyFont(font);
    return true;
}

bool QSPFrame::ApplyFontName(const wxString& name)
{
    if (wxFontEnumerator::IsValidFacename(name))
    {
        wxFont font(m_desc->GetTextFont());
        font.SetFaceName(name);
        ApplyFont(font);
        return true;
    }
    return false;
}

bool QSPFrame::ApplyFontColor(const wxColour& color)
{
    m_desc->SetForegroundColour(color);
    m_objects->SetForegroundColour(color);
    m_actions->SetForegroundColour(color);
    m_vars->SetForegroundColour(color);
    m_input->SetForegroundColour(color);
    return true;
}

bool QSPFrame::ApplyBackColor(const wxColour& color)
{
    m_desc->SetBackgroundColour(color);
    m_objects->SetBackgroundColour(color);
    m_actions->SetBackgroundColour(color);
    m_vars->SetBackgroundColour(color);
    m_input->SetBackgroundColour(color);
    m_imgView->SetBackgroundColour(color);
    return true;
}

/* The historic defaults are a light grey page, black text and blue links, and
   that is still exactly what a light desktop gets. A dark one gets the same
   relationships rather than the same numbers: a page darker than the window
   chrome around it, text a little short of white so it doesn't glare, and a
   link light enough to stay legible against it.

   A game that sets $BCOLOR / $FCOLOR / $LCOLOR still overrides all of this -
   these are only the player's own defaults, which is what ApplyParams falls
   back to. */
void QSPFrame::GetAppearanceColors(wxColour &back, wxColour &font, wxColour &link)
{
    if (wxSystemSettings::GetAppearance().IsDark())
    {
        back = wxColour(0x1E, 0x1E, 0x1E);
        font = wxColour(0xE0, 0xE0, 0xE0);
        link = wxColour(0x6C, 0xB6, 0xFF);
    }
    else
    {
        back = wxColour(0xE0, 0xE0, 0xE0);
        font = wxColour(0x00, 0x00, 0x00);
        link = wxColour(0x00, 0x00, 0xFF);
    }
}

void QSPFrame::SetUseSystemColors(bool toUse)
{
    m_toUseSystemColors = toUse;
    if (m_settingsMenu) m_settingsMenu->Check(ID_USESYSTEMCOLORS, toUse);
}

void QSPFrame::ApplySystemColors()
{
    if (!m_toUseSystemColors) return;

    wxColour back, font, link;
    GetAppearanceColors(back, font, link);
    if (back == m_backColor && font == m_fontColor && link == m_linkColor) return;

    m_backColor = back;
    m_fontColor = font;
    m_linkColor = link;
    ApplyBackColor(back);
    ApplyFontColor(font);
    ApplyLinkColor(link);
    /* A game's own colours win, and ApplyParams is what re-asserts them - so
       it runs after ours rather than being skipped when a game is open. */
    if (m_isGameOpened) ApplyParams();
    RefreshUI();
}

bool QSPFrame::ApplyLinkColor(const wxColour& color)
{
    m_desc->SetLinkColor(color);
    m_objects->SetLinkColor(color);
    m_actions->SetLinkColor(color);
    m_vars->SetLinkColor(color);
    return true;
}

void QSPFrame::CallPaneFunc(wxWindowID id, QSP_BOOL toShow) const
{
    switch (id)
    {
    case ID_VARSDESC:
        QSPShowWindow(QSP_WIN_VARS, toShow);
        break;
    case ID_ACTIONS:
        QSPShowWindow(QSP_WIN_ACTS, toShow);
        break;
    case ID_OBJECTS:
        QSPShowWindow(QSP_WIN_OBJS, toShow);
        break;
    case ID_INPUT:
        QSPShowWindow(QSP_WIN_INPUT, toShow);
        break;
    case ID_VIEWPIC:
        QSPShowWindow(QSP_WIN_VIEW, toShow);
        break;
    }
}

void QSPFrame::SetOverallVolume(int percents)
{
    int id = wxNOT_FOUND;
    switch (percents)
    {
    case 0: id = ID_VOLUME0; break;
    case 20: id = ID_VOLUME20; break;
    case 40: id = ID_VOLUME40; break;
    case 60: id = ID_VOLUME60; break;
    case 80: id = ID_VOLUME80; break;
    case 100: id = ID_VOLUME100; break;
    }
    if (id >= 0) m_settingsMenu->Check(id, true);
    QSPCallbacks::SetOverallVolume((float)percents / 100);
    m_volume = percents;
}

void QSPFrame::TogglePane(wxWindowID id)
{
    bool toShow = !m_manager->GetPane(FindWindow(id)).IsShown();
    CallPaneFunc(id, (QSP_BOOL)toShow);
    ShowPane(id, toShow);
}

void QSPFrame::OpenGameFile(const wxString& fullPath)
{
    /* Existing is not the same as readable: another program may be holding the
       file open while it writes it, and an empty file is not a world. */
    std::vector<char> world;
    if (!QSPFileIO::Read(fullPath, world) || world.empty()) return;

    if (!QSPLoadGameWorldFromData(&world[0], (int)world.size(), QSP_TRUE))
    {
        ShowError();
        return;
    }

    /* Everything below can pump the event loop - reloading settings rebuilds
       the UI, and entering the start location runs game code - so m_toQuit can
       become true part way through. The bytes are owned by the vector, which
       is what lets those paths simply return. */
    UpdateGameFile(fullPath);
    m_isGameOpened = true;

    wxString configString(m_worldPath + QSP_CONFIG);
    wxString newPath(wxFileExists(configString) ? configString : m_configDefPath);
    if (newPath != m_configPath)
    {
        SaveSettings();
        m_configPath = newPath;
        LoadSettings();
    }

    wxCommandEvent dummy;
    OnNewGame(dummy);

    if (m_toQuit) return;
    UpdateTitle();
    EnableControls(true);
    m_savedGamePath.Clear();
    if (m_devServer) m_devServer->NotifyGameOpened(fullPath);
}

bool QSPFrame::OpenGameState(const wxString& fullPath)
{
    std::vector<char> state;
    if (!QSPFileIO::Read(fullPath, state) || state.empty()) return false;

    if (!QSPOpenSavedGameFromData(&state[0], (int)state.size(), QSP_TRUE))
    {
        ShowError();
        return false;
    }
    return true;
}

bool QSPFrame::SaveGameState(const wxString &fullPath, bool toRemember)
{
    std::vector<char> state;
    if (!QSPGameState::Save(state, true))
    {
        ShowError();
        return false;
    }
    if (!QSPFileIO::Write(fullPath, state)) return false;

    if (toRemember) m_savedGamePath = fullPath;
    return true;
}

/* The slot lives next to the game file, the way the game's own config does:
   one quicksave per game, and nothing left behind anywhere else. */
wxString QSPFrame::GetQuickSavePath() const
{
    if (m_gameFilePath.IsEmpty()) return wxEmptyString;
    wxFileName slotPath(m_gameFilePath);
    slotPath.SetName(slotPath.GetName() + wxT("_quick"));
    slotPath.SetExt(wxT("sav"));
    return slotPath.GetFullPath();
}

void QSPFrame::QuickSaveToSlot()
{
    if (!m_isGameOpened || !m_toProcessEvents) return;
    if (!CanSaveGame())
    {
        /* NOSAVE is the game switching saving off for now */
        ShowToast(_("This game doesn't allow saving"), QSP_TOAST_ERROR);
        return;
    }
    wxString slotPath(GetQuickSavePath());
    if (slotPath.IsEmpty()) return;
    if (SaveGameState(slotPath, false))
        ShowToast(_("Quick saved"), QSP_TOAST_SUCCESS);
    else
        ShowToast(_("Couldn't write the quick save"), QSP_TOAST_ERROR);
}

void QSPFrame::QuickLoadFromSlot()
{
    if (!m_isGameOpened || !m_toProcessEvents) return;
    wxString slotPath(GetQuickSavePath());
    /* Loading is deliberately not tied to $NOSAVE: a slot can only exist if
       the game allowed saving when it was written, and the menu keeps the
       ordinary "open saved game" available in either case. */
    if (slotPath.IsEmpty() || !wxFileExists(slotPath))
    {
        ShowToast(_("No quick save for this game yet"), QSP_TOAST_INFO);
        return;
    }
    if (OpenGameState(slotPath))
        ShowToast(_("Quick save loaded"), QSP_TOAST_SUCCESS);
}

/* The engine will not name the location the player is standing in: it exports
   the one it is *executing*, which is reset once control returns. $CURLOC is
   the way to the other one. Only used to label a slot, so a failure here just
   means a slot with a date and no name. */
wxString QSPFrame::GetCurrentLocationName() const
{
    QSP_CHAR buffer[512];
    if (!QSPCalculateStrExpression(QSP_STATIC_STR(QSP_FMT("$CURLOC")), buffer, (int)(sizeof(buffer) / sizeof(buffer[0])), QSP_FALSE))
        return wxEmptyString;
    return wxString(buffer);
}

void QSPFrame::SaveToNumberedSlot(int slot)
{
    if (!m_isGameOpened || !m_toProcessEvents) return;
    if (!CanSaveGame())
    {
        /* NOSAVE is the game switching saving off for now */
        ShowToast(_("This game doesn't allow saving"), QSP_TOAST_ERROR);
        return;
    }
    wxString slotPath(m_saveSlots.GetSlotPath(slot));
    if (slotPath.IsEmpty()) return;

    /* The location is read before the save, not after: writing the file is
       what can fail, and by then the answer is already in hand. */
    wxString location(GetCurrentLocationName());
    if (!SaveGameState(slotPath, false))
    {
        ShowToast(wxString::Format(_("Couldn't write slot %d"), slot), QSP_TOAST_ERROR);
        return;
    }
    m_saveSlots.Remember(slot, location);
    ShowToast(wxString::Format(_("Saved to slot %d"), slot), QSP_TOAST_SUCCESS);
}

void QSPFrame::LoadFromNumberedSlot(int slot)
{
    if (!m_isGameOpened || !m_toProcessEvents) return;
    wxString slotPath(m_saveSlots.GetSlotPath(slot));
    /* Loading is deliberately not tied to $NOSAVE, for the same reason the
       quick slot isn't: a slot can only exist if the game allowed saving when
       it was written. */
    if (slotPath.IsEmpty() || !wxFileExists(slotPath))
    {
        ShowToast(wxString::Format(_("Slot %d is empty"), slot), QSP_TOAST_INFO);
        return;
    }
    if (OpenGameState(slotPath))
        ShowToast(wxString::Format(_("Loaded slot %d"), slot), QSP_TOAST_SUCCESS);
}

/* Rebuilt every time the menu opens rather than kept in step with the saves:
   a slot can be written by a second copy of the player or deleted from the
   file manager, and the menu is the only place it is ever read. */
void QSPFrame::RefreshSlotLabels()
{
    if (!m_saveSlotsMenu || !m_loadSlotsMenu) return;

    for (int slot = 1; slot <= QSPSaveSlots::Count; ++slot)
    {
        wxString label(m_saveSlots.Describe(slot));
        m_saveSlotsMenu->SetLabel(ID_SAVESLOT1 + slot - 1, label);
        m_loadSlotsMenu->SetLabel(ID_LOADSLOT1 + slot - 1, label);
        /* An empty slot has nothing to load, but is a perfectly good place to
           save to - so only the load side is greyed out. */
        m_loadSlotsMenu->Enable(ID_LOADSLOT1 + slot - 1,
                                m_isGameOpened && m_saveSlots.GetInfo(slot).isUsed);
    }
}

void QSPFrame::ShowToast(const wxString &text, QSPToastKind kind)
{
    if (m_toQuit || !m_toast) return;
    m_toast->Pop(text, kind);
}

void QSPFrame::CheckLatestVersion(int type)
{
    wxWebRequest verRequest = wxWebSession::GetDefault().CreateRequest(this, QSP_LATESTVERAPI, type);

    verRequest.Start();
}

void QSPFrame::ProcessVersionResult(const wxString& versionInfo, int type)
{
    bool isSuccess = false;

    if (!versionInfo.IsEmpty())
    {
        wxRegEx versionRegEx("\"name\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        if (versionRegEx.Matches(versionInfo))
        {
            isSuccess = true;
            wxString latestVersion = versionRegEx.GetMatch(versionInfo, 1);
            if (latestVersion > QSP_VER)
            {
                wxString releaseNotes;
                wxRegEx releaseNotesRegEx("\"body\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
                if (releaseNotesRegEx.Matches(versionInfo))
                {
                    releaseNotes = releaseNotesRegEx.GetMatch(versionInfo, 1);
                    releaseNotes.Replace("\\r\\n", "\n");
                    releaseNotes.Replace("\\n", "\n");
                }
                wxString releaseUrl(QSP_LATESTVERPAGE);
                wxRegEx releaseUrlRegEx("\"html_url\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
                if (releaseUrlRegEx.Matches(versionInfo))
                    releaseUrl = releaseUrlRegEx.GetMatch(versionInfo, 1);

                UpdateAppDialog dialog(this, _("Update available"),
                    latestVersion, releaseNotes, releaseUrl);
                dialog.CenterOnParent();
                if (dialog.ShowModal() == wxID_OK)
                    QSPTools::LaunchDefaultBrowser(releaseUrl);
            }
            else if (type == UPDATE_SHOW_ALL_RESULTS)
            {
                wxMessageDialog dlgMsg(this,
                    _("Your app is already up to date."),
                    _("Info"), wxOK | wxCENTRE | wxICON_INFORMATION);
                dlgMsg.ShowModal();
            }
        }
    }

    if (!isSuccess && type == UPDATE_SHOW_ALL_RESULTS)
    {
        wxMessageDialog dlgMsg(this,
            _("Can't check the latest version!"),
            _("Error"), wxOK | wxCENTRE | wxICON_ERROR);
        dlgMsg.ShowModal();
    }
}

void QSPFrame::OnInit(wxInitEvent& event)
{
    OpenGameFile(event.GetInitString());
}

void QSPFrame::OnClose(wxCloseEvent& WXUNUSED(event))
{
    m_toast->Dismiss();
    SaveSettings();
    EnableControls(false, true);
    Destroy();
    m_toQuit = true;
}

void QSPFrame::OnTimer(wxTimerEvent& WXUNUSED(event))
{
    if (m_toProcessEvents && !QSPExecCounter(QSP_TRUE))
        ShowError();
}

void QSPFrame::OnMenu(wxCommandEvent& event)
{
    m_menuIndex = event.GetId() - ID_BEGOFDYNMENU;
}

void QSPFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    Close();
}

void QSPFrame::OnVersionRequestState(wxWebRequestEvent& event)
{
    switch (event.GetState())
    {
        case wxWebRequest::State_Completed:
            ProcessVersionResult(event.GetResponse().AsString(), event.GetId());
            break;
        case wxWebRequest::State_Failed:
        case wxWebRequest::State_Unauthorized:
            ProcessVersionResult(wxEmptyString, event.GetId());
            break;
    }
}

void QSPFrame::OnOpenGame(wxCommandEvent& WXUNUSED(event))
{
    wxFileDialog dialog(this, _("Select game file"),
                        wxEmptyString, wxEmptyString,
                        _("QSP games (*.qsp;*.gam)|*.qsp;*.gam"),
                        wxFD_OPEN);
    if (dialog.ShowModal() == wxID_OK)
        OpenGameFile(dialog.GetPath());
}

void QSPFrame::OnNewGame(wxCommandEvent& WXUNUSED(event))
{
    if (!QSPRestartGame(QSP_TRUE))
        ShowError();
}

void QSPFrame::OnOpenGameStat(wxCommandEvent& WXUNUSED(event))
{
    wxFileDialog dialog(this, _("Select saved game file"),
                        wxEmptyString, wxEmptyString,
                        _("Saved game files (*.sav)|*.sav"),
                        wxFD_OPEN);
    if (dialog.ShowModal() == wxID_OK)
        OpenGameState(dialog.GetPath());
}

void QSPFrame::OnSaveGameStat(wxCommandEvent& WXUNUSED(event))
{
    wxFileDialog dialog(this, _("Select file to save"),
                        wxEmptyString, wxT("game.sav"),
                        _("Saved game files (*.sav)|*.sav"),
                        wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (dialog.ShowModal() == wxID_OK)
        SaveGameState(dialog.GetPath());
}

void QSPFrame::OnQuickSave(wxCommandEvent& event)
{
    if (m_savedGamePath.IsEmpty())
        OnSaveGameStat(event);
    else
        SaveGameState(m_savedGamePath);
}

void QSPFrame::OnQuickSaveSlot(wxCommandEvent& WXUNUSED(event))
{
    QuickSaveToSlot();
}

void QSPFrame::OnQuickLoadSlot(wxCommandEvent& WXUNUSED(event))
{
    QuickLoadFromSlot();
}

void QSPFrame::OnSaveToSlot(wxCommandEvent& event)
{
    SaveToNumberedSlot(event.GetId() - ID_SAVESLOT1 + 1);
}

void QSPFrame::OnLoadFromSlot(wxCommandEvent& event)
{
    LoadFromNumberedSlot(event.GetId() - ID_LOADSLOT1 + 1);
}

/* Filling the slot labels in on open is what keeps them true without watching
   the filesystem. It fires for every menu, including the popup a game puts up
   through SHOWMENU, so the work is skipped unless the Game menu is the one
   being opened. */
void QSPFrame::OnMenuOpen(wxMenuEvent& event)
{
    event.Skip();
    if (event.GetMenu() == m_gameMenu) RefreshSlotLabels();
}

void QSPFrame::OnSelectFont(wxCommandEvent& WXUNUSED(event))
{
    wxFontData data;
    wxFont font(m_desc->GetTextFont());
    font.SetPointSize(m_fontSize);
    font.SetFaceName(m_fontName);
    data.EnableEffects(false);
    data.SetAllowSymbols(false);
    data.SetInitialFont(font);
    wxFontDialog dialog(this, data);
    dialog.SetTitle(_("Select font"));
    if (dialog.ShowModal() == wxID_OK)
    {
        font = dialog.GetFontData().GetChosenFont();
        m_fontSize = font.GetPointSize();
        m_fontName = font.GetFaceName();
        if (m_toProcessEvents)
            ApplyParams();
        else
        {
            ApplyFontSize(m_fontSize);
            ApplyFontName(m_fontName);
            RefreshUI();
        }
    }
}

void QSPFrame::OnUseFontSize(wxCommandEvent& WXUNUSED(event))
{
    m_toUseFontSize = !m_toUseFontSize;
    if (m_toProcessEvents)
        ApplyParams();
    else
    {
        ApplyFontSize(m_fontSize);
        RefreshUI();
    }
}

void QSPFrame::OnSelectFontColor(wxCommandEvent& WXUNUSED(event))
{
    wxColourData data;
    data.SetColour(m_fontColor);
    wxColourDialog dialog(this, &data);
    dialog.SetTitle(_("Select font color"));
    if (dialog.ShowModal() == wxID_OK)
    {
        m_fontColor = dialog.GetColourData().GetColour();
        /* An explicit choice stops the desktop theme overwriting it */
        SetUseSystemColors(false);
        if (m_toProcessEvents)
            ApplyParams();
        else
        {
            ApplyFontColor(m_fontColor);
            RefreshUI();
        }
    }
}

void QSPFrame::OnSelectBackColor(wxCommandEvent& WXUNUSED(event))
{
    wxColourData data;
    data.SetColour(m_backColor);
    wxColourDialog dialog(this, &data);
    dialog.SetTitle(_("Select background color"));
    if (dialog.ShowModal() == wxID_OK)
    {
        m_backColor = dialog.GetColourData().GetColour();
        /* An explicit choice stops the desktop theme overwriting it */
        SetUseSystemColors(false);
        if (m_toProcessEvents)
            ApplyParams();
        else
        {
            ApplyBackColor(m_backColor);
            RefreshUI();
        }
    }
}

void QSPFrame::OnSelectLinkColor(wxCommandEvent& WXUNUSED(event))
{
    wxColourData data;
    data.SetColour(m_linkColor);
    wxColourDialog dialog(this, &data);
    dialog.SetTitle(_("Select links color"));
    if (dialog.ShowModal() == wxID_OK)
    {
        m_linkColor = dialog.GetColourData().GetColour();
        /* An explicit choice stops the desktop theme overwriting it */
        SetUseSystemColors(false);
        if (m_toProcessEvents)
            ApplyParams();
        else
        {
            ApplyLinkColor(m_linkColor);
            RefreshUI();
        }
    }
}

void QSPFrame::OnUseSystemColors(wxCommandEvent& event)
{
    m_toUseSystemColors = event.IsChecked();
    ApplySystemColors();
}

/* The desktop switched between light and dark while the player was running.
   wxWidgets sends this to every window; only the frame acts on it, because the
   panes take their colours from here. */
void QSPFrame::OnSysColourChanged(wxSysColourChangedEvent& event)
{
    event.Skip();
    ApplySystemColors();
}

void QSPFrame::OnCheckUpdatesOnStartup(wxCommandEvent& WXUNUSED(event))
{
    m_toCheckUpdates = !m_toCheckUpdates;
}

void QSPFrame::OnSelectLang(wxCommandEvent& WXUNUSED(event))
{
    if (m_transHelper->AskUserForLanguage()) ReCreateGUI();
}

void QSPFrame::OnVolume(wxCommandEvent& event)
{
    int volume = 100;
    switch (event.GetId())
    {
    case ID_VOLUME0: volume = 0; break;
    case ID_VOLUME20: volume = 20; break;
    case ID_VOLUME40: volume = 40; break;
    case ID_VOLUME60: volume = 60; break;
    case ID_VOLUME80: volume = 80; break;
    }
    SetOverallVolume(volume);
}

void QSPFrame::OnToggleWinMode(wxCommandEvent& WXUNUSED(event))
{
    ShowFullScreen(!IsFullScreen());
}

void QSPFrame::OnToggleObjs(wxCommandEvent& WXUNUSED(event))
{
    TogglePane(ID_OBJECTS);
}

void QSPFrame::OnToggleActs(wxCommandEvent& WXUNUSED(event))
{
    TogglePane(ID_ACTIONS);
}

void QSPFrame::OnToggleDesc(wxCommandEvent& WXUNUSED(event))
{
    TogglePane(ID_VARSDESC);
}

void QSPFrame::OnToggleInput(wxCommandEvent& WXUNUSED(event))
{
    TogglePane(ID_INPUT);
}

void QSPFrame::OnToggleCaptions(wxCommandEvent& WXUNUSED(event))
{
    int i;
    bool toShow = !m_manager->GetPane(m_objects).HasCaption();
    wxAuiPaneInfoArray& allPanes = m_manager->GetAllPanes();
    for (i = (int)allPanes.GetCount() - 1; i >= 0; --i)
        allPanes.Item(i).CaptionVisible(toShow);
    m_manager->GetPane(m_desc).CaptionVisible(false);
    m_manager->Update();
}

void QSPFrame::OnToggleHotkeys(wxCommandEvent& WXUNUSED(event))
{
    m_toShowHotkeys = !m_toShowHotkeys;
    if (m_toProcessEvents) QSPCallbacks::RefreshInt(QSP_FALSE, QSP_FALSE);
}

void QSPFrame::OnCheckUpdates(wxCommandEvent& WXUNUSED(event))
{
    CheckLatestVersion(UPDATE_SHOW_ALL_RESULTS);
}

void QSPFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxAboutDialogInfo info;
    info.SetIcon(wxIcon(logo_big_xpm));
    info.SetName(QSP_LOGO);
    info.SetCopyright(wxT("QSP Foundation, 2001-2025"));
    QSPString version = QSPGetVersion();
    QSPString libCompiledDate = QSPGetCompiledDateTime();
    wxString guiCompiledDate(wxT(__DATE__) wxT(", ") wxT(__TIME__));
    info.SetDescription(wxString::Format(
        _("Engine version: %s\nEngine compiled: %s\nGUI compiled: %s"),
        qspToWxString(version).wx_str(),
        qspToWxString(libCompiledDate).wx_str(),
        guiCompiledDate.wx_str()
    ));
    info.SetWebSite(wxT("https://qsp.org"));
    // ----
    wxAboutBox(info, this);
}

void QSPFrame::OnLinkClicked(wxHtmlLinkEvent& event)
{
    wxString href;
    wxHtmlLinkInfo info(event.GetLinkInfo());
    if (info.GetEvent()->LeftUp())
    {
        href = info.GetHref();
        if (href.StartsWith(wxT("#")))
        {
            if (event.GetId() == m_desc->GetId())
                m_desc->LoadPage(href);
            else
                m_vars->LoadPage(href);
        }
        else if (href.Upper().StartsWith(wxT("EXEC:")))
        {
            wxString string = href.Mid(5);
            if (m_toProcessEvents && !QSPExecString(QSPMutableString(string), QSP_TRUE))
                ShowError();
        }
        else
            QSPTools::LaunchDefaultBrowser(href);
    }
    else
        event.Skip();
}

#ifdef QSPGUI_USE_WEBVIEW

namespace
{
    wxString LastErrorText()
    {
        QSPErrorInfo errorInfo = QSPGetLastErrorData();
        if (!errorInfo.ErrorNum) return _("Unknown error");
        return wxGetTranslation(qspToWxString(errorInfo.ErrorDesc));
    }

    /* Values come back as text plus a flag, so a numeric variable arrives in
       JS as a number and everything else as a string. */
    wxString VariantToText(const QSPVariant& value, bool *isNum)
    {
        *isNum = QSP_ISNUM(value.Type) != 0;
        if (*isNum)
            return wxLongLong((wxLongLong_t)QSP_NUM(value)).ToString();
        if (QSP_ISSTR(value.Type))
            return qspToWxString(QSP_STR(value));

        /* A tuple has no JS counterpart; hand over the engine's own rendering
           of it so a game can at least read it back. */
        QSP_CHAR buffer[4096];
        QSPConvertValueToString(value, buffer, sizeof(buffer) / sizeof(QSP_CHAR));
        return wxString(buffer);
    }
}

/* Everything the game's JS asks of the engine lands here, one queued call at a
   time, and every one of them answers - a promise left pending in the page
   would be indistinguishable from a hang. */
/* A game script that fails silently is indistinguishable from one that does
   nothing, so everything the shell catches lands here. It goes to the log,
   which is why --log-file exists, and to a connected editor. It deliberately
   does not interrupt the player: a warning from a game's hud.js is not the
   reader's problem, and a modal per console.warn would be unusable. */
void QSPFrame::OnScriptDiag(QSPScriptDiagEvent& event)
{
    wxString where(event.GetWhere());
    wxString detail(event.GetText());
    if (!where.IsEmpty()) detail << wxT(" [") << where << wxT("]");

    if (event.GetKind() == wxT("warning"))
        wxLogWarning(wxT("game script (%s pane): %s"), event.GetPane(), detail);
    else
        wxLogError(wxT("game script (%s pane): %s"), event.GetPane(), detail);

    if (m_devServer)
        m_devServer->NotifyScriptDiag(event.GetKind(), event.GetText(), where, event.GetPane());
}

void QSPFrame::OnScriptCall(QSPScriptCallEvent& event)
{
    QSPWebTextBox *pane = wxDynamicCast(event.GetEventObject(), QSPWebTextBox);
    if (!pane) return;

    long callId = event.GetCallId();
    wxString op(event.GetOp());
    bool toRunCode = (op != wxT("get") && op != wxT("size") && op != wxT("index"));

    /* Anything that runs game code has to wait: the engine is single-threaded
       and is already busy - the same guard the action list and EXEC: links use.
       Reads are let through, because a refresh hook fires while a forced
       refresh is still pumping the loop and reading is what it is there for. */
    if (m_toQuit || (toRunCode && !m_toProcessEvents))
    {
        pane->ResolveScriptCall(callId, false, _("The engine is busy"), false);
        return;
    }

    if (op == wxT("exec") || op == wxT("loc") || op == wxT("set") || op == wxT("add"))
    {
        wxString code;
        if (op == wxT("set") || op == wxT("add"))
        {
            wxString error;
            bool toAppend = (op == wxT("add"));
            /* set(name, index, value), add(name, value) */
            if (!QSPCode::BuildAssignment(event.GetArg(0),
                                 toAppend ? wxString() : event.GetArg(1),
                                 toAppend ? event.GetArg(1) : event.GetArg(2),
                                 toAppend, &code, &error))
            {
                pane->ResolveScriptCall(callId, false, error, false);
                return;
            }
        }
        else if (op == wxT("exec"))
        {
            code = event.GetArg(0);
        }
        else
        {
            wxString locName(event.GetArg(0));
            if (!QSPExecLocationCode(QSPMutableString(locName), QSP_TRUE))
            {
                pane->ResolveScriptCall(callId, false, LastErrorText(), false);
                ShowError();
                return;
            }
            pane->ResolveScriptCall(callId, true, wxEmptyString, false);
            return;
        }

        if (!QSPExecString(QSPMutableString(code), QSP_TRUE))
        {
            pane->ResolveScriptCall(callId, false, LastErrorText(), false);
            ShowError();
            return;
        }
        pane->ResolveScriptCall(callId, true, wxEmptyString, false);
        return;
    }

    if (op == wxT("eval") || op == wxT("evalnum"))
    {
        wxString expr(event.GetArg(0));
        QSPMutableString exprString(expr);
        if (op == wxT("evalnum"))
        {
            QSP_BIGINT result = 0;
            if (!QSPCalculateNumExpression(exprString, &result, QSP_TRUE))
            {
                pane->ResolveScriptCall(callId, false, LastErrorText(), false);
                ShowError();
                return;
            }
            pane->ResolveScriptCall(callId, true, wxLongLong((wxLongLong_t)result).ToString(), true);
            return;
        }
        QSP_CHAR buffer[4096];
        if (!QSPCalculateStrExpression(exprString, buffer, sizeof(buffer) / sizeof(QSP_CHAR), QSP_TRUE))
        {
            pane->ResolveScriptCall(callId, false, LastErrorText(), false);
            ShowError();
            return;
        }
        pane->ResolveScriptCall(callId, true, wxString(buffer), false);
        return;
    }

    /* Everything left is a read; anything else never had a handler. */
    if (toRunCode)
    {
        pane->ResolveScriptCall(callId, false, _("Unknown operation"), false);
        return;
    }

    /* Reads never run game code, so they can't fail the way the calls above
       can: an unknown name simply has no value. The name is still checked
       first, because handing the engine a malformed one makes it raise an
       error - which would overwrite the error state of whatever is running.
       Past that, the type prefix is left on: the engine strips it itself. */
    wxString name(event.GetArg(0));
    if (!QSPCode::IsValidVarName(name))
    {
        pane->ResolveScriptCall(callId, false, _("Incorrect variable name"), false);
        return;
    }
    QSPMutableString varName(name);

    if (op == wxT("get"))
    {
        long index = 0;
        QSPVariant value;
        if (!event.GetArg(1).ToLong(&index)) index = 0;
        if (!QSPGetVarValue(varName, (int)index, &value))
        {
            pane->ResolveScriptCall(callId, true, wxEmptyString, false);
            return;
        }
        bool isNum = false;
        wxString text(VariantToText(value, &isNum));
        pane->ResolveScriptCall(callId, true, text, isNum);
        return;
    }
    if (op == wxT("size"))
    {
        int count = 0;
        QSPGetVarValuesCount(varName, &count);
        pane->ResolveScriptCall(callId, true, wxString::Format(wxT("%d"), count), true);
        return;
    }
    if (op == wxT("index"))
    {
        int index = -1;
        wxString key(event.GetArg(1));
        if (!QSPGetVarIndexByString(varName, QSPMutableString(key), &index))
            index = -1;
        pane->ResolveScriptCall(callId, true, wxString::Format(wxT("%d"), index), true);
        return;
    }
}

#endif

void QSPFrame::OnObjectChange(wxCommandEvent& event)
{
    // show selection first
    m_objects->Update();
    wxThread::Sleep(10);
    // execute the handler
    if (!QSPSetSelObjectIndex(event.GetInt(), QSP_TRUE))
        ShowError();
}

void QSPFrame::OnActionChange(wxCommandEvent& event)
{
    // show selection first
    m_actions->Update();
    wxThread::Sleep(10);
    // execute the handler
    if (!QSPSetSelActionIndex(event.GetInt(), QSP_TRUE))
        ShowError();
}

void QSPFrame::OnActionDblClick(wxCommandEvent& WXUNUSED(event))
{
    if (!QSPExecuteSelActionCode(QSP_TRUE))
        ShowError();
}

void QSPFrame::OnInputTextChange(wxCommandEvent& event)
{
    wxString text(event.GetString());
    m_input->SetText(text, false);
    QSPSetInputStrText(qspStringFromLen(text.c_str(), text.Length()));
}

void QSPFrame::OnInputTextEnter(wxCommandEvent& WXUNUSED(event))
{
    if (!QSPExecUserInput(QSP_TRUE))
        ShowError();
}

void QSPFrame::OnKey(wxKeyEvent& event)
{
    event.Skip();
    // Exit fullscreen mode
    if (IsFullScreen() && event.GetKeyCode() == WXK_ESCAPE)
    {
        ShowFullScreen(false);
        return;
    }
    // Process key pressed event
    if (event.GetKeyCode() == WXK_SPACE)
        m_keyPressedWhileDisabled = true;
#ifdef QSPGUI_USE_WEBVIEW
    /* Quick save / quick load are menu accelerators, and accelerators never
       see a key pressed inside a browser pane - that one comes back to us as
       a synthetic event instead, which is what we answer here. */
    if (!event.HasModifiers() &&
        (event.GetEventObject() == m_desc || event.GetEventObject() == m_vars))
    {
        switch (event.GetKeyCode())
        {
        case WXK_F5:
            QuickSaveToSlot();
            return;
        case WXK_F9:
            QuickLoadFromSlot();
            return;
        }
    }
    /* F12 wherever it is pressed, not just inside a pane: the devtools are
       for the pane the game's script is failing in, and the author may well
       be looking at the action list when they reach for them. */
    if (!event.HasModifiers() && event.GetKeyCode() == WXK_F12 &&
        QSPMainTextBox::IsDevModeEnabled())
    {
        QSPMainTextBox *target = (event.GetEventObject() == m_vars ? m_vars : m_desc);
        if (target) target->ShowDevTools();
        return;
    }
#endif
    // Process action shortcut
    if (m_toProcessEvents && !event.HasModifiers() && wxWindow::FindFocus() != m_input)
    {
        int ind = -1;
        int actsCount = QSPGetActions(NULL, 0);
        switch (event.GetKeyCode())
        {
        case '1': case WXK_NUMPAD1: case WXK_NUMPAD_END: ind = 0; break;
        case '2': case WXK_NUMPAD2: case WXK_NUMPAD_DOWN: ind = 1; break;
        case '3': case WXK_NUMPAD3: case WXK_NUMPAD_PAGEDOWN: ind = 2; break;
        case '4': case WXK_NUMPAD4: case WXK_NUMPAD_LEFT: ind = 3; break;
        case '5': case WXK_NUMPAD5: case WXK_CLEAR: ind = 4; break;
        case '6': case WXK_NUMPAD6: case WXK_NUMPAD_RIGHT: ind = 5; break;
        case '7': case WXK_NUMPAD7: case WXK_NUMPAD_HOME: ind = 6; break;
        case '8': case WXK_NUMPAD8: case WXK_NUMPAD_UP: ind = 7; break;
        case '9': case WXK_NUMPAD9: case WXK_NUMPAD_PAGEUP: ind = 8; break;
        case WXK_SPACE:
            if (actsCount == 1) ind = 0;
            break;
        }
        if (ind >= 0 && ind < actsCount)
        {
            wxCommandEvent e;
            if (QSPSetSelActionIndex(ind, QSP_TRUE))
                OnActionDblClick(e);
            else
                ShowError();
        }
    }
}

void QSPFrame::OnWheel(wxMouseEvent& event)
{
    wxWindow *win = wxFindWindowAtPoint(wxGetMousePosition());
    if (win) win->ScrollLines(-event.GetWheelRotation() / event.GetWheelDelta() * event.GetLinesPerAction());
}

void QSPFrame::OnMouseClick(wxMouseEvent& event)
{
    event.Skip();
    m_keyPressedWhileDisabled = true;
}

void QSPFrame::OnPaneClose(wxAuiManagerEvent& event)
{
    if (m_toProcessEvents)
        CallPaneFunc(event.GetPane()->window->GetId(), QSP_FALSE);
    else
        event.Veto();
}

void QSPFrame::OnDropFiles(wxDropFilesEvent& event)
{
    if (event.GetNumberOfFiles() && (!m_isGameOpened || m_toProcessEvents))
    {
        wxFileName path(*event.GetFiles());
        path.MakeAbsolute();
        OpenGameFile(path.GetFullPath());
    }
}
