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

#ifndef MAIN_H
    #define MAIN_H

    #include <wx/wx.h>
    #include <wx/fileconf.h>
    #include <wx/cmdline.h>
    #include <wx/regex.h>
    #include <wx/webrequest.h>
    #include <wx/fontenum.h>
    #include <wx/fontdlg.h>
    #include <wx/colordlg.h>
    #include <wx/aboutdlg.h>
    #include <wx/aui/aui.h>
    #include <qsp_default.h>
    #include "transhelper.h"
    #include "inputbox.h"
    #include "textbox.h"
    #include "listbox.h"
    #include "imgcanvas.h"
    #include "initevent.h"
    #include "pathprovider.h"
    #include "updateappdialog.h"
    #include "toast.h"
    #include "saveslots.h"

    class QSPDevServer;

    #include "qspgui_config.h"

    /* The main and additional description panes can be rendered either by the
       classic wxHtmlWindow or by a real browser engine. Everything else in the
       player is identical, so the choice is a single type. */
    #ifdef QSPGUI_USE_WEBVIEW
        #include "webtextbox.h"
        #include "weblistbox.h"
        #include "webimgcanvas.h"
        typedef QSPWebTextBox QSPMainTextBox;
        typedef QSPWebListBox QSPMainListBox;
        typedef QSPWebImgCanvas QSPMainImgCanvas;
    #else
        typedef QSPTextBox QSPMainTextBox;
        typedef QSPListBox QSPMainListBox;
        typedef QSPImgCanvas QSPMainImgCanvas;
    #endif

    #define QSP_VER wxT(QSPGUI_VER_STR)
    #define QSP_LOGO wxT("Quest Soft Player ") QSP_VER

    enum
    {
        ID_BEGOFDYNMENU = 1000,
        ID_ENDOFDYNMENU = 1500,
        ID_OPENGAME,
        ID_NEWGAME,
        ID_OPENGAMESTAT,
        ID_SAVEGAMESTAT,
        ID_QUICKSAVE,
        ID_QUICKSAVESLOT,
        ID_QUICKLOADSLOT,
        /* Two contiguous runs of QSPSaveSlots::Count, so a menu id maps back
           to a slot number by subtraction */
        ID_SAVETOSLOT,
        ID_LOADFROMSLOT,
        ID_SAVESLOT1,
        ID_SAVESLOT9 = ID_SAVESLOT1 + QSPSaveSlots::Count - 1,
        ID_LOADSLOT1,
        ID_LOADSLOT9 = ID_LOADSLOT1 + QSPSaveSlots::Count - 1,
        ID_VOLUME,
        ID_VOLUME0,
        ID_VOLUME20,
        ID_VOLUME40,
        ID_VOLUME60,
        ID_VOLUME80,
        ID_VOLUME100,
        ID_FONT,
        ID_SELECTFONT,
        ID_USEFONTSIZE,
        ID_COLORS,
        ID_SELECTFONTCOLOR,
        ID_SELECTBACKCOLOR,
        ID_SELECTLINKCOLOR,
        ID_USESYSTEMCOLORS,
        ID_CHECKUPDATESONSTARTUP,
        ID_CHECKUPDATES,
        ID_SELECTLANG,
        ID_TOGGLEWINMODE,
        ID_TOGGLEOBJS,
        ID_TOGGLEACTS,
        ID_TOGGLEDESC,
        ID_TOGGLEINPUT,
        ID_TOGGLECAPTIONS,
        ID_TOGGLEHOTKEYS,
        ID_SHOWHIDE,
        ID_MAINDESC,
        ID_VARSDESC,
        ID_OBJECTS,
        ID_ACTIONS,
        ID_VIEWPIC,
        ID_INPUT,
        ID_TIMER,

        ID_DUMMY
    };

    enum AppUpdateType
    {
        UPDATE_SHOW_ONLY_NEW,
        UPDATE_SHOW_ALL_RESULTS
    };

    class QSPFrame : public wxFrame, public PathProvider
    {
        DECLARE_CLASS(QSPFrame)
        DECLARE_EVENT_TABLE()
    public:
        // C-tors / D-tor
        QSPFrame(const wxString &configPath, QSPTranslationHelper *transHelper);
        virtual ~QSPFrame();

        // Methods
        void SaveSettings();
        void LoadSettings();
        void EnableControls(bool status, bool isExtended = false);
        void ShowPane(wxWindowID id, bool toShow);
        /* Relayouts are coalesced: a single action can toggle several panes,
           and every wxAuiManager::Update() rebuilds the whole dock layout. */
        void RequestManagerUpdate();
        void DoManagerUpdate();
        void ApplyParams();
        void DeleteMenu();
        void AddMenuItem(const wxString &name, const wxString &imgPath);
        int ShowMenu();
        void UpdateGamePath(const wxString &fullPath);
        void UpdateGameFile(const wxString &fullPath);
        wxString ComposeGamePath(const wxString &relativePath) const;
        bool IsValidFullPath(const wxString &path) const;
        wxString GetGamePath() const { return m_worldPath; }
        wxString GetGameFilePath() const { return m_gameFilePath; }
        bool IsGameOpened() const { return m_isGameOpened; }

        /* Development API, off unless the player was started with --dev */
        bool StartDevServer(unsigned short port, const wxString &token);
        QSPDevServer *GetDevServer() const { return m_devServer; }

        // Accessors
        wxTimer *GetTimer() const { return m_timer; }
        QSPMainTextBox *GetDesc() const { return m_desc; }
        QSPMainTextBox *GetVars() const { return m_vars; }
        QSPInputBox *GetInput() const { return m_input; }
        QSPMainListBox *GetActions() const { return m_actions; }
        QSPMainListBox *GetObjects() const { return m_objects; }
        QSPMainImgCanvas *GetImgView() const { return m_imgView; }
        wxMenu *GetGameMenu() const { return m_gameMenu; }
        /* Saving is up to the game: NOSAVE switches it off, which the game
           menu already reflects, so the hotkeys ask the menu rather than
           reading the variable a second time. */
        bool CanSaveGame() const { return m_gameMenu->IsEnabled(ID_SAVEGAMESTAT); }
        void ShowToast(const wxString &text, QSPToastKind kind = QSP_TOAST_INFO);
        bool ToShowHotkeys() const { return m_toShowHotkeys; }
        bool ToCheckUpdates() const { return m_toCheckUpdates; }
        bool ToQuit() const { return m_toQuit; }
        bool IsKeyPressedWhileDisabled() const { return m_keyPressedWhileDisabled; }
        void CheckLatestVersion(int type);
        void ProcessVersionResult(const wxString &versionInfo, int type);

    protected:
        // Internal methods
        void ShowError();
        void UpdateTitle();
        void ReCreateGUI();
        void RefreshUI();
        void ApplyFont(const wxFont& font);
        bool ApplyFontSize(int size);
        bool ApplyFontName(const wxString& name);
        bool ApplyFontColor(const wxColour& color);
        bool ApplyBackColor(const wxColour& color);
        /* The palette the current desktop appearance calls for */
        static void GetAppearanceColors(wxColour &back, wxColour &font, wxColour &link);
        /* Push that palette into the panes; no-op unless it is being followed */
        void ApplySystemColors();
        /* Keeps the flag and the menu check mark in step */
        void SetUseSystemColors(bool toUse);
        bool ApplyLinkColor(const wxColour& color);
        void CallPaneFunc(wxWindowID id, QSP_BOOL toShow) const;
        void TogglePane(wxWindowID id);
        void SetOverallVolume(int percents);
        void OpenGameFile(const wxString& fullPath);
        bool OpenGameState(const wxString& fullPath);
        /* toRemember keeps the file as the target of the Ctrl-S quicksave;
           the F5 slot is saved without it, so it can't silently take over
           the file the player picked themselves. */
        bool SaveGameState(const wxString& fullPath, bool toRemember = true);
        wxString GetQuickSavePath() const;
        void QuickSaveToSlot();
        void QuickLoadFromSlot();
        /* Numbered slots, 1-based. Both report through a toast rather than a
           dialog: saving is not an event worth interrupting play for. */
        void SaveToNumberedSlot(int slot);
        void LoadFromNumberedSlot(int slot);
        void RefreshSlotLabels();
        /* $CURLOC, for the slot listing. Empty when it can't be read. */
        wxString GetCurrentLocationName() const;

        // Events
        void OnVersionRequestState(wxWebRequestEvent& event);
        void OnInit(wxInitEvent& event);
        void OnClose(wxCloseEvent& event);
        void OnTimer(wxTimerEvent& event);
        void OnMenu(wxCommandEvent& event);
        void OnQuit(wxCommandEvent& event);
        void OnOpenGame(wxCommandEvent& event);
        void OnNewGame(wxCommandEvent& event);
        void OnOpenGameStat(wxCommandEvent& event);
        void OnSaveGameStat(wxCommandEvent& event);
        void OnQuickSave(wxCommandEvent& event);
        void OnQuickSaveSlot(wxCommandEvent& event);
        void OnQuickLoadSlot(wxCommandEvent& event);
        void OnSaveToSlot(wxCommandEvent& event);
        void OnLoadFromSlot(wxCommandEvent& event);
        void OnMenuOpen(wxMenuEvent& event);
        void OnSelectFont(wxCommandEvent& event);
        void OnUseFontSize(wxCommandEvent& event);
        void OnUseSystemColors(wxCommandEvent& event);
        void OnSysColourChanged(wxSysColourChangedEvent& event);
        void OnSelectFontColor(wxCommandEvent& event);
        void OnSelectBackColor(wxCommandEvent& event);
        void OnSelectLinkColor(wxCommandEvent& event);
        void OnCheckUpdatesOnStartup(wxCommandEvent& event);
        void OnSelectLang(wxCommandEvent& event);
        void OnToggleWinMode(wxCommandEvent& event);
        void OnToggleObjs(wxCommandEvent& event);
        void OnToggleActs(wxCommandEvent& event);
        void OnToggleDesc(wxCommandEvent& event);
        void OnToggleInput(wxCommandEvent& event);
        void OnToggleCaptions(wxCommandEvent& event);
        void OnToggleHotkeys(wxCommandEvent& event);
        void OnVolume(wxCommandEvent& event);
        void OnCheckUpdates(wxCommandEvent& event);
        void OnAbout(wxCommandEvent& event);
        void OnLinkClicked(wxHtmlLinkEvent& event);
    #ifdef QSPGUI_USE_WEBVIEW
        /* A call the game's JS made through window.qsp. It lives here rather
           than in the pane because this is where the engine is driven from. */
        void OnScriptCall(QSPScriptCallEvent& event);
        /* The game's JS threw, warned, or asked for an asset that isn't
           there. Logged, and pushed to a connected editor. */
        void OnScriptDiag(QSPScriptDiagEvent& event);
    #endif
        void OnObjectChange(wxCommandEvent& event);
        void OnActionChange(wxCommandEvent& event);
        void OnActionDblClick(wxCommandEvent& event);
        void OnInputTextChange(wxCommandEvent& event);
        void OnInputTextEnter(wxCommandEvent& event);
        void OnKey(wxKeyEvent& event);
        void OnMouseClick(wxMouseEvent& event);
        void OnWheel(wxMouseEvent& event);
        void OnPaneClose(wxAuiManagerEvent& event);
        void OnDropFiles(wxDropFilesEvent& event);

        // Fields
        bool m_isGameOpened;
        wxString m_worldPath;
        wxString m_gameFilePath;
        wxString m_savedGamePath;
        QSPDevServer *m_devServer;
        wxString m_configPath;
        wxString m_configDefPath;
        QSPTranslationHelper *m_transHelper;
        wxTimer *m_timer;
        QSPMainTextBox *m_desc;
        QSPMainTextBox *m_vars;
        QSPInputBox *m_input;
        QSPMainListBox *m_objects;
        QSPMainListBox *m_actions;
        QSPMainImgCanvas *m_imgView;
        wxMenu *m_gameMenu;
        int m_menuItemId;
        wxMenu *m_menu;
        wxMenu *m_fileMenu;
        wxMenu *m_settingsMenu;
        wxAuiManager *m_manager;
        QSPToast *m_toast;
        QSPSaveSlots m_saveSlots;
        wxMenu *m_saveSlotsMenu;
        wxMenu *m_loadSlotsMenu;
        bool m_isManagerUpdatePending;
        wxColour m_backColor;
        wxColour m_linkColor;
        wxColour m_fontColor;
        int m_fontSize;
        wxString m_fontName;
        bool m_toUseFontSize;
        bool m_toProcessEvents;
        bool m_toQuit;
        bool m_keyPressedWhileDisabled;
        bool m_toShowHotkeys;
        bool m_toCheckUpdates;
        /* Track the desktop's light/dark setting instead of the colours saved
           in the config. Cleared the moment a colour is picked by hand: an
           explicit choice is not something to quietly overwrite. */
        bool m_toUseSystemColors;
        int m_volume;
        int m_menuIndex;
    };

#endif
