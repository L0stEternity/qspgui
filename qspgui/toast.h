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

#ifndef TOAST_H
    #define TOAST_H

    #include <wx/wx.h>

    enum QSPToastKind
    {
        QSP_TOAST_INFO,
        QSP_TOAST_SUCCESS,
        QSP_TOAST_ERROR
    };

    /* A short-lived message shown over the player, used where a modal dialog
       would interrupt play for something the user does not have to answer.

       It is drawn the way the rest of the player is: a panel in the theme's
       own colours, with a chiselled border and a hand-drawn symbol, sitting in
       the bottom right corner of the window until it has been read. No rounded
       corners, no fading - this is a status message, not a notification.

       It is a floating frame rather than a child window on purpose: the
       description panes can be a real browser, whose own window would paint
       over any sibling we put on top of it. */
    class QSPToast : public wxFrame
    {
        DECLARE_CLASS(QSPToast)
        DECLARE_EVENT_TABLE()
    public:
        // C-tors / D-tor
        QSPToast(wxWindow *parent);

        // Methods
        /* Shows the message, replacing whatever is on screen: toasts are
           status reports, and only the latest one is worth reading. */
        void Pop(const wxString &text, QSPToastKind kind = QSP_TOAST_INFO);
        void Dismiss();
        /* The player's own palette, so a message belongs to the window it
           appears over whichever theme is in use. The desktop's tooltip
           colours stand in until the settings have been read. */
        void SetColors(const wxColour &back, const wxColour &text);

    protected:
        // Internal methods
        void Relayout();
        void Reposition();
        /* The symbol's colour, dark on a light panel and light on a dark one */
        wxColour GetKindColor() const;
        /* The check mark, the "i" and the "!", drawn rather than typed: the
           user's font is not guaranteed to have any of them. */
        void DrawKindSymbol(wxDC &dc, const wxRect &rect) const;

        // Events
        void OnPaint(wxPaintEvent& event);
        void OnEraseBackground(wxEraseEvent& event);
        void OnTimer(wxTimerEvent& event);
        void OnMouseClick(wxMouseEvent& event);

        // Fields
        wxString m_text;
        QSPToastKind m_kind;
        wxTimer m_timer;
        int m_holdLeft; /* msecs left before the toast goes away */
        wxColour m_backColor;
        wxColour m_textColor;
    };

#endif
