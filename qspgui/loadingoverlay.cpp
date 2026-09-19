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

/* One twelfth of a turn per tick gives a spinner that reads as moving without
   looking hurried. The sweep of the indeterminate bar is tied to the same
   tick so the two never drift apart. */
#define QSP_LOADING_TICK 60
#define QSP_LOADING_SWEEPSTEP 22
/* Nothing is said about how long this takes until the wait is long enough to
   be worth explaining. A quick load should not flash a paragraph at anyone. */
#define QSP_LOADING_HINTAFTER 2500

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

/* Sizes as the reader thinks of them. A 40 MB game is the case this whole
   overlay exists for, so the number is worth showing. */
wxString FormatSize(wxFileOffset bytes)
{
    if (bytes >= 1024 * 1024)
        return wxString::Format(wxT("%.1f MB"), (double)bytes / (1024.0 * 1024.0));
    if (bytes >= 1024)
        return wxString::Format(wxT("%.0f KB"), (double)bytes / 1024.0);
    return wxString::Format(wxT("%d B"), (int)bytes);
}

}

QSPLoadingOverlay::QSPLoadingOverlay(wxWindow *parent) :
    wxFrame(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxFRAME_TOOL_WINDOW | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE),
    m_done(0),
    m_total(0),
    m_phase(0),
    m_sweep(0),
    m_lastTick(0),
    m_isRunning(false),
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
    m_phase = 0;
    m_sweep = 0;
    m_lastTick = 0;
    m_elapsed.Start();
    m_isRunning = true;

    Reposition();
    if (!IsShown())
    {
    #ifdef __WXMSW__
        /* The frame keeps the keyboard: the overlay has nothing to type into,
           and taking focus away would only have to be given back. */
        ShowWithoutActivating();
    #else
        Show();
    #endif
    }
    Refresh();
    Update();
    m_timer.Start(QSP_LOADING_TICK);
}

void QSPLoadingOverlay::SetStage(const wxString &stage)
{
    if (!m_isRunning) return;

    m_stage = stage;
    m_done = m_total = 0;
    Refresh();
    Update();
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

    ++m_phase;
    m_sweep = (m_sweep + QSP_LOADING_SWEEPSTEP) % 1000;
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

wxRect QSPLoadingOverlay::GetCardRect() const
{
    wxSize size(GetClientSize());
    int width = wxMin(FromDIP(420), size.GetWidth() - FromDIP(40));
    int height = FromDIP(150);
    if (width < FromDIP(160)) width = size.GetWidth();
    /* A little above centre, where a caption is looked for */
    int top = (size.GetHeight() - height) * 45 / 100;
    if (top < 0) top = 0;
    return wxRect((size.GetWidth() - width) / 2, top, width, height);
}

/* The chiselled edge the rest of the player is drawn with: a hard outline, a
   lit top and left inside it, a shaded bottom and right, all off the panel's
   own colour so it is there on any theme. */
void QSPLoadingOverlay::DrawPanel(wxDC &dc, const wxRect &rect) const
{
    bool isDark = IsDark();
    wxColour face(m_backColor.ChangeLightness(isDark ? 118 : 100));

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(face));
    dc.DrawRectangle(rect);

    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(m_backColor.ChangeLightness(isDark ? 165 : 35)));
    dc.DrawRectangle(rect);

    dc.SetPen(wxPen(face.ChangeLightness(isDark ? 135 : 150)));
    dc.DrawLine(rect.x + 1, rect.y + 1, rect.GetRight(), rect.y + 1);
    dc.DrawLine(rect.x + 1, rect.y + 1, rect.x + 1, rect.GetBottom());
    dc.SetPen(wxPen(face.ChangeLightness(isDark ? 60 : 70)));
    dc.DrawLine(rect.x + 1, rect.GetBottom() - 1, rect.GetRight(), rect.GetBottom() - 1);
    dc.DrawLine(rect.GetRight() - 1, rect.y + 1, rect.GetRight() - 1, rect.GetBottom());
}

/* Twelve spokes around a circle, the one at the head in the accent colour and
   the rest fading back into the panel. Spokes rather than an arc because a
   plain wxDC has no antialiasing to smooth a curve with, and a thick line
   reads cleanly at any size. */
void QSPLoadingOverlay::DrawSpinner(wxDC &dc, const wxRect &rect) const
{
    const int spokes = 12;
    int radius = wxMin(rect.GetWidth(), rect.GetHeight()) / 2;
    if (radius < 4) return;

    int cx = rect.x + rect.GetWidth() / 2, cy = rect.y + rect.GetHeight() / 2;
    int inner = radius * 45 / 100;
    int thickness = wxMax(2, radius / 5);
    wxColour face(m_backColor.ChangeLightness(IsDark() ? 118 : 100));
    wxColour accent(GetAccentColor());

    for (int i = 0; i < spokes; ++i)
    {
        /* How far behind the head this spoke is, so the trail dies away
           around the circle rather than all at once */
        int age = (i - m_phase % spokes + spokes) % spokes;
        wxColour color(Mix(accent, face, age * 88 / (spokes - 1)));
        double angle = 2.0 * M_PI * i / spokes - M_PI / 2;
        double dx = cos(angle), dy = sin(angle);
        dc.SetPen(wxPen(color, thickness));
        dc.DrawLine(
            cx + (int)(dx * inner), cy + (int)(dy * inner),
            cx + (int)(dx * radius), cy + (int)(dy * radius));
    }
}

