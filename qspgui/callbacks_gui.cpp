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

#include "callbacks_gui.h"
#include "devserver.h"
#include "comtools.h"

namespace
{
    /* Read a string variable a game may also fill as an array: $USERJSFILE on
       its own is one script, $USERJSFILE[0], [1], ... is a list of them. Empty
       items are dropped so a game can clear one without shifting the rest. */
    void qspGetStringList(QSPString name, wxArrayString& items)
    {
        int i, count = 0;
        QSPString value;
        items.Empty();
        if (!QSPGetVarValuesCount(name, &count)) return;
        for (i = 0; i < count; ++i)
        {
            if (QSPGetStrVarValue(name, i, &value) && !qspIsEmpty(value))
                items.Add(qspToWxString(value));
        }
    }

    /* Same variables, but as one blob: several items are simply stacked, which
       lets a game build up its CSS or JS from pieces. */
    wxString qspGetStringBlob(QSPString name)
    {
        wxArrayString items;
        qspGetStringList(name, items);
        return wxJoin(items, wxT('\n'), (wxChar)0);
    }
}

QSPFrame *QSPCallbacks::m_frame;
bool QSPCallbacks::m_isHtml;
QSPSounds QSPCallbacks::m_sounds;
float QSPCallbacks::m_volumeCoeff;
QSPVersionInfoValues QSPCallbacks::m_versionInfo;

void QSPCallbacks::Init(QSPFrame *frame)
{
    m_frame = frame;
    m_volumeCoeff = 1.0;

    if (sound_init_engine() < 0)
        wxLogError("Can't initialize sound engine");
    else
    {
        wxString soundFontPath(QSPTools::GetResourcePath(QSP_SOUNDPLUGINS, QSP_MIDISOUNDFONT));
#ifdef _UNICODE
        int soundFontInitResult = soundfont_init_w(soundFontPath.c_str());
#else
        int soundFontInitResult = soundfont_init(soundFontPath.c_str());
#endif
        if (soundFontInitResult < 0)
            wxLogError("Can't load soundfont to play MIDI files");
    }

    QSPSetCallback(QSP_CALL_SETTIMER, (QSP_CALLBACK)&SetTimer);
    QSPSetCallback(QSP_CALL_REFRESHINT, (QSP_CALLBACK)&RefreshInt);
    QSPSetCallback(QSP_CALL_SETINPUTSTRTEXT, (QSP_CALLBACK)&SetInputStrText);
    QSPSetCallback(QSP_CALL_ISPLAYINGFILE, (QSP_CALLBACK)&IsPlay);
    QSPSetCallback(QSP_CALL_PLAYFILE, (QSP_CALLBACK)&PlayFile);
    QSPSetCallback(QSP_CALL_CLOSEFILE, (QSP_CALLBACK)&CloseFile);
    QSPSetCallback(QSP_CALL_SHOWMSGSTR, (QSP_CALLBACK)&Msg);
    QSPSetCallback(QSP_CALL_SLEEP, (QSP_CALLBACK)&Sleep);
    QSPSetCallback(QSP_CALL_GETMSCOUNT, (QSP_CALLBACK)&GetMSCount);
    QSPSetCallback(QSP_CALL_SHOWMENU, (QSP_CALLBACK)&ShowMenu);
    QSPSetCallback(QSP_CALL_INPUTBOX, (QSP_CALLBACK)&Input);
    QSPSetCallback(QSP_CALL_SHOWIMAGE, (QSP_CALLBACK)&ShowImage);
    QSPSetCallback(QSP_CALL_SHOWWINDOW, (QSP_CALLBACK)&ShowPane);
    QSPSetCallback(QSP_CALL_OPENGAME, (QSP_CALLBACK)&OpenGame);
    QSPSetCallback(QSP_CALL_OPENGAMESTATUS, (QSP_CALLBACK)&OpenGameStatus);
    QSPSetCallback(QSP_CALL_SAVEGAMESTATUS, (QSP_CALLBACK)&SaveGameStatus);
    QSPSetCallback(QSP_CALL_VERSION, (QSP_CALLBACK)&Version);

    /* Prepare version values */
    m_versionInfo["player"] = "Classic";
    m_versionInfo["platform"] = QSPTools::GetPlatform();
}

