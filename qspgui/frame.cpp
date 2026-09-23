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
#include "saveslotsdlg.h"
#include "comtools.h"
#include "callbacks_gui.h"
#include "devserver.h"

#include <wx/utils.h>
#include <chrono>
#include <cstdlib>
#include <thread>

#include "icons/logo.xpm"
#include "icons/logo_big.xpm"
#include "icons/open.xpm"
#include "icons/new.xpm"
#include "icons/exit.xpm"
#include "icons/statusopen.xpm"
#include "icons/statussave.xpm"
#include "icons/windowmode.xpm"
#include "icons/about.xpm"

#ifdef __WXMSW__
namespace
{
    /* Windows hands a popup menu the light theme's check mark whatever the
       menu itself is painted in, so on a dark menu the tick and the radio dot
       are black on near-black and simply are not there. A menu item that
       carries its own bitmaps is drawn from those instead, so the mark is
       drawn here in the colour the menu text uses.

       Deliberately hard-edged: the bitmap is blitted through a colour mask, so
       an antialiased edge would come out fringed in the mask colour. A tick
       two pixels thick is what Windows draws anyway. */
    wxBitmap BuildMenuMark(wxWindow *window, bool isRadio)
    {
        const int size = window->FromDIP(14);
        /* Nothing in a mark ever uses it, so it can stand for "not drawn" */
        const wxColour transparent(0xFF, 0x00, 0xFF);

        wxBitmap mark(size, size);
        {
            wxMemoryDC dc(mark);
            dc.SetBackground(wxBrush(transparent));
            dc.Clear();

            wxColour color(wxSystemSettings::GetColour(wxSYS_COLOUR_MENUTEXT));
            if (isRadio)
            {
                dc.SetPen(wxPen(color));
                dc.SetBrush(wxBrush(color));
                dc.DrawCircle(size / 2, size / 2, wxMax(2, size / 5));
            }
            else
            {
                wxPoint tick[3] = {
                    wxPoint(size * 22 / 100, size * 52 / 100),
                    wxPoint(size * 42 / 100, size * 74 / 100),
                    wxPoint(size * 80 / 100, size * 26 / 100)
                };
                dc.SetPen(wxPen(color, wxMax(2, size / 7)));
                dc.DrawLines(3, tick);
            }
        }
        mark.SetMask(new wxMask(mark, transparent));
        return mark;
    }

    void ApplyMenuMarks(wxMenu *menu, wxWindow *window)
    {
        wxMenuItemList& items = menu->GetMenuItems();
        for (wxMenuItemList::iterator i = items.begin(); i != items.end(); ++i)
        {
            wxMenuItem *item = *i;
            if (item->IsSubMenu())
            {
                ApplyMenuMarks(item->GetSubMenu(), window);
                continue;
            }
            /* Only items with no bitmap of their own: one that has one is
               already drawn from it, and its check state is the menu's to
               show, not ours to overwrite. */
            if (!item->IsCheckable() || item->GetBitmap().IsOk()) continue;

            item->SetBitmaps(BuildMenuMark(window, item->GetKind() == wxITEM_RADIO));
        }
    }
}
#endif // __WXMSW__

