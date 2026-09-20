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

#include "loadingoverlay.h"

#include <wx/dcbuffer.h>
#if wxUSE_GRAPHICS_CONTEXT
    #include <wx/dcgraph.h>
#endif

#include <math.h>

/* 40 fps. The ring turns, so it is worth the frames - and a repaint is one
   arc on a background, which is cheap enough to do this often even inside
   the hand-pumped loop that waits on a load. */
#define QSP_LOADING_TICK 25
/* One turn a second: fast enough to read as working, slow enough not to
   look frantic. */
#define QSP_LOADING_TURNMS 1000.0
/* How much of the circle the head covers while there is nothing to measure */
#define QSP_LOADING_ARCDEG 105.0
/* How long a wait has to last before it is worth showing at all. Under this
   the player simply looks busy for a moment, which is the truth and is what
   a reader expects of a quick save. */
#define QSP_LOADING_SHOWAFTER 250

wxIMPLEMENT_CLASS(QSPLoadingOverlay, wxFrame);

BEGIN_EVENT_TABLE(QSPLoadingOverlay, wxFrame)
    EVT_PAINT(QSPLoadingOverlay::OnPaint)
    EVT_ERASE_BACKGROUND(QSPLoadingOverlay::OnEraseBackground)
    EVT_TIMER(wxID_ANY, QSPLoadingOverlay::OnTimer)
    EVT_MOUSE_EVENTS(QSPLoadingOverlay::OnMouse)
    EVT_KEY_DOWN(QSPLoadingOverlay::OnKey)
    EVT_CHAR(QSPLoadingOverlay::OnKey)
END_EVENT_TABLE()

namespace
{

bool IsDarkColor(const wxColour &color)
{
    return (color.Red() * 30 + color.Green() * 59 + color.Blue() * 11) / 100 < 128;
}

}

QSPLoadingOverlay::QSPLoadingOverlay(wxWindow *parent) :
    wxFrame(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxFRAME_TOOL_WINDOW | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE),
    m_done(0),
    m_total(0),
    m_isRunning(false),
    m_lastTick(0),
    m_shownAt(-1),
    m_timer(this),
    m_backColor(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)),
    m_textColor(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    Hide();
}

void QSPLoadingOverlay::Begin(const wxString &stage, const wxString &detail)
{
    m_stage = stage;
    m_detail = detail;
    m_done = m_total = 0;
    m_lastTick = 0;
    m_shownAt = -1;
    m_elapsed.Start();
    m_isRunning = true;

    /* Armed, not shown. ShowIfDue decides, from the timer or from Tick. */
    m_timer.Start(QSP_LOADING_TICK);
}

void QSPLoadingOverlay::ShowIfDue()
{
    if (m_shownAt >= 0 || m_elapsed.Time() < QSP_LOADING_SHOWAFTER) return;

    m_shownAt = m_elapsed.Time();
    Reposition();
#ifdef __WXMSW__
    /* The frame keeps the keyboard: the overlay has nothing to type into,
       and taking focus away would only have to be given back. */
    ShowWithoutActivating();
#else
    Show();
#endif
    Refresh();
    Update();
}

void QSPLoadingOverlay::SetStage(const wxString &stage)
{
    if (!m_isRunning) return;

    m_stage = stage;
    m_done = m_total = 0;
}

void QSPLoadingOverlay::SetProgress(wxFileOffset done, wxFileOffset total)
{
    if (!m_isRunning) return;

    m_done = done;
    m_total = total;
}

void QSPLoadingOverlay::End()
{
    m_timer.Stop();
    m_isRunning = false;
    m_shownAt = -1;
    Hide();
}

void QSPLoadingOverlay::SetColors(const wxColour &back, const wxColour &text)
{
    if (m_backColor == back && m_textColor == text) return;

    m_backColor = back;
    m_textColor = text;
    if (IsShown()) Refresh();
}

void QSPLoadingOverlay::Tick()
{
    if (!m_isRunning) return;

    long now = m_elapsed.Time();
    if (now - m_lastTick < QSP_LOADING_TICK) return;
    m_lastTick = now;

    ShowIfDue();
    if (m_shownAt < 0) return;

    Reposition();
    Refresh();
    /* Straight to the screen: the caller is in the middle of a load and is
       not going back to the event loop to have the paint delivered. */
    Update();
}

/* Exactly over the parent's client area, so the overlay covers the panes and
   leaves the menu bar and the window frame alone - the player is still the
   player while it is loading. */
