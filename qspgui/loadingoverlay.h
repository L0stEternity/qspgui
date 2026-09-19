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

#ifndef LOADINGOVERLAY_H
    #define LOADINGOVERLAY_H

    #include <wx/wx.h>
    #include <wx/filefn.h>
    #include <wx/stopwatch.h>

    /* What the player is doing while the reader waits.

       A large game is tens of megabytes of ciphered text and a save of it can
       be another fifteen, and all of it is decoded on one core. Until this
       existed the window simply stopped answering for those seconds, which is
       indistinguishable from a hang - so the wait is now named, counted and
       animated.

       It is a floating frame covering the parent's client area rather than a
       child window, for the same reason the toast is: the description panes
       can be a real browser, whose own window would paint over any sibling put
       on top of it.

       The animation is driven from two places. Its own timer runs it whenever
       the event loop is the frame's, and Tick() runs it from a caller that is
       pumping the loop by hand around a background load. */
    class QSPLoadingOverlay : public wxFrame
    {
        DECLARE_CLASS(QSPLoadingOverlay)
        DECLARE_EVENT_TABLE()
    public:
        // C-tors / D-tor
        QSPLoadingOverlay(wxWindow *parent);

        // Methods
        /* Puts the overlay up. detail is the thing being loaded - a file name,
           or a slot - and is shown under the stage. */
        void Begin(const wxString &stage, const wxString &detail);
        /* The phase the load has reached. Resets the bar to indeterminate,
           because a new phase knows nothing about the last one's sizes. */
        void SetStage(const wxString &stage);
        /* Bytes done out of bytes expected. A total of 0 or less means the
           step cannot be measured, and the bar sweeps instead of filling. */
        void SetProgress(wxFileOffset done, wxFileOffset total);
        void End();
        /* The player's own palette, as the toast takes it: this is the player
           speaking over the game, not part of the page. */
        void SetColors(const wxColour &back, const wxColour &text);

        /* Advances the animation and repaints now. Called from the loop that
           waits on a background load, which is pumping events itself. */
        void Tick();

        bool IsRunning() const { return m_isRunning; }

    protected:
        // Internal methods
        void Reposition();
        /* The card in the middle, in client coordinates */
        wxRect GetCardRect() const;
        void DrawPanel(wxDC &dc, const wxRect &rect) const;
        void DrawSpinner(wxDC &dc, const wxRect &rect) const;
        void DrawBar(wxDC &dc, const wxRect &rect) const;
        /* Somewhere between the panel and the text, for the parts of the
           drawing that have to read as quieter than either */
        wxColour Mix(const wxColour &from, const wxColour &to, int percent) const;
        wxColour GetAccentColor() const;
        bool IsDark() const;

        // Events
        void OnPaint(wxPaintEvent& event);
        void OnEraseBackground(wxEraseEvent& event);
        void OnTimer(wxTimerEvent& event);
        /* The overlay swallows what it covers: a click or a key going through
           to the game underneath is what it is there to prevent. */
        void OnMouse(wxMouseEvent& event);
        void OnKey(wxKeyEvent& event);

        // Fields
        wxString m_stage;
        wxString m_detail;
        wxFileOffset m_done;
        wxFileOffset m_total;
        int m_phase; /* the spinner's position, in twelfths of a turn */
        int m_sweep; /* the indeterminate bar's position, 0..1000 */
        /* When the animation last moved. Tick() is called as fast as the
           waiting loop goes round, which is not the speed the spinner should
           turn at. */
        long m_lastTick;
        bool m_isRunning;
        wxStopWatch m_elapsed;
        wxTimer m_timer;
        wxColour m_backColor;
        wxColour m_textColor;
    };

#endif