BEGIN_EVENT_TABLE(QSPFrame, wxFrame)
    EVT_INIT(QSPFrame::OnInit)
    EVT_CLOSE(QSPFrame::OnClose)
    EVT_IDLE(QSPFrame::OnIdle)
    EVT_TIMER(ID_TIMER, QSPFrame::OnTimer)
    EVT_MENU(wxID_EXIT, QSPFrame::OnQuit)
    EVT_MENU(ID_OPENGAME, QSPFrame::OnOpenGame)
    EVT_MENU(ID_NEWGAME, QSPFrame::OnNewGame)
    EVT_MENU(ID_OPENGAMESTAT, QSPFrame::OnOpenGameStat)
    EVT_MENU(ID_SAVEGAMESTAT, QSPFrame::OnSaveGameStat)
    EVT_MENU(ID_QUICKSAVE, QSPFrame::OnQuickSave)
    EVT_MENU(ID_QUICKSAVESLOT, QSPFrame::OnQuickSaveSlot)
    EVT_MENU(ID_QUICKLOADSLOT, QSPFrame::OnQuickLoadSlot)
    EVT_MENU(ID_SAVESLOTS, QSPFrame::OnSaveSlots)
    EVT_MENU(ID_SELECTFONT, QSPFrame::OnSelectFont)
    EVT_MENU(ID_USEFONTSIZE, QSPFrame::OnUseFontSize)
    EVT_MENU(ID_SELECTFONTCOLOR, QSPFrame::OnSelectFontColor)
    EVT_MENU(ID_SELECTBACKCOLOR, QSPFrame::OnSelectBackColor)
    EVT_MENU(ID_SELECTLINKCOLOR, QSPFrame::OnSelectLinkColor)
    EVT_MENU(ID_USESYSTEMCOLORS, QSPFrame::OnSelectTheme)
    EVT_MENU(ID_LIGHTTHEME, QSPFrame::OnSelectTheme)
    EVT_MENU(ID_DARKTHEME, QSPFrame::OnSelectTheme)
    EVT_SYS_COLOUR_CHANGED(QSPFrame::OnSysColourChanged)
    EVT_MENU(ID_CHECKUPDATESONSTARTUP, QSPFrame::OnCheckUpdatesOnStartup)
    EVT_MENU(ID_SAVEONEXIT, QSPFrame::OnSaveOnExit)
    EVT_MENU(ID_RESUMEONLAUNCH, QSPFrame::OnResumeOnLaunch)
    EVT_MENU(ID_SELECTLANG, QSPFrame::OnSelectLang)
    EVT_MENU(ID_TOGGLEWINMODE, QSPFrame::OnToggleWinMode)
    EVT_MENU(ID_TOGGLEOBJS, QSPFrame::OnToggleObjs)
    EVT_MENU(ID_TOGGLEACTS, QSPFrame::OnToggleActs)
    EVT_MENU(ID_TOGGLEDESC, QSPFrame::OnToggleDesc)
    EVT_MENU(ID_TOGGLEINPUT, QSPFrame::OnToggleInput)
    EVT_MENU(ID_TOGGLECAPTIONS, QSPFrame::OnToggleCaptions)
    EVT_MENU(ID_TOGGLEHOTKEYS, QSPFrame::OnToggleHotkeys)
    EVT_MENU(ID_TOGGLEDOCKPIXELS, QSPFrame::OnToggleDockPixels)
    EVT_UPDATE_UI_RANGE(ID_TOGGLEOBJS, ID_TOGGLEDOCKPIXELS, QSPFrame::OnUpdateShowHide)
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
    EVT_SIZE(QSPFrame::OnSize)
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
    m_gameMenu->AppendSeparator();
    m_gameMenu->Append(ID_SAVESLOTS, wxT("-"));
    // ------------
    wxMenu *wndsMenu = new wxMenu;
    wndsMenu->AppendCheckItem(ID_TOGGLEOBJS, wxT("-"));
    wndsMenu->AppendCheckItem(ID_TOGGLEACTS, wxT("-"));
    wndsMenu->AppendCheckItem(ID_TOGGLEDESC, wxT("-"));
    wndsMenu->AppendCheckItem(ID_TOGGLEINPUT, wxT("-"));
    wndsMenu->AppendSeparator();
    wndsMenu->AppendCheckItem(ID_TOGGLECAPTIONS, wxT("-"));
    wndsMenu->AppendCheckItem(ID_TOGGLEHOTKEYS, wxT("-"));
    wndsMenu->AppendSeparator();
    wndsMenu->AppendCheckItem(ID_TOGGLEDOCKPIXELS, wxT("-"));
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
    colorsMenu->AppendRadioItem(ID_USESYSTEMCOLORS, wxT("-"));
    colorsMenu->AppendRadioItem(ID_LIGHTTHEME, wxT("-"));
    colorsMenu->AppendRadioItem(ID_DARKTHEME, wxT("-"));
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
    m_settingsMenu->AppendCheckItem(ID_SAVEONEXIT, wxT("-"));
    m_settingsMenu->AppendCheckItem(ID_RESUMEONLAUNCH, wxT("-"));
    m_settingsMenu->AppendSeparator();
    wxMenuItem *settingsWinModeItem = new wxMenuItem(m_settingsMenu, ID_TOGGLEWINMODE, wxT("-"));
    settingsWinModeItem->SetBitmap(wxBitmap(windowmode_xpm));
    /* F11 as well as the Alt-Enter the label shows: it is the key most
       programs use for this, and the label only has room for one */
    settingsWinModeItem->AddExtraAccel(wxAcceleratorEntry(wxACCEL_NORMAL, WXK_F11, ID_TOGGLEWINMODE));
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
    m_loadingOverlay = new QSPLoadingOverlay(this);
    // --------------------------------------
    SetMinClientSize(wxSize(450, 300));
    SetOverallVolume(100);
    m_savedGamePath.Clear();
    m_worldPath.Clear();
    m_hasCustomCss = false;
    m_hasCustomJs = false;
    m_toQuit = false;
    m_toSaveOnExit = false;
    m_toResumeOnLaunch = false;
    m_keyPressedWhileDisabled = false;
    m_isGameOpened = false;
    m_isManagerUpdatePending = false;
    m_isRescalingDocks = false;
    m_isLoading = false;
    m_isCloseDeferred = false;
    m_lastLayoutSize = wxSize(0, 0);
    m_colorTheme = QSP_THEME_SYSTEM;
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
    cfg.Write(wxT("General/KeepPanelPixels"), m_dockLayout.IsKeepingPixels());
    cfg.Write(wxT("General/CheckUpdates"), m_toCheckUpdates);
    cfg.Write(wxT("Colors/Theme"), m_colorTheme);
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

    wxFileConfig cfg(wxEmptyString, wxEmptyString, m_configPath);
    /* The desktop's light or dark setting is followed on a first run, so the
       player's frame matches everything else on screen. The setting used to
       be a plain on/off flag for exactly that, and an existing config that
       had it off asked for the classic light frame. */
    bool toUseSystemColors;
    cfg.Read(wxT("Colors/UseSystemColors"), &toUseSystemColors, true);
    cfg.Read(wxT("Colors/Theme"), &m_colorTheme, (toUseSystemColors ? QSP_THEME_SYSTEM : QSP_THEME_LIGHT));
    if (m_colorTheme < QSP_THEME_SYSTEM || m_colorTheme > QSP_THEME_DARK)
        m_colorTheme = QSP_THEME_SYSTEM;
    /* The page, the text and the links belong to the game: these are only the
       fallbacks it inherits when it sets none of them, and the player's own
       historic ones at that - the theme has no say in them. The stored form is
       QSP's own 0xBBGGRR, which is also what wxColour's packed constructor
       reads, so the defaults are built from components rather than written as
       literals. */
    cfg.Read(wxT("Colors/BackColor"), &temp, (int)QSPTools::PackColor(wxColour(0xE0, 0xE0, 0xE0)));
    m_backColor = wxColour(temp);
    cfg.Read(wxT("Colors/FontColor"), &temp, (int)QSPTools::PackColor(wxColour(0x00, 0x00, 0x00)));
    m_fontColor = wxColour(temp);
    cfg.Read(wxT("Colors/LinkColor"), &temp, (int)QSPTools::PackColor(wxColour(0x00, 0x00, 0xFF)));
    m_linkColor = wxColour(temp);
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
    bool toKeepPanelPixels;
    cfg.Read(wxT("General/KeepPanelPixels"), &toKeepPanelPixels, false);
    cfg.Read(wxT("General/CheckUpdates"), &m_toCheckUpdates, true);
    {
        /* Always the player's own file, not a game's: see m_toSaveOnExit */
        wxFileConfig playerCfg(wxEmptyString, wxEmptyString, m_configDefPath);
        playerCfg.Read(wxT("Session/SaveOnExit"), &m_toSaveOnExit, false);
        playerCfg.Read(wxT("Session/ResumeOnLaunch"), &m_toResumeOnLaunch, false);
    }
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
    UpdateSessionMenu();
    SetColorTheme(m_colorTheme);
    ApplyThemeToDockArt();
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
    /* The dock sizes just restored belong to the window size being restored
       with them, so that is the size every later resize is measured against.
       Whatever the window was sized at while it was being built is not. */
    m_dockLayout.SetKeepPixels(toKeepPanelPixels);
    m_lastLayoutSize = wxSize(0, 0);
    SetSize(x, y, w, h);
    if (m_lastLayoutSize.GetWidth() < 1) m_lastLayoutSize = GetClientSize();
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
    m_gameMenu->Enable(ID_SAVESLOTS, status);
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
                    m_manager->RestorePane(*pane);
                    pane->Hide();
                    RequestManagerUpdate();
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
   and a single action can toggle several panes. Collapse a burst of toggles
   into one relayout. */
