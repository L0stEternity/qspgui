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

#include "toast.h"

#include <wx/dcbuffer.h>

/* How long the message stays up. The tick only has to be short enough for the
   panel to follow a window being dragged about while it is on screen. */
#define QSP_TOAST_HOLD 2400
#define QSP_TOAST_TICK 100

wxIMPLEMENT_CLASS(QSPToast, wxFrame);

BEGIN_EVENT_TABLE(QSPToast, wxFrame)
    EVT_PAINT(QSPToast::OnPaint)
    EVT_ERASE_BACKGROUND(QSPToast::OnEraseBackground)
    EVT_TIMER(wxID_ANY, QSPToast::OnTimer)
    EVT_LEFT_DOWN(QSPToast::OnMouseClick)
END_EVENT_TABLE()

namespace
{

/* Which way round the panel is, so the symbol can be drawn in a colour that
   is actually visible on it: the tooltip colours are the desktop's, and on a
   dark desktop they are dark. */
bool IsDarkColor(const wxColour &color)
{
    return (color.Red() * 30 + color.Green() * 59 + color.Blue() * 11) / 100 < 128;
}

}

QSPToast::QSPToast(wxWindow *parent) :
    wxFrame(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxFRAME_TOOL_WINDOW | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE),
    m_kind(QSP_TOAST_INFO),
    m_timer(this),
    m_holdLeft(0),
    m_backColor(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOBK)),
    m_textColor(wxSystemSettings::GetColour(wxSYS_COLOUR_INFOTEXT))
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    /* The font the desktop puts in its own tooltips and status bars */
    SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
    Hide();
}

void QSPToast::Pop(const wxString &text, QSPToastKind kind)
{
    m_text = text;
    m_kind = kind;
    m_holdLeft = QSP_TOAST_HOLD;

    Relayout();
    Reposition();
    Refresh();

    if (!IsShown())
    {
    #ifdef __WXMSW__
        /* Never take the keyboard away from the game while it is playing */
        ShowWithoutActivating();
    #else
        Show();
    #endif
    }
    m_timer.Start(QSP_TOAST_TICK);
}

void QSPToast::Dismiss()
{
    m_timer.Stop();
    Hide();
}

void QSPToast::SetColors(const wxColour &back, const wxColour &text)
{
    if (m_backColor == back && m_textColor == text) return;

    m_backColor = back;
    m_textColor = text;
    if (IsShown()) Refresh();
}

void QSPToast::Relayout()
{
    wxClientDC dc(this);
    dc.SetFont(GetFont());

    int padX = FromDIP(10), padY = FromDIP(7);
    int symbolSize = FromDIP(16), gap = FromDIP(8), border = FromDIP(2);
    wxSize textSize(dc.GetTextExtent(m_text));
    int width = border * 2 + padX * 2 + symbolSize + gap + textSize.GetWidth();
    int height = border * 2 + padY * 2 + wxMax(textSize.GetHeight(), symbolSize);

    /* Never wider than the window it belongs to */
    wxWindow *parent = GetParent();
    if (parent)
    {
        int maxWidth = parent->GetClientSize().GetWidth() - FromDIP(40);
        if (maxWidth > FromDIP(120) && width > maxWidth) width = maxWidth;
    }
    SetSize(width, height);
}

/* The bottom right corner of the window, where a status message has always
   gone - not the middle of the screen. */
void QSPToast::Reposition()
{
    wxWindow *parent = GetParent();
    if (!parent) return;

    wxSize size(GetSize());
    wxSize parentSize(parent->GetClientSize());
    int margin = FromDIP(12);
    wxPoint pos(parent->ClientToScreen(wxPoint(
        parentSize.GetWidth() - size.GetWidth() - margin,
        parentSize.GetHeight() - size.GetHeight() - margin)));
    /* Called on every tick, so a window that has not moved is left alone */
    if (pos != GetPosition()) Move(pos);
}

wxColour QSPToast::GetKindColor() const
{
    bool isDark = IsDarkColor(m_backColor);
    switch (m_kind)
    {
    case QSP_TOAST_SUCCESS:
        return (isDark ? wxColour(126, 205, 136) : wxColour(0, 110, 30));
    case QSP_TOAST_ERROR:
        return (isDark ? wxColour(255, 130, 120) : wxColour(168, 0, 0));
    default:
        return (isDark ? wxColour(142, 176, 255) : wxColour(0, 0, 160));
    }
}

