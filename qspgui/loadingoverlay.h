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
       indistinguishable from a hang - so the wait is now shown.

       It is deliberately nothing more than a spinner on the player's own
       background. Everything the wait has to say, it says with the ring: it
       turns while the step cannot be measured, and fills round once while it
       can. A panel, a caption and a progress bar were all tried here and all
       of them looked like a dialog the reader was expected to read.

       It is a floating frame covering the parent's client area rather than a
       child window, for the same reason the toast is: the description panes
       can be a real browser, whose own window would paint over any sibling put
       on top of it.

       The animation is driven from two places. Its own timer runs it whenever
       the event loop is the frame's, and Tick() runs it from a caller that is
       pumping the loop by hand around a background load.

       Begin() arms the overlay rather than showing it, and it only appears
       once the wait has gone on long enough to be worth admitting to. Most
       loads - a quick save above all - are over before that, and a spinner
       that appears and vanishes inside a blink reads as a glitch. Worse, a
       step that cannot be ticked at all, as restoring a save cannot, would
       put up a ring and freeze it, which reads as a hang. Waiting out the
       delay means such a step is simply never drawn. */
    class QSPLoadingOverlay : public wxFrame
    {
        DECLARE_CLASS(QSPLoadingOverlay)
        DECLARE_EVENT_TABLE()
    public:
        // C-tors / D-tor
        QSPLoadingOverlay(wxWindow *parent);

        // Methods
        /* Arms the overlay: it goes up by itself once the wait passes the
           delay, and not at all if the work finishes first. stage and detail
           name the work for anything that asks the player what it is doing -
           the dev API, mainly - and are not drawn. */
        void Begin(const wxString &stage, const wxString &detail);
        /* The phase the load has reached. Resets the ring to indeterminate,
           because a new phase knows nothing about the last one's sizes. */
        void SetStage(const wxString &stage);
        /* Bytes done out of bytes expected. A total of 0 or less means the
           step cannot be measured, and the ring turns instead of filling. */
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
        /* Puts the overlay on screen if the wait has gone on long enough.
           Does nothing once it is already up. */
        void ShowIfDue();
        /* The spinner's square, in client coordinates */
        wxRect GetSpinnerRect() const;
        /* Given a wxGCDC where the platform has one, so the ring comes out
           antialiased, and the plain paint DC where it does not. */
        void DrawSpinner(wxDC &dc, const wxRect &rect) const;
        /* Somewhere between the background and the text, for the parts of the
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
        /* Kept rather than drawn. The ring says everything the reader is
           shown; these are the record of which step is running, which is what
           a caller passes in and what anything asking the player what it is
           busy with would want. */
        wxString m_stage;
        wxString m_detail;
        wxFileOffset m_done;
        wxFileOffset m_total;
        bool m_isRunning;
        /* The ring's angle comes off this rather than off a tick count, so it
           turns at the same speed whether the timer or a hand-pumped loop is
           driving it. */
        wxStopWatch m_elapsed;
        /* When the animation last moved. Tick() is called as fast as the
           waiting loop goes round, which is faster than anything needs to be
           repainted at. */
        long m_lastTick;
        /* When the overlay went up, or -1 while it is still armed. The ring's
           angle is measured from here, so it always starts at the top rather
           than wherever the delay happened to leave it. */
        long m_shownAt;
        wxTimer m_timer;
        wxColour m_backColor;
        wxColour m_textColor;
    };

#endif
