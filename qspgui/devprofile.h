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

#ifndef DEVPROFILE_H
    #define DEVPROFILE_H

    #include <wx/wx.h>

    /* ------------------------------------------------------------------ */
    /* Player-side performance counters.

       The line profiler in the dev server measures the interpreter. It cannot
       see what the interpreter asks the *player* to do, which in this player
       is most of the cost: a refresh rebuilds four documents and hands them to
       a browser engine, and a picture or a track is read off disk.

       So the player times those itself, into a fixed table of counters. The
       table is tiny and the ids are compile-time, so a counter costs a branch
       and two adds - the hot paths here run per refresh, not per line, but a
       game that refreshes inside a loop makes them hot anyway. */
    /* ------------------------------------------------------------------ */

    namespace QSPDev
    {
        enum ProfCounter
        {
            Prof_Refresh = 0, /* one whole RefreshInt, everything below included */
            Prof_MainDesc,    /* building and pushing the main description */
            Prof_VarsDesc,
            Prof_Actions,
            Prof_Objects,
            Prof_Image,       /* loading a picture from disk */
            Prof_Script,      /* a script evaluated in a web pane */
            Prof_Sound,
            Prof_Dialog,      /* MSG / INPUT / menu: the engine waits on a human */
            Prof_SaveLoad,
            Prof_CounterCount
        };

        struct ProfCounterData
        {
            long long count;
            double totalMs;
            double maxMs;
            long long bytes; /* payload size where one is meaningful, else 0 */
        };

        /* Counters are collected only while a client is profiling. Nothing here
           allocates or locks, so this stays false for an ordinary session and
           the whole thing costs one predictable branch. */
        extern bool g_profOn;
        extern ProfCounterData g_profCounters[Prof_CounterCount];

        /* Monotonic, in milliseconds. Never jumps backwards over a clock change,
           which a profiler measuring sub-millisecond lines badly needs. */
        double NowMs();

        const wxChar *GetProfCounterName(int counter);
        void ResetProfCounters();
        void AddProfBytes(int counter, long long bytes);

        /* Resident set of the player process, in KB, or -1 where the platform
           does not say. Sampled by the live monitor, not by the counters. */
        long long GetProcessMemoryKB();

        class ProfScope
        {
        public:
            explicit ProfScope(int counter)
                : m_counter(counter), m_isOn(g_profOn), m_bytes(0),
                  m_start(m_isOn ? NowMs() : 0.0)
            {
            }

            ~ProfScope()
            {
                /* m_isOn rather than g_profOn: a profile started between the
                   two would otherwise charge this counter the time since the
                   epoch. */
                if (!m_isOn) return;
                ProfCounterData &data = g_profCounters[m_counter];
                double elapsed = NowMs() - m_start;
                ++data.count;
                data.totalMs += elapsed;
                if (elapsed > data.maxMs) data.maxMs = elapsed;
                data.bytes += m_bytes;
            }

            /* How much text this piece of work moved, added on scope exit so a
               caller can measure it while the scope is still open. */
            void AddBytes(long long bytes) { m_bytes += bytes; }

        private:
            ProfScope(const ProfScope &);
            ProfScope &operator=(const ProfScope &);

            int m_counter;
            bool m_isOn;
            long long m_bytes;
            double m_start;
        };
    }

#endif
