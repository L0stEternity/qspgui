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

/* Keeping the docks at the same share of the window. wxAUI writes their sizes
   into the perspective string in pixels, so this is a string rewrite - and a
   string rewrite is exactly the kind of thing that should not be verified by
   dragging the window about and looking at it. */

#include "testing.h"
#include "../qspgui/comtools.h"

namespace
{
    /* The shape wxAuiManager::SavePerspective() produces, cut down to the
       fields the rewrite reads */
    wxString Perspective(int right, int bottom, int input)
    {
        return wxString::Format(
            wxT("layout3|")
            wxT("name=desc;caption=;state=768;dir=5;layer=0;row=0;pos=0;prop=100000|")
            wxT("name=objs;caption=Objects;state=6293500;dir=2;layer=0;row=0;pos=0;prop=100000|")
            wxT("name=acts;caption=Actions;state=6293500;dir=3;layer=0;row=0;pos=0;prop=100000|")
            wxT("name=input;caption=Input area;state=2099196;dir=3;layer=1;row=0;pos=0;prop=100000|")
            wxT("dock_size(5,0,0)=22|dock_size(2,0,0)=%d|dock_size(3,0,0)=%d|dock_size(3,1,0)=%d|"),
            right, bottom, input);
    }

    wxArrayString FixedPanes()
    {
        wxArrayString panes;
        panes.Add(wxT("input"));
        return panes;
    }

    /* The value of one dock_size entry, or -1 when it isn't there */
    int DockSize(const wxString &perspective, const wxString &key)
    {
        wxString needle(wxT("dock_size(") + key + wxT(")="));
        int at = perspective.Find(needle);
        if (at == wxNOT_FOUND) return -1;

        long size;
        wxString value(perspective.Mid(at + needle.Length()).BeforeFirst(wxT('|')));
        return (value.ToLong(&size) ? (int)size : -1);
    }
}

QSP_TEST(DockLayoutKeepsTheShareOfTheWindow)
{
    QSPDockLayout layout;
    wxString grown(layout.Rescale(Perspective(200, 150, 24),
                                  wxSize(800, 600), wxSize(1600, 1200), FixedPanes()));

    /* A quarter of the width and a quarter of the height, before and after */
    QSP_CHECK_INT(DockSize(grown, wxT("2,0,0")), 400);
    QSP_CHECK_INT(DockSize(grown, wxT("3,0,0")), 300);
}

QSP_TEST(DockLayoutLeavesTheInputRowAlone)
{
    QSPDockLayout layout;
    wxString grown(layout.Rescale(Perspective(200, 150, 24),
                                  wxSize(800, 600), wxSize(1600, 1200), FixedPanes()));

    /* One line of text does not get taller because the window did */
    QSP_CHECK_INT(DockSize(grown, wxT("3,1,0")), 24);
    /* Nor does the centre pane have a size of its own to scale */
    QSP_CHECK_INT(DockSize(grown, wxT("5,0,0")), 22);
}

QSP_TEST(DockLayoutSurvivesARoundTrip)
{
    QSPDockLayout layout;
    /* "small" is a macro in the Windows headers */
    wxSize smallWindow(800, 600), largeWindow(1500, 1100);

    /* Rounding to whole pixels on every step of a slow drag is what would
       otherwise walk the layout away from where it started */
    wxString current(Perspective(201, 149, 24));
    for (int i = 0; i < 20; ++i)
    {
        current = layout.Rescale(current, smallWindow, largeWindow, FixedPanes());
        current = layout.Rescale(current, largeWindow, smallWindow, FixedPanes());
    }
    QSP_CHECK_INT(DockSize(current, wxT("2,0,0")), 201);
    QSP_CHECK_INT(DockSize(current, wxT("3,0,0")), 149);
}

QSP_TEST(DockLayoutFollowsASashTheUserDragged)
{
    QSPDockLayout layout;
    wxSize smallWindow(800, 600), largeWindow(1600, 1200);

    wxString grown(layout.Rescale(Perspective(200, 150, 24), smallWindow, largeWindow, FixedPanes()));
    QSP_CHECK_INT(DockSize(grown, wxT("2,0,0")), 400);

    /* The dock is not the size we left it at, so the user moved the sash and
       theirs is the share to keep from here on: half the window, not a quarter */
    wxString dragged(Perspective(800, 150, 24));
    wxString shrunk(layout.Rescale(dragged, largeWindow, smallWindow, FixedPanes()));
    QSP_CHECK_INT(DockSize(shrunk, wxT("2,0,0")), 400);
}

QSP_TEST(DockLayoutHandsBackWhatItCannotUse)
{
    QSPDockLayout layout;
    wxString perspective(Perspective(200, 150, 24));

    /* A window with no size yet, and a layout string with nothing to scale */
    QSP_CHECK_STR(layout.Rescale(perspective, wxSize(0, 600), wxSize(1600, 1200), FixedPanes()),
                  perspective);
    QSP_CHECK_STR(layout.Rescale(wxT("layout3|"), wxSize(800, 600), wxSize(1600, 1200), FixedPanes()),
                  wxT("layout3|"));
    /* The same size on both sides leaves every dock where it is */
    QSP_CHECK_STR(layout.Rescale(perspective, wxSize(800, 600), wxSize(800, 600), FixedPanes()),
                  perspective);
}

QSP_TEST(DockLayoutKeepsEverythingElseInTheString)
{
    QSPDockLayout layout;
    wxString grown(layout.Rescale(Perspective(200, 150, 24),
                                  wxSize(800, 600), wxSize(1600, 1200), FixedPanes()));

    /* The panes' own entries are passed through untouched - a rewrite that
       dropped one would take the pane's caption or state with it */
    QSP_CHECK(grown.Contains(wxT("name=objs;caption=Objects;state=6293500;dir=2;layer=0;row=0;pos=0;prop=100000")));
    QSP_CHECK(grown.Contains(wxT("name=input;caption=Input area;")));
    QSP_CHECK(grown.StartsWith(wxT("layout3|")));
    QSP_CHECK(grown.EndsWith(wxT("|")));
}
