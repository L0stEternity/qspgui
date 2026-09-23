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

#include <wx/dir.h>

#if defined(__WINDOWS__) && wxUSE_ON_FATAL_EXCEPTION && wxUSE_CRASHREPORT
    #define QSP_HAS_CRASH_REPORTS
    #include <wx/msw/crashrpt.h>
    #include <wx/msw/seh.h>
    #include <exception>
    #include <cstdlib>
#endif

/* Newly written reports carry this until a start has told the player */
#define QSP_CRASH_NEW_SUFFIX wxT(".new.dmp")
#define QSP_CRASH_KEEP 10

wxIMPLEMENT_APP(QSPApp);

#ifdef QSP_HAS_CRASH_REPORTS
namespace
{
    /* wxWidgets catches a crash on the main thread and in its own threads
       only. The world loader runs on a std::thread and the audio on one of
       miniaudio's, and a crash in either lands here. The handler writes the
       report and says to end the process. */
    LONG WINAPI QSPUnhandledExceptionFilter(EXCEPTION_POINTERS *info)
    {
        return (LONG)wxGlobalSEHandler(info);
    }

    /* A C++ exception nothing caught never reaches the filter: the runtime
       ends the process by a route that skips it */
    void QSPTerminateHandler()
    {
        wxCrashReport::GenerateNow(wxCRASH_REPORT_DEFAULT);
        std::abort();
    }
}
#endif

bool QSPApp::OnInit()
{
    if (!wxApp::OnInit())
        return false;

    SetupCrashReports();
    SetupLogging();

    /* Before InitUI, and before anything else creates a window: the choice
       cannot be made once one exists. */
    ApplyStoredAppearance();

    /* Before the engine is started: a false return from here skips OnExit,
       so nothing that needs taking down may exist yet. */
    if (!CheckBrowserEngine())
    {
        CloseLogging();
        return false;
    }

    wxInitAllImageHandlers();
    QSPInit();
    InitUI();
    return true;
}

/* Every pane is a browser view, and on Windows the browser is the WebView2
   runtime - built into Windows 11, but missing from older and stripped-down
   systems, which is where many readers are. Without it wxWebView::New() has
   nothing to build and the panes would take the player down as it started.
   Said once, here, in the player's own language, with the way out. */
bool QSPApp::CheckBrowserEngine()
{
#ifdef QSPGUI_USE_WEBVIEW
    if (wxWebView::IsBackendAvailable(wxWebViewBackendDefault)) return true;

    wxLogError(wxT("No browser engine is available; the WebView2 runtime is probably not installed"));
    /* The frame is what normally loads the language, and there is no frame */
    QSPTranslationHelper transHelper(QSP_APPNAME, QSPTools::GetResourcePath(QSP_TRANSLATIONS));
    {
        wxFileConfig cfg(wxEmptyString, wxEmptyString, GetSettingsPath());
        transHelper.Load(cfg, wxT("General/Language"));
    }
    int answer = wxMessageBox(
        _("This player shows games with Microsoft Edge WebView2 Runtime, and it is not installed on this computer.\n\nInstall it from Microsoft, then start the player again. Open the download page now?"),
        _("WebView2 Runtime is missing"),
        wxYES_NO | wxICON_ERROR);
    if (answer == wxYES)
        wxLaunchDefaultBrowser(wxT("https://go.microsoft.com/fwlink/p/?LinkId=2124703"));
    return false;
#else
    return true;
#endif
}

wxString QSPApp::GetCrashDir()
{
    return wxFileName(QSPTools::GetConfigPath(wxT("qspgui_crashes"), wxT("report.dmp"))).GetPath();
}

void QSPApp::SetupCrashReports()
{
#ifdef QSP_HAS_CRASH_REPORTS
    /* Under a debugger the crash is better left to it */
    if (wxIsDebuggerRunning()) return;

    /* Made now: by the time a report is written the process is already
       failing, which is no time to be creating directories */
    wxString dir(GetCrashDir());
    if (!wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) && !wxDirExists(dir)) return;

    wxHandleFatalExceptions(true);
    /* Named by the start time and the process, like wxWidgets' own default,
       so one run never overwrites another's */
    wxFileName report(dir, wxString::Format(wxT("qspgui_%s_%lu"),
                                            wxDateTime::Now().Format(wxT("%Y%m%dT%H%M%S")),
                                            (unsigned long)wxGetProcessId()) + QSP_CRASH_NEW_SUFFIX);
    wxCrashReport::SetFileName(report.GetFullPath());
    ::SetUnhandledExceptionFilter(QSPUnhandledExceptionFilter);
    std::set_terminate(QSPTerminateHandler);