void QSPCallbacks::DeInit()
{
    CloseFile(qspStringFromPair(0, 0));
    sound_free_engine();
}

int QSPCallbacks::SetTimer(int msecs)
{
    if (m_frame->ToQuit()) return 0;
    if (msecs)
        m_frame->GetTimer()->Start(msecs);
    else
        m_frame->GetTimer()->Stop();
    return 0;
}

int QSPCallbacks::RefreshInt(QSP_BOOL isForced, QSP_BOOL isNewDesc)
{
    /* A forced refresh yields to the event loop with game code still running */
    QSPDev::EngineScope engineScope;
    /* Everything below is the player's answer to one line of game code, and
       none of it is visible to the line profiler except as time passing. A
       game that refreshes inside a loop finds out here. */
    QSPDev::ProfScope refreshScope(QSPDev::Prof_Refresh);
    int changedState;
    QSP_BIGINT numVal;
    QSPString strVal;
    bool toScroll, canSave;
    if (m_frame->ToQuit()) return 0;
    changedState = QSPGetWindowsChangedState();
    /* Hold the description panes back until everything below has been worked
       out, then let each apply its changes in one go - the engine often clears
       a pane and refills it during the same refresh. */
    m_frame->GetDesc()->BeginUpdate();
    m_frame->GetVars()->BeginUpdate();
    wxON_BLOCK_EXIT_OBJ0(*m_frame->GetVars(), QSPMainTextBox::EndUpdate);
    wxON_BLOCK_EXIT_OBJ0(*m_frame->GetDesc(), QSPMainTextBox::EndUpdate);
    // -------------------------------
    toScroll = !(QSPGetNumVarValue(QSP_STATIC_STR(QSP_FMT("DISABLESCROLL")), 0, &numVal) && numVal);
    canSave = !(QSPGetNumVarValue(QSP_STATIC_STR(QSP_FMT("NOSAVE")), 0, &numVal) && numVal);
    m_isHtml = QSPGetNumVarValue(QSP_STATIC_STR(QSP_FMT("USEHTML")), 0, &numVal) && numVal;
    // -------------------------------
    m_frame->GetDesc()->SetIsHtml(m_isHtml);
    if (changedState & QSP_WIN_MAIN)
    {
        QSPDev::ProfScope descScope(QSPDev::Prof_MainDesc);
        QSPString mainDesc = QSPGetMainDesc();
        wxString text(qspToWxString(mainDesc));
        /* Counted as well as timed: a description that has grown to a megabyte
           is slow for a reason no timing alone would name. */
        descScope.AddBytes((long long)text.length());
        // we don't scroll main description if it's completely updated (isNewDesc is true)
        m_frame->GetDesc()->SetText(text, !isNewDesc && toScroll);
    }
    // -------------------------------
    m_frame->GetVars()->SetIsHtml(m_isHtml);
    if (changedState & QSP_WIN_VARS)
    {
        QSPDev::ProfScope varsScope(QSPDev::Prof_VarsDesc);
        QSPString varsDesc = QSPGetVarsDesc();
        wxString text(qspToWxString(varsDesc));
        varsScope.AddBytes((long long)text.length());
        // we always try to scroll additional description
        m_frame->GetVars()->SetText(text, toScroll);
    }
    // -------------------------------
    m_frame->GetActions()->SetIsHtml(m_isHtml);
    m_frame->GetActions()->SetToShowNums(m_frame->ToShowHotkeys());
    if (changedState & QSP_WIN_ACTS)
    {
        QSPDev::ProfScope actionsScope(QSPDev::Prof_Actions);
        QSPListItem items[MAX_LIST_ITEMS];
        int i, actionsCount = QSPGetActions(items, MAX_LIST_ITEMS);
        m_frame->GetActions()->BeginItems();
        for (i = 0; i < actionsCount; ++i)
            m_frame->GetActions()->AddItem(qspToWxString(items[i].Image), qspToWxString(items[i].Name));
        m_frame->GetActions()->EndItems();
    }
    m_frame->GetActions()->SetSelection(QSPGetSelActionIndex());
    m_frame->GetObjects()->SetIsHtml(m_isHtml);
    if (changedState & QSP_WIN_OBJS)
    {
        QSPDev::ProfScope objectsScope(QSPDev::Prof_Objects);
        QSPObjectItem items[MAX_LIST_ITEMS];
        int i, objectsCount = QSPGetObjects(items, MAX_LIST_ITEMS);
        m_frame->GetObjects()->BeginItems();
        for (i = 0; i < objectsCount; ++i)
            m_frame->GetObjects()->AddItem(qspToWxString(items[i].Image), qspToWxString(items[i].Title));
        m_frame->GetObjects()->EndItems();
    }
    m_frame->GetObjects()->SetSelection(QSPGetSelObjectIndex());
    // -------------------------------
    if (QSPGetStrVarValue(QSP_STATIC_STR(QSP_FMT("BACKIMAGE")), 0, &strVal) && !qspIsEmpty(strVal))
        m_frame->GetDesc()->LoadBackImage(qspToWxString(strVal));
    else
        m_frame->GetDesc()->LoadBackImage(wxEmptyString);
    // -------------------------------
    /* Game-supplied CSS and JS, read here exactly like $BACKIMAGE above, so
       the variables are the whole interface and a game never has to know which
       renderer it is running under.

       Every pane is a separate document, and all of them get the stylesheets -
       one sheet themes the whole player. Only the description panes run the
       scripts: that is where window.qsp is, and running them in the lists and
       the picture too would mean four more copies of whatever state they set
       up. The two description panes tell themselves apart through qsp.pane. */
    {
        wxString userCss(qspGetStringBlob(QSP_STATIC_STR(QSP_FMT("USERCSS"))));
        wxString userJs(qspGetStringBlob(QSP_STATIC_STR(QSP_FMT("USERJS"))));
        wxArrayString cssFiles, jsFiles;
        qspGetStringList(QSP_STATIC_STR(QSP_FMT("USERCSSFILE")), cssFiles);
        qspGetStringList(QSP_STATIC_STR(QSP_FMT("USERJSFILE")), jsFiles);
        m_frame->GetDesc()->SetUserStyles(userCss, cssFiles);
        m_frame->GetVars()->SetUserStyles(userCss, cssFiles);
        m_frame->GetActions()->SetUserStyles(userCss, cssFiles);
        m_frame->GetObjects()->SetUserStyles(userCss, cssFiles);
        m_frame->GetImgView()->SetUserStyles(userCss, cssFiles);
        m_frame->GetDesc()->SetUserScripts(userJs, jsFiles);
        m_frame->GetVars()->SetUserScripts(userJs, jsFiles);
    }
    // -------------------------------
    m_frame->ApplyParams();
    /* Everything is settled, so apply it now rather than at scope exit: the
       forced branch below paints and pumps the loop, and must not show the
       previous refresh's content. The guards above become no-ops. */
    m_frame->GetDesc()->EndUpdate();
    m_frame->GetVars()->EndUpdate();
    if (isForced)
    {
        m_frame->EnableControls(false, true);
        m_frame->Update();
        wxTheApp->Yield(true);
        if (m_frame->ToQuit()) return 0;
        m_frame->EnableControls(true, true);
    }
    m_frame->GetGameMenu()->Enable(ID_SAVEGAMESTAT, canSave);
    m_frame->GetGameMenu()->Enable(ID_QUICKSAVE, canSave);
    m_frame->GetGameMenu()->Enable(ID_QUICKSAVESLOT, canSave);
    if (m_frame->GetDevServer()) m_frame->GetDevServer()->NotifyRefreshed(isNewDesc != QSP_FALSE);
    return 0;
}