void QSPFrame::RequestManagerUpdate()
{
    if (m_isManagerUpdatePending) return;
    m_isManagerUpdatePending = true;
    CallAfter(&QSPFrame::DoManagerUpdate);
}

/* Deliberately not frozen. Freeze() on Windows is WM_SETREDRAW, which works by
   clearing the window's visible bit - and visibility is inherited, so every
   browser pane in the frame counts as hidden until Thaw(), and then has to be
   re-presented from nothing. A VIEW that only opens the picture blanked the
   description, the lists and the menu bar for a frame. wxAUI already repaints
   exactly the panes whose rectangle changed, and nothing else. */
void QSPFrame::DoManagerUpdate()
{
    if (!m_isManagerUpdatePending) return;
    m_isManagerUpdatePending = false;
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
    /* qQSP picks these two up from the game folder without being asked, so a
       game moving over from it may rely on them and never name them in
       $USERCSSFILE / $USERJSFILE. Checked once per folder, as qQSP does, rather
       than on every refresh. */
    m_hasCustomCss = wxFileExists(m_worldPath + wxT("custom.css"));
    m_hasCustomJs = wxFileExists(m_worldPath + wxT("custom.js"));
    NotifyPanesOfGamePath();
}

/* The game folder is where every pane resolves the game's pictures against,
   and in the browser renderer it is also a virtual host that has to be
   registered with each browser control before a document can fetch anything
   from it. A pane reads the folder through its path provider, so handing the
   provider over again is how it is told the folder has moved; the classic
   panes only keep the pointer, so for them this is a no-op.

   It has to be pushed rather than waited for. A pane that registers the host
   only when its own document finishes loading gets nothing when the player is
   started empty and a game is opened afterwards - which is the ordinary way to
   open one - and then every picture in it fails to load. */
