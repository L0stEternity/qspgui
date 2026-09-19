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

#ifndef APP_H
    #define APP_H

    #include <wx/wx.h>
    #include <wx/stdpaths.h>
    #include <wx/clipbrd.h>
    #include "frame.h"
    #include "callbacks_gui.h"
    #include "transhelper.h"

    /* Default loopback port of the development API (see devserver.h) */
    #define QSP_DEV_DEFAULTPORT 4747

    class QSPApp : public wxApp
    {
    public:
        // Overloaded methods
        virtual bool OnInit();
        virtual int OnExit();
        virtual void OnInitCmdLine(wxCmdLineParser &parser);
        virtual bool OnCmdLineParsed(wxCmdLineParser &parser);
    protected:
        void InitUI();
        /* Where the settings file lives: next to the player when that folder
           can be written to, in the user's config directory otherwise. */
        wxString GetSettingsPath() const;
        /* Light or dark for the parts Windows draws - the menus, the
           dropdowns, the scrollbars, the window frame and the common dialogs.
           Read from the settings file and applied before the first window
           exists, because that is the only time wxWidgets can choose. */
        void ApplyStoredAppearance();
        void SetupLogging();
        void CloseLogging();
        bool GetAutoRunEvent(wxInitEvent& initEvent);
        // Fields
        QSPTranslationHelper *m_transHelper;
        wxString m_gameFile;
        bool m_isDevMode = false;
        unsigned short m_devPort = QSP_DEV_DEFAULTPORT;
        wxString m_devToken;
        /* Kept open for the lifetime of the app: the active wxLog target
           writes straight into it, so it can only be closed after that
           target has been replaced. */
        FILE *m_logFile = NULL;
        wxString m_logPath;
        wxLogLevel m_logLevel = wxLOG_Message;
    };

#endif