int QSPCallbacks::SetInputStrText(QSPString text)
{
    if (m_frame->ToQuit()) return 0;
    m_frame->GetInput()->SetText(qspToWxString(text));
    return 0;
}

int QSPCallbacks::IsPlay(QSPString file)
{
    wxString fileName(qspToWxString(file));
    QSPSounds::iterator elem = m_sounds.find(fileName.Upper());
    if (elem != m_sounds.end() && elem->second.IsPlaying())
        return QSP_TRUE;
    return QSP_FALSE;
}

int QSPCallbacks::CloseFile(QSPString file)
{
    if (file.Str)
    {
        wxString fileName(qspToWxString(file));
        QSPSounds::iterator elem = m_sounds.find(fileName.Upper());
        if (elem != m_sounds.end())
        {
            elem->second.Close();
            m_sounds.erase(elem);
        }
    }
    else
    {
        for (QSPSounds::iterator i = m_sounds.begin(); i != m_sounds.end(); ++i)
            i->second.Close();
        m_sounds.clear();
    }
    return 0;
}

int QSPCallbacks::PlayFile(QSPString file, int volume)
{
    QSPDev::ProfScope soundScope(QSPDev::Prof_Sound);
    QSPSound snd;
    if (SetVolume(file, volume)) return 0;
    CloseFile(file);
    wxString fileName(qspToWxString(file));
    wxString filePath(m_frame->ComposeGamePath(fileName));
    if (!snd.Play(filePath, volume, m_volumeCoeff))
        return 0;
    UpdateSounds();
    m_sounds.insert(QSPSounds::value_type(fileName.Upper(), snd));
    return 0;
}