#endif
}

void QSPApp::OnFatalException()
{
#ifdef QSP_HAS_CRASH_REPORTS
    wxCrashReport::Generate(wxCRASH_REPORT_DEFAULT);
#endif
}

/* This run's own report has not been written, so any new one is from before */
void QSPApp::ReportLastCrash(wxWindow *parent)
{
#ifdef QSP_HAS_CRASH_REPORTS
    wxString dir(GetCrashDir());
    if (!wxDirExists(dir)) return;

    wxArrayString reports;
    wxDir::GetAllFiles(dir, &reports, wxString(wxT("*")) + QSP_CRASH_NEW_SUFFIX, wxDIR_FILES);
    bool hasNew = false;
    size_t suffixLength = wxStrlen(QSP_CRASH_NEW_SUFFIX);
    for (size_t i = 0; i < reports.GetCount(); ++i)
    {
        wxString seenName(reports[i].Left(reports[i].Length() - suffixLength) + wxT(".dmp"));
        if (wxRenameFile(reports[i], seenName, true)) hasNew = true;
    }

    /* The newest few are enough to go on; the names sort by date */
    wxArrayString all;
    wxDir::GetAllFiles(dir, &all, wxT("*.dmp"), wxDIR_FILES);
    all.Sort();
    for (size_t i = 0; i + QSP_CRASH_KEEP < all.GetCount(); ++i)
        wxRemoveFile(all[i]);

    if (!hasNew) return;
    wxLogError(wxT("The previous session crashed; its report is in %s"), dir);
    /* An editor's session is no place for a dialog */
    if (m_isDevMode) return;
    wxMessageBox(wxString::Format(_("The player closed unexpectedly last time. A report about it was saved in:\n%s\n\nIf this keeps happening, please send the files from that folder to the player's developers."), dir),
                 _("The player crashed"), wxOK | wxICON_INFORMATION, parent);
#else
    wxUnusedVar(parent);
#endif
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

wxString QSPApp::GetSettingsPath() const
{
    wxString configPath = QSPTools::GetAppPath(wxEmptyString, QSP_CONFIG);
    if (!wxFileExists(configPath) && !wxFileName::IsDirWritable(QSPTools::GetAppPath()))
        configPath = QSPTools::GetConfigPath(wxEmptyString, QSP_CONFIG);
    return configPath;
}

/* The menus, the dropdowns, the scrollbars and the window frame belong to
   Windows, and wxWidgets can only pick light or dark for them while there is
   no window yet - so the theme is read here, out of the settings file
   directly, rather than waiting for the frame to load it. That is also why
   changing the theme while the player runs leaves those parts as they are
   until the next start, which the frame says as it happens. */
void QSPApp::ApplyStoredAppearance()
{
    wxFileConfig cfg(wxEmptyString, wxEmptyString, GetSettingsPath());
    /* The setting used to be an on/off flag for following the desktop */
    bool toUseSystemColors;
    cfg.Read(wxT("Colors/UseSystemColors"), &toUseSystemColors, true);
    int theme;
    cfg.Read(wxT("Colors/Theme"), &theme, (toUseSystemColors ? QSP_THEME_SYSTEM : QSP_THEME_LIGHT));

    Appearance appearance;
    switch (theme)
    {
    case QSP_THEME_LIGHT: appearance = Appearance::Light; break;
    case QSP_THEME_DARK: appearance = Appearance::Dark; break;
    default: appearance = Appearance::System; break;
    }
    SetAppearance(appearance);
}

void QSPApp::InitUI()
{
    wxString configPath = GetSettingsPath();

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
    /* After the settings, so it is said in the player's language */
    ReportLastCrash(frame);
    // ----------------------
    wxInitEvent initEvent;
    /* A game named on the command line wins, and so does auto.qsp; only
       without either does the player go back to the last one. An editor
       starting a dev session gets exactly the game it asked for. */
    bool toRun = GetAutoRunEvent(initEvent);
    if (!toRun && !m_isDevMode)
    {
        wxString resumePath(frame->GetGameToResume());
        if (!resumePath.IsEmpty())
        {
            initEvent.SetInitString(resumePath);
            toRun = true;
        }
    }
    if (toRun)
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
