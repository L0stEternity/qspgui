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

#ifndef WEBIMGCANVAS_H
    #define WEBIMGCANVAS_H

    #include <wx/wx.h>
    #include "webpane.h"

    /* Drop-in replacement for QSPImgCanvas backed by the browser engine.

       The classic canvas decodes with wxImage, keeps a scaled wxBitmap cached
       against the pane size, and hands animated GIFs off to a separate
       always-on-top child window because wxWidgets cannot animate inside an
       ordinary paint handler. All of that is one <img> and one line of CSS
       here - and the same document gets WebP, AVIF and APNG for free, which is
       the whole reason the web renderer exists. A .webm/.mp4/.ogv/.mov path
       becomes a muted, looping <video> instead, so short clips work as
       animated pictures too.

       OpenFile answers whether the pane should be shown at all, so it has to
       decide before the browser has fetched anything: the file is checked on
       disk and the decode failure comes back later as a diagnostic, the same
       way a broken image in a description does. */
    class QSPWebImgCanvas : public QSPWebPane
    {
        DECLARE_CLASS(QSPWebImgCanvas)
    public:
        // C-tors / D-tor
        QSPWebImgCanvas(wxWindow *parent, wxWindowID id);

        // Methods
        bool OpenFile(const wxString& fullPath);
        void RefreshUI();
        /* Game-supplied CSS, so a game can restyle the picture pane along with
           everything else - a border, a different fit, a frame. See
           QSPWebListBox::SetUserStyles for why the scripts do not come too. */
        void SetUserStyles(const wxString& inlineCss, const wxArrayString& files);

        // Overloaded methods
        virtual bool SetBackgroundColour(const wxColour& color);

    protected:
        static wxString BuildShellDocument();

        // Overridden from QSPWebPane
        virtual void OnShellReady();

        void Apply();
        wxString BuildUserStylesScript() const;

        // Fields
        wxString m_path;
        wxColour m_backColor;
        wxString m_userCss;
        wxArrayString m_userCssFiles;
    };

#endif