int QSPCallbacks::ShowPane(int type, QSP_BOOL toShow)
{
    if (m_frame->ToQuit()) return 0;
    if (type & QSP_WIN_VARS)
        m_frame->ShowPane(ID_VARSDESC, toShow != QSP_FALSE);
    if (type & QSP_WIN_ACTS)
        m_frame->ShowPane(ID_ACTIONS, toShow != QSP_FALSE);
    if (type & QSP_WIN_OBJS)
        m_frame->ShowPane(ID_OBJECTS, toShow != QSP_FALSE);
    if (type & QSP_WIN_INPUT)
        m_frame->ShowPane(ID_INPUT, toShow != QSP_FALSE);
    if (type & QSP_WIN_VIEW)
        m_frame->ShowPane(ID_VIEWPIC, toShow != QSP_FALSE);
    return 0;
}

int QSPCallbacks::Sleep(int msecs)
{
    /* Yields to the event loop with game code still running */
    QSPDev::EngineScope engineScope;
    if (m_frame->ToQuit()) return 0;
    bool canSaveGame = m_frame->GetGameMenu()->IsEnabled(ID_SAVEGAMESTAT);
    bool canQuicksave = m_frame->GetGameMenu()->IsEnabled(ID_QUICKSAVE);
    bool canQuicksaveSlot = m_frame->GetGameMenu()->IsEnabled(ID_QUICKSAVESLOT);
    bool toBreak = false;
    m_frame->EnableControls(false, true);
    int i, count = msecs / 50;
    for (i = 0; i < count; ++i)
    {
        wxThread::Sleep(50);
        m_frame->Update();
        wxTheApp->Yield(true);
        if (m_frame->ToQuit() ||
            m_frame->IsKeyPressedWhileDisabled())
        {
            toBreak = true;
            break;
        }
    }
    if (!toBreak)
    {
        wxThread::Sleep(msecs % 50);
        m_frame->Update();
        wxTheApp->Yield(true);
    }
    m_frame->EnableControls(true, true);
    m_frame->GetGameMenu()->Enable(ID_SAVEGAMESTAT, canSaveGame);
    m_frame->GetGameMenu()->Enable(ID_QUICKSAVE, canQuicksave);
    m_frame->GetGameMenu()->Enable(ID_QUICKSAVESLOT, canQuicksaveSlot);
    return 0;
}

int QSPCallbacks::GetMSCount()
{
    static wxStopWatch stopWatch;
    int ret = stopWatch.Time();
    stopWatch.Start();
    return ret;
}

