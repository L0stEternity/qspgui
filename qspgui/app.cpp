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

#include "app.h"
#include "comtools.h"
#include "devserver.h"

wxIMPLEMENT_APP(QSPApp);

bool QSPApp::OnInit()
{
    if (!wxApp::OnInit())
        return false;

    SetupLogging();

#ifdef __WXMSW__
    /* Themes the parts the player does not paint itself - the menu bar, the
       AUI captions, the scrollbars and the common dialogs - to match the
       desktop. The panes' own colours are settings, handled by the frame. */
    MSWEnableDarkMode(DarkMode_Auto);
#endif

    wxInitAllImageHandlers();
    QSPInit();
    InitUI();
    return true;
}

int QSPApp::OnExit()
{
    QSPTerminate();
    QSPCallbacks::DeInit();
    delete m_transHelper;
    wxTheClipboard->Flush();
    CloseLogging();
    return wxApp::OnExit();
}

/* A GUI build has no console attached on Windows, so the stock stderr target
   drops everything it is given - which is why a bug report never arrives with
   a log. --log-file produces one; dev mode turns it on by itself, since an
   editor driving the player is exactly when the detail is wanted. */
void QSPApp::SetupLogging()
{
    wxLog::EnableLogging(true);
    wxLog::SetLogLevel(m_logLevel);
    wxLog::SetTimestamp(wxT("%Y-%m-%d %H:%M:%S"));

    if (m_logPath.IsEmpty())
    {
        delete wxLog::SetActiveTarget(new wxLogStderr());
        return;
    }

    /* Appending keeps the previous run, which is usually the interesting one,
       but a player left running for days would otherwise grow the file without
       bound. Past the cap the old contents are dropped rather than rotated:
       one stale log is worth keeping, a directory of them is not. */
    const wxFileOffset maxLogBytes = 4 * 1024 * 1024;
    if (wxFileExists(m_logPath) && wxFileName::GetSize(m_logPath) > (wxULongLong)maxLogBytes)
        wxRemoveFile(m_logPath);

    m_logFile = wxFopen(m_logPath, wxT("a"));
    if (!m_logFile)
    {
        delete wxLog::SetActiveTarget(new wxLogStderr());
        wxLogError(wxT("Cannot open the log file: %s"), m_logPath);
        return;
    }
    delete wxLog::SetActiveTarget(new wxLogStderr(m_logFile));
    wxLogMessage(wxT("%s %s starting on %s"), QSP_APPNAME, QSP_VER, QSPTools::GetPlatform());
}

void QSPApp::CloseLogging()
{
    /* The active target writes into m_logFile, so it has to go first */
    delete wxLog::SetActiveTarget(new wxLogStderr());
    if (m_logFile)
    {
        fclose(m_logFile);
        m_logFile = NULL;
    }
}

