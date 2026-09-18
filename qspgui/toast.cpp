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

/* How long the message stays fully opaque, and how long it then takes to
   fade away. A tick is short enough for the fade to look continuous. */
#define QSP_TOAST_HOLD 2200
#define QSP_TOAST_FADE 400
#define QSP_TOAST_TICK 30

wxIMPLEMENT_CLASS(QSPToast, wxFrame);

BEGIN_EVENT_TABLE(QSPToast, wxFrame)
    EVT_PAINT(QSPToast::OnPaint)
    EVT_ERASE_BACKGROUND(QSPToast::OnEraseBackground)
    EVT_TIMER(wxID_ANY, QSPToast::OnTimer)
    EVT_LEFT_DOWN(QSPToast::OnMouseClick)
END_EVENT_TABLE()

QSPToast::QSPToast(wxWindow *parent) :
    wxFrame(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize,
            wxFRAME_TOOL_WINDOW | wxFRAME_NO_TASKBAR | wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE),
    m_kind(QSP_TOAST_INFO),
    m_timer(this),
    m_holdLeft(0),
    m_alpha(255)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(*wxNORMAL_FONT);
    Hide();
}

void QSPToast::Pop(const wxString &text, QSPToastKind kind)
{
    m_text = text;
    m_kind = kind;
    m_holdLeft = QSP_TOAST_HOLD;
    m_alpha = 255;

    Relayout();
    Reposition();
    if (CanSetTransparent()) SetTransparent((wxByte)m_alpha);
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

void QSPToast::Relayout()
{
    wxClientDC dc(this);
    dc.SetFont(GetFont());

    int padX = FromDIP(16), padY = FromDIP(11), barWidth = FromDIP(4);
    wxSize textSize(dc.GetTextExtent(m_text));
    int width = barWidth + padX * 2 + textSize.GetWidth();
    int height = padY * 2 + textSize.GetHeight();

    /* Never wider than the window it belongs to */
    wxWindow *parent = GetParent();
    if (parent)
    {
        int maxWidth = parent->GetClientSize().GetWidth() - FromDIP(40);
        if (maxWidth > FromDIP(120) && width > maxWidth) width = maxWidth;
    }
    SetSize(width, height);
}

void QSPToast::Reposition()
{
    wxWindow *parent = GetParent();
    if (!parent) return;

    wxSize size(GetSize());
    wxSize parentSize(parent->GetClientSize());
    wxPoint pos(parent->ClientToScreen(wxPoint(
        (parentSize.GetWidth() - size.GetWidth()) / 2,
        parentSize.GetHeight() - size.GetHeight() - FromDIP(36))));
    /* Called on every tick, and moving a layered window repaints it, so a
       window that has not moved is left alone. */
    if (pos != GetPosition()) Move(pos);
}

wxColour QSPToast::GetAccentColor() const
{
    switch (m_kind)
    {
    case QSP_TOAST_SUCCESS: return wxColour(92, 190, 122);
    case QSP_TOAST_ERROR: return wxColour(226, 96, 88);
    default: return wxColour(94, 150, 238);
    }
}

void QSPToast::OnPaint(wxPaintEvent& WXUNUSED(event))
{
    wxAutoBufferedPaintDC dc(this);
    wxSize size(GetClientSize());
    int barWidth = FromDIP(4), padX = FromDIP(16);

    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(wxColour(30, 31, 34)));
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    dc.SetBrush(wxBrush(GetAccentColor()));
    dc.DrawRectangle(0, 0, barWidth, size.GetHeight());

    dc.SetPen(wxPen(wxColour(72, 74, 80)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(0, 0, size.GetWidth(), size.GetHeight());

    dc.SetFont(GetFont());
    dc.SetTextForeground(wxColour(238, 238, 240));
    int textX = barWidth + padX;
    wxString text(m_text);
    /* The box is clamped to the window width, so a long message is cut down
       to what actually fits rather than running off the edge. */
    int available = size.GetWidth() - textX - padX;
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

    if (m_holdLeft > 0)
    {
        m_holdLeft -= QSP_TOAST_TICK;
        return;
    }
    if (!CanSetTransparent())
    {
        Dismiss();
        return;
    }
    m_alpha -= 255 * QSP_TOAST_TICK / QSP_TOAST_FADE;
    if (m_alpha <= 0)
    {
        SetTransparent(255);
        Dismiss();
        return;
    }
    SetTransparent((wxByte)m_alpha);
}

void QSPToast::OnMouseClick(wxMouseEvent& WXUNUSED(event))
{
    if (CanSetTransparent()) SetTransparent(255);
    Dismiss();
}