int QSPCallbacks::Msg(QSPString str)
{
    /* Yields to the event loop with game code still running */
    QSPDev::EngineScope engineScope;
    QSPDev::ProfScope dialogScope(QSPDev::Prof_Dialog);
    if (m_frame->ToQuit()) return 0;
    QSPMsgDlg dialog(m_frame,
        wxID_ANY,
        m_frame->GetDesc()->GetBackgroundColour(),
        m_frame->GetDesc()->GetForegroundColour(),
        m_frame->GetDesc()->GetTextFont(),
        _("Info"),
        qspToWxString(str),
        m_isHtml,
        m_frame
    );
    if (m_frame->GetDevServer()) m_frame->GetDevServer()->NotifyMessage(qspToWxString(str));
    m_frame->EnableControls(false);
    dialog.ShowModal();
    m_frame->EnableControls(true);
    return 0;
}

int QSPCallbacks::ShowMenu(QSPListItem *items, int count)
{
    /* Yields to the event loop with game code still running */
    QSPDev::EngineScope engineScope;
    QSPDev::ProfScope dialogScope(QSPDev::Prof_Dialog);
    if (m_frame->ToQuit()) return -1;
    m_frame->EnableControls(false);
    m_frame->DeleteMenu();
    for (int i = 0; i < count; ++i)
        m_frame->AddMenuItem(qspToWxString(items[i].Name), qspToWxString(items[i].Image));
    int index = m_frame->ShowMenu();
    m_frame->EnableControls(true);
    return index;
}

int QSPCallbacks::Input(QSPString text, QSP_CHAR *buffer, int maxLen)
{
    /* Yields to the event loop with game code still running */
    QSPDev::EngineScope engineScope;
    QSPDev::ProfScope dialogScope(QSPDev::Prof_Dialog);
    if (m_frame->ToQuit()) return 0;
    QSPInputDlg dialog(m_frame,
        wxID_ANY,
        m_frame->GetDesc()->GetBackgroundColour(),
        m_frame->GetDesc()->GetForegroundColour(),
        m_frame->GetDesc()->GetTextFont(),
        _("Input data"),
        qspToWxString(text),
        m_isHtml,
        m_frame
    );
    m_frame->EnableControls(false);
    dialog.ShowModal();
    m_frame->EnableControls(true);
#ifdef _UNICODE
    wcsncpy(buffer, dialog.GetText().c_str(), maxLen);
#else
    strncpy(buffer, dialog.GetText().c_str(), maxLen);
#endif
    return 0;
}

int QSPCallbacks::ShowImage(QSPString file)
{
    QSPDev::ProfScope imageScope(QSPDev::Prof_Image);
    if (m_frame->ToQuit()) return 0;
    if (file.Str)
    {
        wxString imgFullPath(m_frame->ComposeGamePath(qspToWxString(file)));
        m_frame->ShowPane(ID_VIEWPIC, m_frame->GetImgView()->OpenFile(imgFullPath));
    }
    else
    {
        m_frame->ShowPane(ID_VIEWPIC, false);
    }
    return 0;
}

int QSPCallbacks::OpenGame(QSPString file, QSP_BOOL isNewGame)
{
    if (m_frame->ToQuit()) return 0;
    wxString fullPath(m_frame->ComposeGamePath(qspToWxString(file)));
    /* Existing is not the same as readable: another program may be holding the
       file open while it writes it, and an empty file is not a world. */
    std::vector<char> world;
    if (!QSPFileIO::Read(fullPath, world) || world.empty()) return 0;

    if (QSPLoadGameWorldFromData(&world[0], (int)world.size(), isNewGame) && isNewGame)
    {
        /* The whole path, not just the folder: the quick save slot and the
           development API's reload both key off the world file, and OPENQST
           is how a game moves between them. */
        m_frame->UpdateGameFile(fullPath);
        if (m_frame->GetDevServer()) m_frame->GetDevServer()->NotifyGameOpened(fullPath);
    }
    return 0;
}