void QSPLoadingOverlay::Reposition()
{
    wxWindow *parent = GetParent();
    if (!parent) return;

    wxSize size(parent->GetClientSize());
    if (size.GetWidth() < 1 || size.GetHeight() < 1) return;
    wxPoint pos(parent->ClientToScreen(wxPoint(0, 0)));
    if (pos != GetPosition() || size != GetSize()) SetSize(pos.x, pos.y, size.GetWidth(), size.GetHeight());
}

bool QSPLoadingOverlay::IsDark() const
{
    return IsDarkColor(m_backColor);
}

wxColour QSPLoadingOverlay::Mix(const wxColour &from, const wxColour &to, int percent) const
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    return wxColour(
        (unsigned char)(from.Red() + (to.Red() - from.Red()) * percent / 100),
        (unsigned char)(from.Green() + (to.Green() - from.Green()) * percent / 100),
        (unsigned char)(from.Blue() + (to.Blue() - from.Blue()) * percent / 100));
}

wxColour QSPLoadingOverlay::GetAccentColor() const
{
    return (IsDark() ? wxColour(142, 176, 255) : wxColour(0, 0, 160));
}

/* Centred, and a little above the middle: dead centre sits low to the eye. */
wxRect QSPLoadingOverlay::GetSpinnerRect() const
{
    wxSize size(GetClientSize());
    int side = FromDIP(54);
    int smallest = wxMin(size.GetWidth(), size.GetHeight());
    if (side > smallest / 3) side = smallest / 3;
    if (side < 8) side = smallest;
    return wxRect((size.GetWidth() - side) / 2, (size.GetHeight() - side) * 46 / 100, side, side);
}

void QSPLoadingOverlay::DrawSpinner(wxDC &dc, const wxRect &rect) const
{
    /* Clockwise from twelve o'clock, in degrees, the way the eye reads it. */
    double head, sweep;
    if (m_total > 0)
    {
        wxFileOffset done = wxMin(m_done, m_total);
        head = 0.0;
        sweep = 360.0 * (double)done / (double)m_total;
        /* Enough of an arc to read as an arc the moment a step begins,
           rather than a speck until the first block comes back */
        if (sweep < 14.0) sweep = 14.0;
    }
    else
    {
        double since = (double)(m_elapsed.Time() - wxMax(m_shownAt, 0));
        head = fmod(since * 360.0 / QSP_LOADING_TURNMS, 360.0);
        sweep = QSP_LOADING_ARCDEG;
    }

    int thickness = wxMax(2, rect.GetWidth() / 8);
    wxRect circle(rect);
    circle.Deflate(thickness / 2);
    if (circle.GetWidth() < 4 || circle.GetHeight() < 4) return;

    /* The track is the whole circle the head runs on. It has to be there to
       close the shape, and quiet enough not to compete with the head. */
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(Mix(m_backColor, m_textColor, IsDark() ? 18 : 13), thickness));
    dc.DrawEllipse(circle);

    if (sweep >= 359.5) return;

    /* wxDC angles run counter-clockwise from three o'clock and the arc is
       drawn from the first to the second, so the head's two ends swap. */
    dc.SetPen(wxPen(GetAccentColor(), thickness));
    dc.DrawEllipticArc(circle.x, circle.y, circle.GetWidth(), circle.GetHeight(),
                       90.0 - (head + sweep), 90.0 - head);
}

void QSPLoadingOverlay::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    /* Buffered by hand rather than through wxAutoBufferedPaintDC, because
       what wxGCDC can be built over is the memory DC this gives us on every
       platform, and the ring is the one thing here that needs the graphics
       context's antialiasing. */
    wxBufferedPaintDC dc(this);
    wxSize size(GetClientSize());

    /* The backdrop hides whatever the panes were showing. A half-drawn game
       behind a loading spinner is worse than no game at all: the reader
       cannot tell which of the two is the truth. */
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_backColor));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

#if wxUSE_GRAPHICS_CONTEXT
    wxGCDC gcdc(dc);
    if (gcdc.IsOk())
    {
        DrawSpinner(gcdc, GetSpinnerRect());
        return;
    }
#endif
    DrawSpinner(dc, GetSpinnerRect());
}

void QSPLoadingOverlay::OnEraseBackground(wxEraseEvent& WXUNUSED(event))
{
}

void QSPLoadingOverlay::OnTimer(wxTimerEvent& WXUNUSED(event))
{
    Tick();
}

void QSPLoadingOverlay::OnMouse(wxMouseEvent& WXUNUSED(event))
{
}

void QSPLoadingOverlay::OnKey(wxKeyEvent& WXUNUSED(event))
{
}
