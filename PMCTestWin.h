//                       PMCTestWin.h                    2014-09-30 Agner Fog
//
//          Multithread PMC Test program
//          System-specific definitions for Windows
//
// See PMCTest.txt for instructions.
//
// (c) Copyright 2000-2012 by Agner Fog. GNU General Public License www.gnu.org/licences
//////////////////////////////////////////////////////////////////////////////

#include <windows.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#ifndef __CYGWIN__
#include <conio.h>
#endif

// maximum number of threads. Must be 4 or 8.
#if defined(_M_X64) || defined(__x86_64__)
#define MAXTHREADS 8
#else
#define MAXTHREADS 4
#endif

#ifdef _MSC_VER // Use intrinsics for low level functions

static inline void Serialize()
{
    // serialize CPU by cpuid function 0
    int dummy[4];
    __cpuid(dummy, 0);
    // Prevent the compiler from optimizing away the whole Serialize function:
    volatile int DontSkip = dummy[0];
}
#define Cpuid __cpuid
#define Readtsc __rdtsc
#define Readpmc __readpmc

#else // This version is for gas/AT&T syntax

static void Cpuid(int Output[4], int aa)
{
    int a, b, c, d;
    __asm("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(aa), "c"(0) :);
    Output[0] = a;
    Output[1] = b;
    Output[2] = c;
    Output[3] = d;
}

static inline void Serialize()
{
    // serialize CPU
    __asm__ __volatile__("xorl %%eax, %%eax \n cpuid " : : : "%eax", "%ebx", "%ecx", "%edx");
}

static inline int Readtsc()
{
    // read time stamp counter
    int r;
    __asm__ __volatile__("rdtsc" : "=a"(r) : : "%edx");
    return r;
}

static inline int Readpmc(int nPerfCtr)
{
    // read performance monitor counter number nPerfCtr
    int r;
    __asm__ __volatile__("rdpmc" : "=a"(r) : "c"(nPerfCtr) : "%edx");
    return r;
}
#endif

//////////////////////////////////////////////////////////////////////////////
//
//  Definitions due to different OS calls
//
//////////////////////////////////////////////////////////////////////////////

void ThreadProc1(void* parm);

namespace SyS
{ // system-specific process and thread functions

typedef DWORD_PTR ProcMaskType; // Type for processor mask
// typedef unsigned int ProcMaskType;     // If DWORD_PTR not defined

// Get mask of possible CPU cores
static inline ProcMaskType GetProcessMask()
{
    ProcMaskType ProcessAffMask = 0, SystemAffMask = 0;
    GetProcessAffinityMask(GetCurrentProcess(), &ProcessAffMask, &SystemAffMask);
    return ProcessAffMask;
}

// Set CPU to run on specified CPU core number (0-based)
static inline void SetProcessMask(int p)
{
    int r = (int)SetThreadAffinityMask(GetCurrentThread(), (ProcMaskType)1 << p);
    if (r == 0)
    {
        int e = GetLastError();
        printf("\nFailed to lock thread to processor %i. Error = %i\n", p, e);
    }
}

// Test if specified CPU core is available
static inline int TestProcessMask(int p, ProcMaskType* m)
{
    return ((ProcMaskType)1 << p) & *m;
}

// MainThreadProcNum = GetCurrentProcessorNumber(); // only available in Vista and above

// Sleep for the rest of current timeslice
static inline void Sleep0()
{
    Sleep(0);
}

// Set process (all threads) to high priority
static inline void SetProcessPriorityHigh()
{
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
}

// Set process (all threads) to normal priority
static inline void SetProcessPriorityNormal()
{
    SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
}
} // namespace SyS