void QSPFrame::NotifyPanesOfGamePath()
{
    m_desc->SetPathProvider(this);
    m_vars->SetPathProvider(this);
    m_objects->SetPathProvider(this);
    m_actions->SetPathProvider(this);
    m_imgView->SetPathProvider(this);
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

/* The lines around the one that failed. The engine hands the error a single
   line of code, which is rarely enough to see what went wrong; the location's
   own code is there for the asking and is not modified by reading it, so the
   report carries a window of it with the failing line marked.

   The engine counts a location's code from one and the stored lines from zero,
   hence the offset - it is the same one qspExecCode applies on the way in. */
void QSPFrame::AppendErrorCode(wxString &report, const QSPErrorInfo &errorInfo,
                               const wxString &rule) const
{
    wxString locName(qspToWxString(errorInfo.LocName));
    if (locName.IsEmpty()) return;

    QSPMutableString location(locName);
    wxString title;
    int linesCount;
    if (errorInfo.ActIndex < 0)
    {
        linesCount = QSPGetLocationCode(location, 0, 0);
        title = wxString::Format(wxT("CODE - %s"), locName);
    }
    else
    {
        linesCount = QSPGetLocationActionCode(location, errorInfo.ActIndex, 0, 0);
        title = wxString::Format(wxT("CODE - %s, action %d"), locName, errorInfo.ActIndex + 1);
    }
    if (linesCount <= 0) return;

    std::vector<QSPLineInfo> lines(linesCount);
    if (errorInfo.ActIndex < 0)
        QSPGetLocationCode(location, &lines[0], linesCount);
    else
        QSPGetLocationActionCode(location, errorInfo.ActIndex, &lines[0], linesCount);

    int failed = -1;
    for (int i = 0; i < linesCount; ++i)
    {
        if (lines[i].LineNum + 1 == errorInfo.TopLineNum) { failed = i; break; }
    }
    if (failed < 0) return;

    const int context = 4;
    int first = wxMax(0, failed - context);
    int last = wxMin(linesCount - 1, failed + context);

    report << rule << wxT("  ") << title << wxT("\n") << rule;
    for (int i = first; i <= last; ++i)
    {
        /* The failing line is marked, not merely numbered: a line number is no
           use to whoever pasted this without the file in front of them. */
        report << wxString::Format(wxT("%s %4d | %s\n"),
                                   (i == failed ? wxT("  >>") : wxT("    ")),
                                   lines[i].LineNum + 1,
                                   qspToWxString(lines[i].Line));
    }
}

/* One block of plain text somebody can paste into a bug report: what failed,
   exactly where, and the code around it. Nothing that is true of every report
   - no timestamps, no machine paths - because that is what buries the three
   lines that matter. Framed in ASCII so it survives being pasted somewhere
   that keeps none of the formatting. */
wxString QSPFrame::BuildErrorReport(const QSPErrorInfo &errorInfo) const
{
    static const wxChar *heavy = wxT("================================================================\n");
    static const wxChar *light = wxT("----------------------------------------------------------------\n");

    wxString locName(qspToWxString(errorInfo.LocName));
    wxString report;

    report << heavy
           << wxString::Format(wxT("  QSP ERROR %d - %s\n"), errorInfo.ErrorNum,
                               wxGetTranslation(qspToWxString(errorInfo.ErrorDesc)))
           << heavy;

    /* Aligned labels rather than prose: this gets scanned, not read */
    if (!locName.IsEmpty())
    {
        report << wxString::Format(wxT("  Location  : %s (%s)\n"), locName,
                                   (errorInfo.ActIndex < 0
                                        ? wxString(wxT("on visit"))
                                        : wxString::Format(wxT("on action %d"), errorInfo.ActIndex + 1)));
        report << wxString::Format(wxT("  Line      : %d of the location, %d of the code being run\n"),
                                   errorInfo.TopLineNum, errorInfo.IntLineNum);
    }
    else
    {
        /* No location means the engine was between them - navigating, or
           running code handed to it from outside - and then the location the
           reader is standing in is the only thing that places the error at
           all. Safe to ask for here: everything above is already copied out of
           the engine's own memory, so evaluating an expression cannot pull it
           out from under us. */
        wxString current(m_isGameOpened ? GetCurrentLocationName() : wxString());
        if (!current.IsEmpty())
            report << wxString::Format(wxT("  Location  : none; the player is standing in %s\n"), current);
        else
            report << wxT("  Location  : none\n");
        if (errorInfo.IntLineNum)
            report << wxString::Format(wxT("  Line      : %d of the code being run\n"), errorInfo.IntLineNum);
    }

    wxString line(qspToWxString(errorInfo.IntLine));
    if (!line.IsEmpty())
        report << wxT("  Statement : ") << line << wxT("\n");

    /* The file's name, not where this particular machine happens to keep it */
    if (!m_gameFilePath.IsEmpty())
        report << wxT("  Game      : ") << wxFileName(m_gameFilePath).GetFullName() << wxT("\n");
    /* A bug report wants the exact commit, so this line takes the build id */
    report << wxT("  Player    : ") << QSP_BUILD << wxT(" (") << QSPTools::GetPlatform()
#ifdef QSPGUI_USE_WEBVIEW
           << wxT(", browser renderer)\n");
#else
           << wxT(", classic renderer)\n");
#endif

    AppendErrorCode(report, errorInfo, light);
    report << heavy;
    return report;
}

void QSPFrame::ShowError()
{
    if (m_toQuit) return;
    QSPErrorInfo errorInfo = QSPGetLastErrorData();
    if (!errorInfo.ErrorNum) return; // error is undefined
    /* An error never goes under the overlay: it would cover the dialog and
       swallow the clicks meant for it. Whatever was loading has failed. */
    if (m_isLoading) EndLoading();
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
    /* The dialog says what a reader needs; the report behind the copy button
       says what whoever has to fix it needs. */
    dialog.SetCopyText(BuildErrorReport(errorInfo));
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
    menuBar->SetLabel(ID_SAVESLOTS, _("Save slo&ts...\tF6"));
    menuBar->SetLabel(ID_TOGGLEOBJS, _("&Objects\tCtrl-1"));
    menuBar->SetLabel(ID_TOGGLEACTS, _("&Actions\tCtrl-2"));
    menuBar->SetLabel(ID_TOGGLEDESC, _("A&dditional desc\tCtrl-3"));
    menuBar->SetLabel(ID_TOGGLEINPUT, _("&Input area\tCtrl-4"));
    menuBar->SetLabel(ID_TOGGLECAPTIONS, _("&Captions\tCtrl-5"));
    menuBar->SetLabel(ID_TOGGLEHOTKEYS, _("&Hotkeys for actions\tCtrl-6"));
    menuBar->SetLabel(ID_TOGGLEDOCKPIXELS, _("Keep panel sizes in &pixels"));
    menuBar->SetLabel(ID_SHOWHIDE, _("&Show / Hide"));
    menuBar->SetLabel(ID_FONT, _("&Font"));
    menuBar->SetLabel(ID_SELECTFONT, _("Select &font...\tAlt-F"));
    menuBar->SetLabel(ID_USEFONTSIZE, _("&Always use selected font size"));
    menuBar->SetLabel(ID_COLORS, _("&Colors"));
    menuBar->SetLabel(ID_SELECTFONTCOLOR, _("Select font &color...\tAlt-C"));
    menuBar->SetLabel(ID_SELECTBACKCOLOR, _("Select &background color...\tAlt-B"));
    menuBar->SetLabel(ID_SELECTLINKCOLOR, _("Select l&inks color...\tAlt-I"));
    menuBar->SetLabel(ID_USESYSTEMCOLORS, _("Follow s&ystem light / dark theme"));
    menuBar->SetLabel(ID_LIGHTTHEME, _("Li&ght theme"));
    menuBar->SetLabel(ID_DARKTHEME, _("Dar&k theme"));
    menuBar->SetLabel(ID_VOLUME, _("Sound &volume"));
    menuBar->SetLabel(ID_VOLUME0, _("No sound\tAlt-1"));
    menuBar->SetLabel(ID_VOLUME20, _("20%\tAlt-2"));
    menuBar->SetLabel(ID_VOLUME40, _("40%\tAlt-3"));
    menuBar->SetLabel(ID_VOLUME60, _("60%\tAlt-4"));
    menuBar->SetLabel(ID_VOLUME80, _("80%\tAlt-5"));
    menuBar->SetLabel(ID_VOLUME100, _("Initial volume\tAlt-6"));
    menuBar->SetLabel(ID_CHECKUPDATESONSTARTUP, _("Check for updates on startup"));
    menuBar->SetLabel(ID_SAVEONEXIT, _("Save the game on e&xit"));
    menuBar->SetLabel(ID_RESUMEONLAUNCH, _("&Resume it on the next launch"));
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
    ApplyThemeToMenus();
    m_manager->Update();
}

/* The ticks and dots beside the checkable menu items. Unlike the pane captions
   this is not part of ApplyThemeToDockArt: Windows decides a menu's light or
   dark look once, before the first window exists, so it is settled for the run
   and only has to be done when the labels are built. */
void QSPFrame::ApplyThemeToMenus()
{
#ifdef __WXMSW__
    /* A light menu draws its own marks perfectly well, and giving an item a
       bitmap turns the whole menu owner-drawn - so this is only worth doing
       where the stock mark cannot be seen. */
    if (!wxSystemSettings::GetAppearance().IsDark()) return;

    wxMenuBar *menuBar = GetMenuBar();
    if (!menuBar) return;

    for (size_t i = 0; i < menuBar->GetMenuCount(); ++i)
        ApplyMenuMarks(menuBar->GetMenu(i), this);
#endif
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

void QSPFrame::SetColorTheme(int theme)
{
    m_colorTheme = theme;
    if (!m_settingsMenu) return;

    wxWindowID id;
    switch (theme)
    {
    case QSP_THEME_LIGHT: id = ID_LIGHTTHEME; break;
    case QSP_THEME_DARK: id = ID_DARKTHEME; break;
    default: id = ID_USESYSTEMCOLORS; break;
    }
    m_settingsMenu->Check(id, true);
}

bool QSPFrame::IsDarkTheme() const
{
    switch (m_colorTheme)
    {
    case QSP_THEME_LIGHT: return false;
    case QSP_THEME_DARK: return true;
    default: break;
    }
    return wxSystemSettings::GetAppearance().IsDark();
}

/* The captions over the panes, the sashes between them and the border round
   the lot. Windows draws the menus and the dropdowns and only lets that be
   decided once, at startup - but these are ours, so they follow the theme the
   moment it is picked. */
void QSPFrame::ApplyThemeToDockArt()
{
    wxAuiDockArt *art = (m_manager ? m_manager->GetArtProvider() : NULL);
    if (!art) return;

    bool isDark = IsDarkTheme();
    wxColour background(isDark ? wxColour(0x2B, 0x2B, 0x2B) : wxColour(0xF0, 0xF0, 0xF0));
    wxColour border(isDark ? wxColour(0x18, 0x18, 0x18) : wxColour(0xA0, 0xA0, 0xA0));

    art->SetColour(wxAUI_DOCKART_BACKGROUND_COLOUR, background);
    art->SetColour(wxAUI_DOCKART_SASH_COLOUR, background);
    art->SetColour(wxAUI_DOCKART_GRIPPER_COLOUR, background);
    art->SetColour(wxAUI_DOCKART_BORDER_COLOUR, border);
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_COLOUR,
        (isDark ? wxColour(0x3C, 0x40, 0x45) : wxColour(0xC4, 0xD9, 0xF2)));
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_GRADIENT_COLOUR,
        (isDark ? wxColour(0x2F, 0x32, 0x36) : wxColour(0xEC, 0xF3, 0xFC)));
    art->SetColour(wxAUI_DOCKART_ACTIVE_CAPTION_TEXT_COLOUR,
        (isDark ? wxColour(0xF0, 0xF0, 0xF0) : wxColour(0x00, 0x00, 0x00)));
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_COLOUR,
        (isDark ? wxColour(0x2B, 0x2B, 0x2B) : wxColour(0xE2, 0xE2, 0xE2)));
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_GRADIENT_COLOUR,
        (isDark ? wxColour(0x24, 0x24, 0x24) : wxColour(0xF4, 0xF4, 0xF4)));
    art->SetColour(wxAUI_DOCKART_INACTIVE_CAPTION_TEXT_COLOUR,
        (isDark ? wxColour(0xBD, 0xBD, 0xBD) : wxColour(0x40, 0x40, 0x40)));

    /* The toast is the player speaking, not the game, so it is painted like
       the frame rather than like the page. */
    wxColour chromeText(isDark ? wxColour(0xF0, 0xF0, 0xF0) : wxColour(0x1A, 0x1A, 0x1A));
    if (m_toast)
        m_toast->SetColors(background, chromeText);
    /* The loading overlay is the player too, and it is what the reader looks
       at for seconds at a time - a light card on a dark desktop would be the
       one bright thing in the window. */
    if (m_loadingOverlay)
        m_loadingOverlay->SetColors(background, chromeText);

    RequestManagerUpdate();
    Refresh();
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

