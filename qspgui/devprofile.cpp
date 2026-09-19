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

#include "devprofile.h"

#include <chrono>

#ifdef __WXMSW__
    #include <windows.h>
    #include <psapi.h>
#elif defined(__WXMAC__)
    #include <mach/mach.h>
#else
    #include <stdio.h>
    #include <unistd.h>
#endif

namespace QSPDev
{
    bool g_profOn = false;
    ProfCounterData g_profCounters[Prof_CounterCount];

    double NowMs()
    {
        using namespace std::chrono;
        static const steady_clock::time_point epoch = steady_clock::now();
        return duration<double, std::milli>(steady_clock::now() - epoch).count();
    }

    const wxChar *GetProfCounterName(int counter)
    {
        switch (counter)
        {
        case Prof_Refresh: return wxT("refresh");
        case Prof_MainDesc: return wxT("mainDesc");
        case Prof_VarsDesc: return wxT("varsDesc");
        case Prof_Actions: return wxT("actions");
        case Prof_Objects: return wxT("objects");
        case Prof_Image: return wxT("image");
        case Prof_Script: return wxT("script");
        case Prof_Sound: return wxT("sound");
        case Prof_Dialog: return wxT("dialog");
        case Prof_SaveLoad: return wxT("saveLoad");
        }
        return wxT("?");
    }

    void ResetProfCounters()
    {
        for (int i = 0; i < Prof_CounterCount; ++i)
        {
            g_profCounters[i].count = 0;
            g_profCounters[i].totalMs = 0.0;
            g_profCounters[i].maxMs = 0.0;
            g_profCounters[i].bytes = 0;
        }
    }

    void AddProfBytes(int counter, long long bytes)
    {
        if (!g_profOn) return;
        g_profCounters[counter].bytes += bytes;
    }

    /* Resident set, which is what a "is this game leaking" graph wants - the
       heap the player actually holds, not what it has reserved. */
    long long GetProcessMemoryKB()
    {
#ifdef __WXMSW__
        PROCESS_MEMORY_COUNTERS counters;
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            return (long long)(counters.WorkingSetSize / 1024);
        return -1;
#elif defined(__WXMAC__)
        mach_task_basic_info_data_t info;
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                      (task_info_t)&info, &count) == KERN_SUCCESS)
            return (long long)(info.resident_size / 1024);
        return -1;
#else
        /* statm is in pages, and field 2 is the resident set */
        FILE *file = fopen("/proc/self/statm", "r");
        if (!file) return -1;
        long long total = 0, resident = 0;
        int read = fscanf(file, "%lld %lld", &total, &resident);
        fclose(file);
        if (read != 2) return -1;
        return resident * (long long)(sysconf(_SC_PAGESIZE) / 1024);
#endif
    }
}