void QSPApp::OnInitCmdLine(wxCmdLineParser &parser)
{
    wxApp::OnInitCmdLine(parser);

    parser.AddParam("game file",
                    wxCMD_LINE_VAL_STRING,
                    wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddLongSwitch("dev",
                         "expose the development API on a loopback port");
    parser.AddLongOption("dev-port",
                         "port for the development API (default 4747)",
                         wxCMD_LINE_VAL_NUMBER,
                         wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddLongOption("dev-token",
                         "require this token before accepting development commands",
                         wxCMD_LINE_VAL_STRING,
                         wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddLongOption("log-file",
                         "append diagnostics to this file (default: none, or qspgui.log in dev mode)",
                         wxCMD_LINE_VAL_STRING,
                         wxCMD_LINE_PARAM_OPTIONAL);
    parser.AddLongOption("log-level",
                         "error | warning | message | info | debug (default: message)",
                         wxCMD_LINE_VAL_STRING,
                         wxCMD_LINE_PARAM_OPTIONAL);
}

bool QSPApp::OnCmdLineParsed(wxCmdLineParser &parser)
{
    if (!wxApp::OnCmdLineParsed(parser))
        return false;

    if (parser.GetParamCount() > 0)
        m_gameFile = parser.GetParam();

    m_isDevMode = parser.Found("dev");
    long devPort;
    if (parser.Found("dev-port", &devPort))
    {
        /* Out of range is a typo, not a request to wrap around to some other
           port - 0 stays legal, it asks the OS to pick one. */
        if (devPort < 0 || devPort > 65535)
        {
            wxLogError(wxT("--dev-port must be between 0 and 65535"));
            return false;
        }
        m_devPort = (unsigned short)devPort;
        m_isDevMode = true;
    }
    /* Asking for a token is asking for the API, the same way naming a port is */
    if (parser.Found("dev-token", &m_devToken))
        m_isDevMode = true;

    wxString levelName;
    if (parser.Found("log-level", &levelName))
    {
        levelName.MakeLower();
        if (levelName == wxT("error")) m_logLevel = wxLOG_Error;
        else if (levelName == wxT("warning")) m_logLevel = wxLOG_Warning;
        else if (levelName == wxT("message")) m_logLevel = wxLOG_Message;
        else if (levelName == wxT("info")) m_logLevel = wxLOG_Info;
        else if (levelName == wxT("debug")) m_logLevel = wxLOG_Debug;
        else
        {
            /* Silently picking a level for a typo is how a report arrives
               with the wrong detail in it */
            wxLogError(wxT("--log-level must be error, warning, message, info or debug"));
            return false;
        }
    }

    if (!parser.Found("log-file", &m_logPath) && m_isDevMode)
    {
        /* Dev mode without an explicit path still gets a log, next to the
           executable where an editor knows to look for it. */
        m_logPath = QSPTools::GetAppPath(wxEmptyString, wxT("qspgui.log"));
    }
    return true;
}

void QSPApp::InitUI()
{
    wxString configPath = QSPTools::GetAppPath(wxEmptyString, QSP_CONFIG);
    if (!wxFileExists(configPath) && !wxFileName::IsDirWritable(QSPTools::GetAppPath()))
        configPath = QSPTools::GetConfigPath(wxEmptyString, QSP_CONFIG);

    wxString langsPath = QSPTools::GetResourcePath(QSP_TRANSLATIONS);
    m_transHelper = new QSPTranslationHelper(QSP_APPNAME, langsPath);

    /* Must precede the frame: it builds its panes in its constructor, and the
       devtools, the context menu and the shell's contents are all decided
       there. */
    QSPMainTextBox::EnableDevMode(m_isDevMode);

    // ----------------------
    QSPFrame * frame = new QSPFrame(configPath, m_transHelper);
    QSPCallbacks::Init(frame);
    if (m_isDevMode)
    {
        /* Printed so that an editor launching the player can pick the port up
           from its output instead of guessing. */
        if (frame->StartDevServer(m_devPort, m_devToken))
            wxPrintf(wxT("qspgui: development API listening on 127.0.0.1:%u\n"), frame->GetDevServer()->GetPort());
        else
            wxLogError(wxT("Cannot start the development API on port %u"), m_devPort);
        fflush(stdout);
    }
    frame->LoadSettings(); // load settings after initialization to properly restore everything
    frame->EnableControls(false);
    // ----------------------
    wxInitEvent initEvent;
    if (GetAutoRunEvent(initEvent))
        wxPostEvent(frame, initEvent);
    else
    {
        if (frame->ToCheckUpdates())
            frame->CheckLatestVersion(UPDATE_SHOW_ONLY_NEW);
    }
}

bool QSPApp::GetAutoRunEvent(wxInitEvent& initEvent)
{
    if (!m_gameFile.IsEmpty())
    {
        wxFileName path(m_gameFile);
        path.MakeAbsolute();
        initEvent.SetInitString(path.GetFullPath());
        return true;
    }
    else
    {
        wxFileName autoPath(wxT("auto.qsp"));
        autoPath.MakeAbsolute();
        wxString autoPathString(autoPath.GetFullPath());
        if (wxFileExists(autoPathString))
        {
            initEvent.SetInitString(autoPathString);
            return true;
        }
    }
    return false;
}