void QSPToast::DrawKindSymbol(wxDC &dc, const wxRect &rect) const
{
    wxColour color(GetKindColor());
    int size = wxMin(rect.GetWidth(), rect.GetHeight());
    int x = rect.GetX(), y = rect.GetY();
    int thickness = wxMax(2, size / 8);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(color));
    if (m_kind == QSP_TOAST_SUCCESS)
    {
        /* A check mark, thick enough to read at any size */
        dc.SetPen(wxPen(color, thickness));
        dc.DrawLine(x + size * 15 / 100, y + size / 2, x + size * 40 / 100, y + size * 78 / 100);
        dc.DrawLine(x + size * 40 / 100, y + size * 78 / 100, x + size * 85 / 100, y + size * 20 / 100);
        return;
    }
    /* An exclamation mark for trouble, the same shape upside down for a note:
       both are a bar and a dot, which is all they have ever been. */
    int barWidth = wxMax(2, size * 14 / 100);
    int barX = x + (size - barWidth) / 2;
    if (m_kind == QSP_TOAST_ERROR)
    {
        dc.DrawRectangle(barX, y + size * 12 / 100, barWidth, size * 50 / 100);
        dc.DrawRectangle(barX, y + size * 74 / 100, barWidth, barWidth);
    }
    else
    {
        dc.DrawRectangle(barX, y + size * 12 / 100, barWidth, barWidth);
        dc.DrawRectangle(barX, y + size * 38 / 100, barWidth, size * 50 / 100);
    }
}

void QSPToast::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size(GetClientSize());
    int padX = FromDIP(10), border = FromDIP(2);
    int symbolSize = FromDIP(16), gap = FromDIP(8);
    bool isDark = IsDarkColor(m_backColor);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(m_backColor));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    /* The chiselled edge every panel in this player has had: a hard outline,
       a lit top and left inside it, a shaded bottom and right. All three come
       off the panel's own colour, so the edge is there on a light theme, on a
       dark one, and on whatever the user picked by hand. */
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.SetPen(wxPen(m_backColor.ChangeLightness(isDark ? 165 : 35)));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    dc.SetPen(wxPen(m_backColor.ChangeLightness(isDark ? 135 : 150)));
    dc.DrawLine(1, 1, size.GetWidth() - 1, 1);
    dc.DrawLine(1, 1, 1, size.GetHeight() - 1);
    dc.SetPen(wxPen(m_backColor.ChangeLightness(isDark ? 60 : 70)));
    dc.DrawLine(1, size.GetHeight() - 2, size.GetWidth() - 1, size.GetHeight() - 2);
    dc.DrawLine(size.GetWidth() - 2, 1, size.GetWidth() - 2, size.GetHeight() - 1);

    int symbolX = border + padX;
    DrawKindSymbol(dc, wxRect(symbolX, (size.GetHeight() - symbolSize) / 2, symbolSize, symbolSize));

    dc.SetFont(GetFont());
    dc.SetTextForeground(m_textColor);
    int textX = symbolX + symbolSize + gap;
    wxString text(m_text);
    /* The box is clamped to the window width, so a long message is cut down
       to what actually fits rather than running off the edge. */
    int available = size.GetWidth() - textX - padX - border;
    if (available > 0)
    {
        wxSize textSize(dc.GetTextExtent(text));
        if (textSize.GetWidth() > available)
        {
            text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, available);
            textSize = dc.GetTextExtent(text);
        }
        dc.DrawText(text, textX, (size.GetHeight() - textSize.GetHeight()) / 2);
    }
}

void QSPToast::OnEraseBackground(wxEraseEvent& WXUNUSED(event))
{
}

void QSPToast::OnTimer(wxTimerEvent& WXUNUSED(event))
{
    if (!IsShown())
    {
        m_timer.Stop();
        return;
    }
    /* The window it floats over can be moved or resized while the message is
       up, so the position is worked out again on every tick. */
    Reposition();

    m_holdLeft -= QSP_TOAST_TICK;
    if (m_holdLeft <= 0) Dismiss();
}

void QSPToast::OnMouseClick(wxMouseEvent& WXUNUSED(event))
{
    Dismiss();
}