/* The overlay and the background thread live here rather than in the panes or
   the callbacks because this is the only place a load is started from: the
   engine's own OPENQST runs inside game code and cannot be moved off this
   thread, so it is left alone. */
void QSPFrame::BeginLoading(const wxString &stage, const wxString &detail)
{
    m_isLoading = true;
    if (m_toast) m_toast->Dismiss();
    if (m_loadingOverlay) m_loadingOverlay->Begin(stage, detail);
}

void QSPFrame::SetLoadingStage(const wxString &stage)
{
    if (m_loadingOverlay) m_loadingOverlay->SetStage(stage);
}

void QSPFrame::EndLoading()
{
    m_isLoading = false;
    if (m_loadingOverlay) m_loadingOverlay->End();
}

void QSPFrame::RunLoadingStep(const std::function<void()> &work,
                              std::atomic<wxFileOffset> *done,
                              std::atomic<wxFileOffset> *total)
{
    if (!m_loadingOverlay || !m_loadingOverlay->IsRunning() || m_toQuit)
    {
        /* No overlay to keep alive, so there is nothing to gain by leaving
           this thread - and plenty to lose. */
        work();
        return;
    }

    std::atomic<bool> isFinished(false);
    std::exception_ptr failure;
    std::thread worker([&work, &isFinished, &failure]()
    {
        try
        {
            work();
        }
        catch (...)
        {
            /* Carried back rather than thrown here: an exception leaving a
               std::thread's function calls terminate(). */
            failure = std::current_exception();
        }
        isFinished = true;
    });

    /* Nothing the reader does may reach the engine while its world is half
       written. Every path into it is already behind m_toProcessEvents, which
       goes down here for the length of the step, and the window disabler
       closes what is left: the menu bar, dropped files, the frame's own keys. */
    bool oldToProcessEvents = m_toProcessEvents;
    m_toProcessEvents = false;
    {
        wxWindowDisabler disabler(m_loadingOverlay);
        while (!isFinished.load())
        {
            if (done && total) m_loadingOverlay->SetProgress(done->load(), total->load());
            m_loadingOverlay->Tick();
            /* Paints, and the messages Windows needs answered for the window
               to count as alive - which is the whole point of this loop. */
            wxTheApp->Yield(true);
            wxMilliSleep(15);
        }
    }
    m_toProcessEvents = oldToProcessEvents;
    worker.join();

    if (done && total) m_loadingOverlay->SetProgress(done->load(), total->load());
    if (failure) std::rethrow_exception(failure);
}

void QSPFrame::OpenGameFile(const wxString& fullPath, bool toResume)
{
    std::vector<char> world;
    bool isLoaded = false;
    {
        QSPLoadingScope loading(this, _("Opening the game"), wxFileName(fullPath).GetFullName());

        /* Existing is not the same as readable: another program may be holding the
           file open while it writes it, and an empty file is not a world. */
        std::atomic<wxFileOffset> read(0), size(0);
        bool isRead = false;
        RunLoadingStep([&]() { isRead = QSPFileIO::Read(fullPath, world, read, size); }, &read, &size);
        if (!isRead || world.empty()) return;

        /* The long one: tens of megabytes of ciphered text decoded on one core.
           Off the UI thread because QSPLoadGameWorldFromData is pure - it parses
           into the engine's own arrays and calls nothing back out - so the only
           rule to keep is that this thread does not touch the engine meanwhile,
           which is what RunLoadingStep is for. */
        loading.SetStage(_("Unpacking the game world"));
        RunLoadingStep([&]() {
            isLoaded = (QSPLoadGameWorldFromData(&world[0], (int)world.size(), QSP_TRUE) != QSP_FALSE);
        });

        if (isLoaded)
        {
            loading.SetStage(_("Starting the game"));

            /* Reloading settings rebuilds the UI and can pump the event loop, so
               m_toQuit can become true part way through. The bytes are owned by
               the vector, which is what lets those paths simply return. */
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
        }
    }

    /* The overlay is down by here, for the same reason as in OpenGameState:
       an error dialog under it cannot be seen or clicked - the overlay's timer
       keeps running inside the dialog's modal loop and puts it on top - and
       the start location is game code that may print, ask or fail. */
    if (!isLoaded)
    {
        ShowError();
        return;
    }

    /* The exit save goes straight into the new world. Starting the game first
       would run its opening location - music, a MSG, a name prompt - only for
       the save to replace all of it a moment later. */
    if (!toResume || !ResumeFromExitSave())
    {
        wxCommandEvent dummy;
        OnNewGame(dummy);
    }

    if (m_toQuit) return;
    UpdateTitle();
    EnableControls(true);
    m_savedGamePath.Clear();
    if (m_devServer) m_devServer->NotifyGameOpened(fullPath);
}