int QSPCallbacks::OpenGameStatus(QSPString file)
{
    /* The file dialog below yields to the event loop with game code running */
    QSPDev::EngineScope engineScope;
    QSPDev::ProfScope saveScope(QSPDev::Prof_SaveLoad);
    if (m_frame->ToQuit()) return 0;
    wxString fullPath;
    if (file.Str)
    {
        fullPath = m_frame->ComposeGamePath(qspToWxString(file));
    }
    else
    {
        wxFileDialog dialog(m_frame, _("Select saved game file"), wxEmptyString, wxEmptyString, _("Saved game files (*.sav)|*.sav"), wxFD_OPEN);
        m_frame->EnableControls(false);
        int res = dialog.ShowModal();
        m_frame->EnableControls(true);
        if (res != wxID_OK)
            return 0;
        fullPath = dialog.GetPath();
    }
    /* See OpenGame: the file may exist and still not be readable */
    std::vector<char> state;
    if (!QSPFileIO::Read(fullPath, state) || state.empty()) return 0;

    QSPOpenSavedGameFromData(&state[0], (int)state.size(), QSP_FALSE);
    return 0;
}

int QSPCallbacks::SaveGameStatus(QSPString file)
{
    /* The file dialog below yields to the event loop with game code running */
    QSPDev::EngineScope engineScope;
    QSPDev::ProfScope saveScope(QSPDev::Prof_SaveLoad);
    if (m_frame->ToQuit()) return 0;
    wxString fullPath;
    if (file.Str)
    {
        fullPath = m_frame->ComposeGamePath(qspToWxString(file));
    }
    else
    {
        wxFileDialog dialog(m_frame, _("Select file to save"), wxEmptyString, wxEmptyString, _("Saved game files (*.sav)|*.sav"), wxFD_SAVE);
        m_frame->EnableControls(false);
        int res = dialog.ShowModal();
        m_frame->EnableControls(true);
        if (res != wxID_OK)
            return 0;
        fullPath = dialog.GetPath();
    }
    std::vector<char> state;
    if (!QSPGameState::Save(state, false)) return 0;

    QSPFileIO::Write(fullPath, state);
    return 0;
}

int QSPCallbacks::Version(QSPString param, QSP_CHAR *buffer, int maxLen)
{
    wxString result;
    wxString request(qspToWxString(param));

    if (request.IsEmpty())
    {
        QSPString libVersion = QSPGetVersion();
        result = QSPTools::GetVersion(qspToWxString(libVersion));
    }
    else
    {
        QSPVersionInfoValues::iterator value = m_versionInfo.find(request.Lower());
        if (value != m_versionInfo.end())
            result = value->second;
    }

#ifdef _UNICODE
    wcsncpy(buffer, result.c_str(), maxLen);
#else
    strncpy(buffer, result.c_str(), maxLen);
#endif
    return 0;
}

bool QSPCallbacks::SetVolume(QSPString file, int volume)
{
    if (!IsPlay(file)) return false;
    wxString fileName(qspToWxString(file));
    QSPSounds::iterator elem = m_sounds.find(fileName.Upper());
    if (elem != m_sounds.end())
    {
        QSPSound *snd = &elem->second;
        snd->SetVolume(volume, m_volumeCoeff);
        return true;
    }
    return false;
}

void QSPCallbacks::SetOverallVolume(float coeff)
{
    if (coeff < 0.0)
        coeff = 0.0;
    else if (coeff > 1.0)
        coeff = 1.0;
    m_volumeCoeff = coeff;
    for (QSPSounds::iterator i = m_sounds.begin(); i != m_sounds.end(); ++i)
    {
        QSPSound *snd = &i->second;
        if (snd->IsPlaying())
            snd->SetVolume(snd->Volume, m_volumeCoeff);
    }
}

void QSPCallbacks::UpdateSounds()
{
    QSPSound *snd;
    QSPSounds::iterator i = m_sounds.begin();
    while (i != m_sounds.end())
    {
        snd = &i->second;
        if (snd->IsPlaying())
            ++i;
        else
        {
            snd->Close();
            m_sounds.erase(i++);
        }
    }
}