/* Filled to the fraction read while that is known, and a block sweeping from
   side to side while it is not. The sweep is not a guess at progress - it
   says the player is still working, which during a single uninterruptible
   call into the engine is the only honest thing it can say. */
void QSPLoadingOverlay::DrawBar(wxDC &dc, const wxRect &rect) const
{
    bool isDark = IsDark();
    wxColour face(m_backColor.ChangeLightness(isDark ? 118 : 100));
    wxColour trough(face.ChangeLightness(isDark ? 78 : 88));
    wxColour accent(GetAccentColor());

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(trough));
    dc.DrawRectangle(rect);

    if (m_total > 0)
    {
        wxFileOffset done = wxMin(m_done, m_total);
        int width = (int)((wxFileOffset)rect.GetWidth() * done / m_total);
        if (width > 0)
        {
            dc.SetBrush(wxBrush(accent));
            dc.DrawRectangle(rect.x, rect.y, width, rect.GetHeight());
        }
    }
    else
    {
        int blockWidth = wxMax(FromDIP(40), rect.GetWidth() / 4);
        /* Out one side and back in the other, so the block is never clipped
           in a way that looks like it stopped at the edge */
        int span = rect.GetWidth() + blockWidth;
        int x = rect.x - blockWidth + span * m_sweep / 1000;
        wxRect block(x, rect.y, blockWidth, rect.GetHeight());
        block.Intersect(rect);
        if (!block.IsEmpty())
        {
            dc.SetBrush(wxBrush(accent));
            dc.DrawRectangle(block);
        }
    }

    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(m_backColor.ChangeLightness(isDark ? 150 : 55)));
    dc.DrawRectangle(rect);
}

void QSPLoadingOverlay::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size(GetClientSize());
    bool isDark = IsDark();
    wxColour face(m_backColor.ChangeLightness(isDark ? 118 : 100));

    /* The backdrop hides whatever the panes were showing. A half-drawn game
       behind a loading message is worse than no game at all: the reader
       cannot tell which of the two is the truth. */
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_backColor));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    wxRect card(GetCardRect());
    DrawPanel(dc, card);

    int pad = FromDIP(18);
    int spinnerSize = FromDIP(34);
    int gap = FromDIP(16);
    wxRect inner(card.x + pad, card.y + pad, card.GetWidth() - pad * 2, card.GetHeight() - pad * 2);
    if (inner.GetWidth() < FromDIP(60)) return;

    DrawSpinner(dc, wxRect(inner.x, inner.y, spinnerSize, spinnerSize));

    int textX = inner.x + spinnerSize + gap;
    int textWidth = inner.GetRight() - textX + 1;
    if (textWidth < FromDIP(40)) return;

    wxFont stageFont(GetFont());
    stageFont.MakeBold();
    dc.SetFont(stageFont);
    dc.SetTextForeground(m_textColor);
    wxString stage(wxControl::Ellipsize(m_stage, dc, wxELLIPSIZE_END, textWidth));
    dc.DrawText(stage, textX, inner.y);
    int lineHeight = dc.GetTextExtent(stage).GetHeight();

    dc.SetFont(GetFont());
    dc.SetTextForeground(Mix(m_textColor, face, 35));
    /* The middle of a long path says the least, so that is what goes */
    wxString detail(wxControl::Ellipsize(m_detail, dc, wxELLIPSIZE_MIDDLE, textWidth));
    int detailY = inner.y + lineHeight + FromDIP(4);
    dc.DrawText(detail, textX, detailY);
    int detailHeight = dc.GetTextExtent(detail).GetHeight();

    int barHeight = FromDIP(8);
    int barY = inner.GetBottom() - barHeight - FromDIP(2) - dc.GetCharHeight() - FromDIP(6);
    if (barY < detailY + detailHeight + FromDIP(6))
        barY = detailY + detailHeight + FromDIP(6);
    DrawBar(dc, wxRect(inner.x, barY, inner.GetWidth(), barHeight));

    /* The status line under the bar: how far the measurable step has got, or,
       once the wait has gone on long enough to worry anyone, why it has. */
    wxString status;
    if (m_total > 0)
        status = wxString::Format(wxT("%s / %s"), FormatSize(wxMin(m_done, m_total)), FormatSize(m_total));
    else if (m_elapsed.Time() >= QSP_LOADING_HINTAFTER)
        status = wxString::Format(_("Large games take a while to unpack - %d s so far"),
                                  (int)(m_elapsed.Time() / 1000));

    if (!status.IsEmpty())
    {
        dc.SetTextForeground(Mix(m_textColor, face, 50));
        status = wxControl::Ellipsize(status, dc, wxELLIPSIZE_END, inner.GetWidth());
        dc.DrawText(status, inner.x, barY + barHeight + FromDIP(6));
    }
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