bool QSPFrame::OpenGameState(const wxString& fullPath)
{
    std::vector<char> state;
    std::atomic<wxFileOffset> read(0), size(0);
    bool isRead = false;
    {
        QSPLoadingScope loading(this, _("Loading the saved game"), wxFileName(fullPath).GetFullName());
        RunLoadingStep([&]() { isRead = QSPFileIO::Read(fullPath, state, read, size); }, &read, &size);
    }
    if (!isRead || state.empty()) return false;

    /* The overlay comes down before this, and deliberately. Restoring is not
       moved off this thread, unlike the world: it calls back into the GUI -
       the timer, the input line, the picture, the pane visibility - and
       finishes by running the game's own ONGLOAD, which may print, show a
       picture or ask the reader something. An overlay over that would cover
       game code that wants the screen, and could not be animated while the
       thread is inside the engine besides: it would sit there frozen, which
       reads as a hang. A save is a set of variables, not a world, so there is
       no wait here worth covering. */
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

/* Beside the game like the quick save, and a file of its own so that leaving
   never overwrites a save the player made. */
wxString QSPFrame::GetExitSavePath(const wxString& gameFilePath)
{
    if (gameFilePath.IsEmpty()) return wxEmptyString;
    wxFileName savePath(gameFilePath);
    savePath.SetName(savePath.GetName() + wxT("_exit"));
    savePath.SetExt(wxT("sav"));
    return savePath.GetFullPath();
}

/* Only a game at rest is saved. Closing in the middle of game code - a MSG on
   screen, a WAIT, a menu - would save half an action, and a save has no way to
   carry the other half. Nor is it at rest once that code has finished after a
   deferred close: it ran with every callback stood down, a WAIT cut short and
   a MSG skipped. The previous exit save is left as it is instead, as it is
   when the game has NOSAVE on. Dev sessions are throwaway and never save. */
void QSPFrame::SaveOnExit()
{
    if (!m_toSaveOnExit || m_devServer || !m_isGameOpened) return;
    /* The game to go back to is this one, even when it cannot be saved now */
    {
        wxFileConfig cfg(wxEmptyString, wxEmptyString, m_configDefPath);
        cfg.Write(wxT("Session/LastGame"), m_gameFilePath);
    }
    if (m_isCloseDeferred || m_isLoading || !m_toProcessEvents ||
        QSPDev::IsEngineBusy() || !CanSaveGame())
        return;

    wxString savePath(GetExitSavePath(m_gameFilePath));
    if (savePath.IsEmpty()) return;
    /* A save runs the game's ONGSAVE. Every callback stands down once
       m_toQuit is set, so a MSG or a sound in it is skipped rather than put in
       front of someone who is leaving. */
    m_toQuit = true;
    std::vector<char> state;
    if (QSPGameState::Save(state, false))
        QSPFileIO::Write(savePath, state);
}

/* False sends the caller on to a fresh start: no exit save, or one this world
   no longer accepts - the game was updated since, say. A save that cannot be
   loaded says nothing the player can act on, so there is no error for it. */
bool QSPFrame::ResumeFromExitSave()
{
    if (!m_toSaveOnExit || !m_toResumeOnLaunch || m_devServer) return false;
    wxString savePath(GetExitSavePath(m_gameFilePath));
    if (savePath.IsEmpty() || !wxFileExists(savePath)) return false;

    std::vector<char> state;
    if (!QSPFileIO::Read(savePath, state) || state.empty()) return false;
    if (!QSPOpenSavedGameFromData(&state[0], (int)state.size(), QSP_TRUE))
    {
        /* An error in a location is the game's ONGLOAD failing after the state
           was already in: the game is resumed, and the error is shown the way
           any load shows it. Without a location it is the save that was
           refused, and the game starts over instead. */
        QSPErrorInfo errorInfo = QSPGetLastErrorData();
        if (qspToWxString(errorInfo.LocName).IsEmpty()) return false;
        ShowError();
        return true;
    }
    ShowToast(_("Resumed where you left off"));
    return true;
}

wxString QSPFrame::GetGameToResume() const
{
    if (!m_toSaveOnExit || !m_toResumeOnLaunch) return wxEmptyString;
    wxFileConfig cfg(wxEmptyString, wxEmptyString, m_configDefPath);
    wxString gamePath(cfg.Read(wxT("Session/LastGame"), wxEmptyString));
    if (gamePath.IsEmpty() || !wxFileExists(gamePath)) return wxEmptyString;
    return gamePath;
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
    /* QSPMutableString, not QSP_STATIC_STR: the engine upper-cases the
       expression where it stands before evaluating it, and a string literal
       lives in read-only memory - writing to it faults. */
    if (!QSPCalculateStrExpression(QSPMutableString(wxT("$CURLOC")), buffer, (int)(sizeof(buffer) / sizeof(buffer[0])), QSP_FALSE))
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

/* The dialog reads the slots itself and deletes from them itself; what comes
   back is only what it cannot do, because it drives the engine. Game events
   are held off while it is up, the way they are for the error dialog: a timer
   firing under a modal dialog would run the game the player is saving. */
void QSPFrame::ShowSaveSlots()
{
    /* Busy is checked here as well as in the save and the load, because F6
       pressed in a pane reaches this during a WAIT - and a dialog whose
       buttons then quietly do nothing is worse than no dialog */
    if (!m_isGameOpened || !m_toProcessEvents) return;

    QSPSaveSlotsDlg dialog(this,
                           &m_saveSlots,
                           CanSaveGame(),
                           m_desc->GetBackgroundColour(),
                           m_desc->GetForegroundColour(),
                           m_desc->GetTextFont()
    );
    bool oldToProcessEvents = m_toProcessEvents;
    m_toProcessEvents = false;
    int result = dialog.ShowModal();
    m_toProcessEvents = oldToProcessEvents;
    if (result != wxID_OK) return;

    switch (dialog.GetAction())
    {
    case QSPSaveSlotsDlg::ACTION_SAVE:
        SaveToNumberedSlot(dialog.GetSlot());
        break;
    case QSPSaveSlotsDlg::ACTION_LOAD:
        LoadFromNumberedSlot(dialog.GetSlot());
        break;
    default:
        break;
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
    /* Whichever way the game arrived - the command line, auto.qsp or the
       last session - it is picked up where it was left, if it was. */
    OpenGameFile(event.GetInitString(), true);
}

void QSPFrame::OnClose(wxCloseEvent& WXUNUSED(event))
{
    /* Game code can be running underneath this - a WAIT or a forced refresh
       yields to the event loop - and it comes back into the frame when it
       resumes. Destroying the frame here freed it under that code: the yield
       runs the idle pass that does the deleting, and the WAIT went on to use
       the dead frame and crashed the player on the way out. So the window goes
       at once and everything stands down, but the frame itself waits in
       OnIdle until the engine has returned. */
    if (QSPDev::IsEngineBusy() || m_isLoading)
    {
        if (!m_isCloseDeferred)
        {
            m_isCloseDeferred = true;
            m_toast->Dismiss();
            /* Written now rather than on the way out, which may never come */
            SaveOnExit();
            SaveSettings();
            m_toQuit = true;
            m_timer->Stop();
            Hide();
            /* A game looping on WAIT never returns: with every callback stood
               down, its loop just spins, out of sight. It gets a few seconds
               to finish what it was doing; after that the process ends, and
               nothing is lost that is not already on disk. */
            std::thread([]()
            {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                std::_Exit(0);
            }).detach();
        }
        return;
    }
    m_toast->Dismiss();
    SaveOnExit();
    SaveSettings();
    EnableControls(false, true);
    Destroy();
    m_toQuit = true;
}

void QSPFrame::OnIdle(wxIdleEvent& event)
{
    event.Skip();
    if (m_isCloseDeferred && !IsBeingDeleted() && !QSPDev::IsEngineBusy() && !m_isLoading)
        Close(true);
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
    /* The log is not something a reader sees, so a save that did not reach
       the disk has to say so here - or they carry on believing it did */
    if (dialog.ShowModal() == wxID_OK && !SaveGameState(dialog.GetPath()))
        ShowToast(_("Couldn't write the saved game"), QSP_TOAST_ERROR);
}

void QSPFrame::OnQuickSave(wxCommandEvent& event)
{
    if (m_savedGamePath.IsEmpty())
        OnSaveGameStat(event);
    else if (!SaveGameState(m_savedGamePath))
        ShowToast(_("Couldn't write the saved game"), QSP_TOAST_ERROR);
}

void QSPFrame::OnQuickSaveSlot(wxCommandEvent& WXUNUSED(event))
{
    QuickSaveToSlot();
}

void QSPFrame::OnQuickLoadSlot(wxCommandEvent& WXUNUSED(event))
{
    QuickLoadFromSlot();
}

void QSPFrame::OnSaveSlots(wxCommandEvent& WXUNUSED(event))
{
    ShowSaveSlots();
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
        if (m_toProcessEvents)
            ApplyParams();
        else
        {
            ApplyLinkColor(m_linkColor);
            RefreshUI();
        }
    }
}

void QSPFrame::OnSelectTheme(wxCommandEvent& event)
{
    switch (event.GetId())
    {
    case ID_LIGHTTHEME: SetColorTheme(QSP_THEME_LIGHT); break;
    case ID_DARKTHEME: SetColorTheme(QSP_THEME_DARK); break;
    default: SetColorTheme(QSP_THEME_SYSTEM); break;
    }
    ApplyThemeToDockArt();
    /* The menus, the dropdowns and the common dialogs are Windows' own, and
       it only takes that decision while the player has no window open yet -
       so a switch away from what they are showing is honoured on the next
       start. Said here rather than left as a surprise. */
    if (IsDarkTheme() != wxSystemSettings::GetAppearance().IsDark())
        ShowToast(_("Menus and dialogs follow when the player is restarted"));
}

/* The desktop switched between light and dark while the player was running.
   wxWidgets sends this to every window; only the frame acts on it, because the
   captions are drawn from here - and only while the desktop is what the player
   is following. */
void QSPFrame::OnSysColourChanged(wxSysColourChangedEvent& event)
{
    event.Skip();
    if (m_colorTheme == QSP_THEME_SYSTEM) ApplyThemeToDockArt();
}

void QSPFrame::OnCheckUpdatesOnStartup(wxCommandEvent& WXUNUSED(event))
{
    m_toCheckUpdates = !m_toCheckUpdates;
}

/* Written the moment they change, into the player's own file, rather than
   with the rest in SaveSettings - which writes to the game's file whenever a
   game has one. */
void QSPFrame::OnSaveOnExit(wxCommandEvent& WXUNUSED(event))
{
    m_toSaveOnExit = !m_toSaveOnExit;
    wxFileConfig cfg(wxEmptyString, wxEmptyString, m_configDefPath);
    cfg.Write(wxT("Session/SaveOnExit"), m_toSaveOnExit);
    UpdateSessionMenu();
}

void QSPFrame::OnResumeOnLaunch(wxCommandEvent& WXUNUSED(event))
{
    m_toResumeOnLaunch = !m_toResumeOnLaunch;
    wxFileConfig cfg(wxEmptyString, wxEmptyString, m_configDefPath);
    cfg.Write(wxT("Session/ResumeOnLaunch"), m_toResumeOnLaunch);
    UpdateSessionMenu();
}

/* Resuming needs something to resume from, so it is greyed out while saving
   on exit is off. Its own choice is kept meanwhile and comes back with it. */
void QSPFrame::UpdateSessionMenu()
{
    m_settingsMenu->Check(ID_SAVEONEXIT, m_toSaveOnExit);
    m_settingsMenu->Check(ID_RESUMEONLAUNCH, m_toResumeOnLaunch);
    m_settingsMenu->Enable(ID_RESUMEONLAUNCH, m_toSaveOnExit);
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

/* Nothing moves when it is switched: the panels stay where they are, and the
   next resize is the first one measured the new way. */
void QSPFrame::OnToggleDockPixels(wxCommandEvent& WXUNUSED(event))
{
    m_dockLayout.SetKeepPixels(!m_dockLayout.IsKeepingPixels());
}

/* The marks are read off the layout each time the menu opens rather than kept
   in step by hand: a pane is also hidden by its own close button, by the game's
   SHOWOBJS and friends, and by a saved layout being loaded. */
void QSPFrame::OnUpdateShowHide(wxUpdateUIEvent& event)
{
    switch (event.GetId())
    {
    case ID_TOGGLEOBJS: event.Check(m_manager->GetPane(m_objects).IsShown()); break;
    case ID_TOGGLEACTS: event.Check(m_manager->GetPane(m_actions).IsShown()); break;
    case ID_TOGGLEDESC: event.Check(m_manager->GetPane(m_vars).IsShown()); break;
    case ID_TOGGLEINPUT: event.Check(m_manager->GetPane(m_input).IsShown()); break;
    case ID_TOGGLECAPTIONS: event.Check(m_manager->GetPane(m_objects).HasCaption()); break;
    case ID_TOGGLEHOTKEYS: event.Check(m_toShowHotkeys); break;
    case ID_TOGGLEDOCKPIXELS: event.Check(m_dockLayout.IsKeepingPixels()); break;
    }
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
    wxString description(wxString::Format(
        _("Engine version: %s\nEngine compiled: %s\nGUI compiled: %s"),
        qspToWxString(version).wx_str(),
        qspToWxString(libCompiledDate).wx_str(),
        guiCompiledDate.wx_str()
    ));
    /* Off a tag these are the same string and there is nothing to add; between
       tags, say which commit this is - down here, not in the heading */
    if (wxStrcmp(QSP_BUILD, QSP_VER) != 0)
        description << wxT("\n") << wxString::Format(_("Build: %s"), QSP_BUILD);
    info.SetDescription(description);
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

/* The web lists queue their events rather than sending them, and a queued
   event is dispatched by whatever pumps the loop next - a WAIT, a forced
   refresh, a MSG, or the world load running on its worker thread. The engine
   has no guard against being entered from inside itself, so these three need
   the same one the links, the keys and the timer already have. What the pane
   painted meanwhile is put right by the refresh that follows. */
void QSPFrame::OnObjectChange(wxCommandEvent& event)
{
    if (!m_toProcessEvents || m_toQuit) return;
    // show selection first
    m_objects->Update();
    wxThread::Sleep(10);
    // execute the handler
    if (!QSPSetSelObjectIndex(event.GetInt(), QSP_TRUE))
        ShowError();
}

void QSPFrame::OnActionChange(wxCommandEvent& event)
{
    if (!m_toProcessEvents || m_toQuit) return;
    // show selection first
    m_actions->Update();
    wxThread::Sleep(10);
    // execute the handler
    if (!QSPSetSelActionIndex(event.GetInt(), QSP_TRUE))
        ShowError();
}

void QSPFrame::OnActionDblClick(wxCommandEvent& WXUNUSED(event))
{
    if (!m_toProcessEvents || m_toQuit) return;
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
    /* The save keys are menu accelerators, and accelerators never see a key
       pressed inside a browser pane - that one comes back to us as a synthetic
       event instead, which is what we answer here. Every pane counts: which of
       them the reader last clicked in is not something a save key should turn
       on. */
    wxObject *source = event.GetEventObject();
    /* The window mode keys, for the same reason. Only a pane's key: one
       pressed anywhere else has already been through the accelerator, and
       would switch the mode straight back. */
    if ((source == m_desc || source == m_vars || source == m_objects ||
         source == m_actions || source == m_imgView) &&
        ((event.GetKeyCode() == WXK_F11 && !event.HasModifiers()) ||
         (event.GetKeyCode() == WXK_RETURN && event.GetModifiers() == wxMOD_ALT)))
    {
        ShowFullScreen(!IsFullScreen());
        return;
    }
    if (!event.HasModifiers() &&
        (source == m_desc || source == m_vars || source == m_objects || source == m_actions))
    {
        switch (event.GetKeyCode())
        {
        case WXK_F5:
            QuickSaveToSlot();
            return;
        case WXK_F9:
            QuickLoadFromSlot();
            return;
        case WXK_F6:
            ShowSaveSlots();
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

void QSPFrame::OnSize(wxSizeEvent& event)
{
    event.Skip();
    RescaleDocks();
}

/* wxAUI sizes a dock in pixels and leaves it there, so the centre pane takes
   every pixel a resize adds: the same layout that fits a small window leaves
   a thin strip of actions and objects around a vast description on a large
   one. Each dock is given back the share of the window it had instead, so
   what the player set up is what they keep at any size. A player who wants
   the panels to stay the size they made them can have that instead: the
   dock layout keeps pixels then, and only steps in when the window gets too
   small to hold them.

   The input row is left out of it - it holds one line of text, and a line of
   text does not get taller because the window did. */
void QSPFrame::RescaleDocks()
{
    if (m_isRescalingDocks || !m_manager) return;

    wxSize size(GetClientSize());
    if (size.GetWidth() < 1 || size.GetHeight() < 1) return;
    if (size == m_lastLayoutSize) return;
    if (m_lastLayoutSize.GetWidth() < 1)
    {
        /* Nothing to scale from yet: this is the size the layout starts at */
        m_lastLayoutSize = size;
        return;
    }
    /* A maximized pane covers the docks entirely and its own saved sizes are
       what a restore brings back, so they are left alone until it is. */
    const wxAuiPaneInfoArray& panes = m_manager->GetAllPanes();
    for (size_t i = 0; i < panes.GetCount(); ++i)
    {
        if (panes[i].IsMaximized())
        {
            m_lastLayoutSize = size;
            return;
        }
    }

    wxArrayString fixedPanes;
    fixedPanes.Add(wxT("input"));
    wxString perspective(m_manager->SavePerspective());
    wxString rescaled(m_dockLayout.Rescale(perspective, m_lastLayoutSize, size, fixedPanes));
    m_lastLayoutSize = size;
    if (rescaled == perspective) return;

    m_isRescalingDocks = true;
    m_manager->LoadPerspective(rescaled, true);
    m_isRescalingDocks = false;
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
