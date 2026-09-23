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
    #include <atomic>
    #include <functional>
    #include <qsp_default.h>
    #include "comtools.h"
    #include "transhelper.h"
    #include "inputbox.h"
    #include "textbox.h"
    #include "listbox.h"
    #include "imgcanvas.h"
    #include "initevent.h"
    #include "pathprovider.h"
    #include "updateappdialog.h"
    #include "toast.h"
    #include "loadingoverlay.h"
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
    #define QSP_BUILD wxT(QSPGUI_BUILD_STR)
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
        /* Every numbered slot, and saving, loading and deleting, live in one
           dialog rather than in two submenus of nine labels each */
        ID_SAVESLOTS,
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
        /* One radio group: the desktop's choice, or either theme by hand */
        ID_USESYSTEMCOLORS,
        ID_LIGHTTHEME,
        ID_DARKTHEME,
        ID_CHECKUPDATESONSTARTUP,
        ID_SAVEONEXIT,
        ID_RESUMEONLAUNCH,
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

    /* How the player's own frame is painted - the pane captions, the sashes,
       the menus and the dialogs. The page, the text and the links are not part
       of it: those belong to the game, and to the three colour settings next
       to this one. Stored in the config as a number, so the order is part of
       the file format. */
    enum QSPColorTheme
    {
        QSP_THEME_SYSTEM = 1,
        QSP_THEME_LIGHT = 2,
        QSP_THEME_DARK = 3
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
        /* Tells every pane the game folder moved; see the definition */
        void NotifyPanesOfGamePath();
        wxString ComposeGamePath(const wxString &relativePath) const;
        bool IsValidFullPath(const wxString &path) const;
        wxString GetGamePath() const { return m_worldPath; }
        wxString GetGameFilePath() const { return m_gameFilePath; }
        /* custom.css / custom.js next to the world file, which qQSP loads on its own */
        bool HasCustomCss() const { return m_hasCustomCss; }
        bool HasCustomJs() const { return m_hasCustomJs; }
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

        /* Opening a game or a saved game, and the overlay that says so.
           BeginLoading/EndLoading bracket the whole operation - use
           QSPLoadingScope rather than pairing them by hand - and
           RunLoadingStep runs one blocking piece of it off the UI thread so
           the window keeps painting and answering Windows while it goes.

           Only work that calls no engine callback may be handed to
           RunLoadingStep: reading a file, and QSPLoadGameWorldFromData, which
           parses and allocates and nothing else. Restoring a saved game does
           call back into the GUI, so it stays on this thread. */
        void BeginLoading(const wxString &stage, const wxString &detail);
        void SetLoadingStage(const wxString &stage);
        void EndLoading();
        /* done and total are the step's own published progress, read while it
           runs; pass none for a step whose length cannot be known. */
        void RunLoadingStep(const std::function<void()> &work,
                            std::atomic<wxFileOffset> *done = NULL,
                            std::atomic<wxFileOffset> *total = NULL);
        /* True from BeginLoading to EndLoading. The engine's state is being
           rewritten and nothing outside may touch it - the dev server asks
           before it acts on anything a connected editor sent. */
        bool IsBusyLoading() const { return m_isLoading; }
        bool ToShowHotkeys() const { return m_toShowHotkeys; }
        bool ToCheckUpdates() const { return m_toCheckUpdates; }
        /* The game the last session ended in, when the player is set to go
           back to it - empty otherwise, or when that file is gone. */
        wxString GetGameToResume() const;
        bool ToQuit() const { return m_toQuit; }
        bool IsKeyPressedWhileDisabled() const { return m_keyPressedWhileDisabled; }
        void CheckLatestVersion(int type);
        void ProcessVersionResult(const wxString &versionInfo, int type);

    protected:
        // Internal methods
        void ShowError();
        /* What the copy button on the error dialog puts on the clipboard */
        wxString BuildErrorReport(const QSPErrorInfo &errorInfo) const;
        void AppendErrorCode(wxString &report, const QSPErrorInfo &errorInfo,
                             const wxString &rule) const;
        void UpdateTitle();
        void ReCreateGUI();
        void RefreshUI();
        void ApplyFont(const wxFont& font);
        bool ApplyFontSize(int size);
        bool ApplyFontName(const wxString& name);
        bool ApplyFontColor(const wxColour& color);
        bool ApplyBackColor(const wxColour& color);
        /* Whether the theme in force is a dark one */
        bool IsDarkTheme() const;
        /* The pane captions, sashes and borders, which wxAUI draws for us and
           which are not tied to the desktop's own light or dark setting */
        void ApplyThemeToDockArt();
        /* The check marks beside the checkable menu items; see the definition */
        void ApplyThemeToMenus();
        /* Keeps the setting and the menu's radio group in step */
        void SetColorTheme(int theme);
        bool ApplyLinkColor(const wxColour& color);
        void CallPaneFunc(wxWindowID id, QSP_BOOL toShow) const;
        void TogglePane(wxWindowID id);
        void SetOverallVolume(int percents);
        /* toResume picks the game up from its exit save, when it has one,
           instead of starting it */
        void OpenGameFile(const wxString& fullPath, bool toResume = false);
        bool OpenGameState(const wxString& fullPath);
        /* toRemember keeps the file as the target of the Ctrl-S quicksave;
           the F5 slot is saved without it, so it can't silently take over
           the file the player picked themselves. */
        bool SaveGameState(const wxString& fullPath, bool toRemember = true);
        wxString GetQuickSavePath() const;
        static wxString GetExitSavePath(const wxString& gameFilePath);
        void SaveOnExit();
        bool ResumeFromExitSave();
        void UpdateSessionMenu();
        void QuickSaveToSlot();
        void QuickLoadFromSlot();
        /* Numbered slots, 1-based. Both report through a toast rather than a
           dialog: saving is not an event worth interrupting play for. */
        void SaveToNumberedSlot(int slot);
        void LoadFromNumberedSlot(int slot);
        /* The slots dialog, and whichever of the two the player asked for
           there. Deleting is done inside the dialog: it is only files. */
        void ShowSaveSlots();
        /* $CURLOC, for the slot listing. Empty when it can't be read. */
        wxString GetCurrentLocationName() const;

        // Events
        void OnVersionRequestState(wxWebRequestEvent& event);
        void OnInit(wxInitEvent& event);
        void OnClose(wxCloseEvent& event);
        void OnIdle(wxIdleEvent& event);
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
        void OnSaveSlots(wxCommandEvent& event);
        void OnSelectFont(wxCommandEvent& event);
        void OnUseFontSize(wxCommandEvent& event);
        void OnSelectTheme(wxCommandEvent& event);
        void OnSysColourChanged(wxSysColourChangedEvent& event);
        void OnSelectFontColor(wxCommandEvent& event);
        void OnSelectBackColor(wxCommandEvent& event);
        void OnSelectLinkColor(wxCommandEvent& event);
        void OnCheckUpdatesOnStartup(wxCommandEvent& event);
        void OnSaveOnExit(wxCommandEvent& event);
        void OnResumeOnLaunch(wxCommandEvent& event);
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
        void OnSize(wxSizeEvent& event);
        void OnDropFiles(wxDropFilesEvent& event);
        /* Give every dock the share of the window it had before the resize */
        void RescaleDocks();

        // Fields
        bool m_isGameOpened;
        wxString m_worldPath;
        bool m_hasCustomCss;
        bool m_hasCustomJs;
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
        QSPLoadingOverlay *m_loadingOverlay;
        bool m_isLoading;
        /* The window was closed while game code was running under it; the
           frame is destroyed once that code has returned. See OnClose. */
        bool m_isCloseDeferred;
        QSPSaveSlots m_saveSlots;
        bool m_isManagerUpdatePending;
        QSPDockLayout m_dockLayout;
        /* The client size the dock sizes belong to, and a guard against the
           resize our own relayout could set off */
        wxSize m_lastLayoutSize;
        bool m_isRescalingDocks;
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
        /* Kept in the player's own settings file even while a game has its
           own: they are needed at startup, before any game is open, and the
           resume option means nothing without the exit save. */
        bool m_toSaveOnExit;
        bool m_toResumeOnLaunch;
        /* How the frame is painted - QSPColorTheme. Following the desktop is
           worked out on the spot, so a desktop that switches to dark while the
           player is running is followed as it happens. */
        int m_colorTheme;
        int m_volume;
        int m_menuIndex;
    };

    /* Keeps the overlay up for exactly as long as the load lasts. Opening a
       game leaves by half a dozen different returns - an unreadable file, a
       world the engine rejects, the player being closed part way through -
       and every one of them has to take the overlay with it. */
    class QSPLoadingScope
    {
    public:
        QSPLoadingScope(QSPFrame *frame, const wxString &stage, const wxString &detail)
            : m_frame(frame)
        {
            m_frame->BeginLoading(stage, detail);
        }
        ~QSPLoadingScope() { m_frame->EndLoading(); }

        void SetStage(const wxString &stage) { m_frame->SetLoadingStage(stage); }

    private:
        QSPLoadingScope(const QSPLoadingScope&);
        QSPLoadingScope& operator=(const QSPLoadingScope&);

        QSPFrame *m_frame;
    };

#endif
