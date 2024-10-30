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
#include <windef.h>
#include <winnt.h>
#include <winsvc.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef __CYGWIN__
#include <conio.h>
#endif

#if defined(_WIN64) && !defined(__CYGWIN__)
#include <intrin.h>
#endif

// maximum number of threads. Must be 4 or 8.
#if defined(_M_X64) || defined(__x86_64__)
#define MAXTHREADS 8
#else
#define MAXTHREADS 4
#endif

//////////////////////////////////////////////////////////////////////////////
//
//  Definitions due to different compiler syntax
//
//////////////////////////////////////////////////////////////////////////////

#ifdef __CYGWIN__
// Define macro for aligned structure, gcc syntax
#define ALIGNEDSTRUCTURE(Name, Align) struct __attribute__((aligned(Align))) Name
#else
// Define macro for aligned structure, MS Visual Studio syntax
#define ALIGNEDSTRUCTURE(Name, Align) __declspec(align(Align)) struct Name
#endif

// Define low level functions
#ifdef __INTRIN_H_ // Use intrinsics for low level functions

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

#elif defined(__CYGWIN__) // use gcc style inline assembly
// This version is for gas/AT&T syntax

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

#else // Intrinsics not supported, use inline assembly
// This version is for 32-bit, MASM syntax

static void Cpuid(int output[4], int functionnumber)
{
    __asm {
        mov eax, functionnumber;
        cpuid;
        mov esi, output;
        mov [esi],    eax;
        mov [esi+4],  ebx;
        mov [esi+8],  ecx;
        mov [esi+12], edx;
    }
}

static inline void Serialize()
{
    // serialize CPU
    __asm {
        xor eax, eax
            cpuid
    }
    // Note: ebx is changed by cpuid.
    // The compiler will save ebx automatically in most cases, but bugs have been observed.
}

#pragma warning(disable : 4035)
static inline int Readtsc()
{
    // read performance monitor counter number nPerfCtr
    __asm {
        rdtsc
    }
}

static inline int Readpmc(int nPerfCtr)
{
    // read performance monitor counter number nPerfCtr
    __asm {
        mov ecx, nPerfCtr
            rdpmc
    }
}

#pragma warning(default : 4035)

#endif // __INTRIN_H_

//////////////////////////////////////////////////////////////////////////////
//
//  Definitions due to different OS calls
//
//////////////////////////////////////////////////////////////////////////////

// Function declaration for thread procedure
#define ThreadProcedureDeclaration(Name) extern "C" DWORD WINAPI Name(LPVOID parm)

ThreadProcedureDeclaration(ThreadProc1);

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

//////////////////////////////////////////////////////////////////////
//
//        Class ThreadHandler: Create threads
//
//////////////////////////////////////////////////////////////////////

#define ThreadStackSize 0x4000 // Stack size for each thread

class ThreadHandler
{
public:
    ThreadHandler()
    {
        NumThreads = 0;
        for (int i = 0; i < MAXTHREADS; i++)
        {
            hThreads[i] = 0;
            ThreadData[i] = 0;
        }
    }

    void Start(int Num) // start threads
    {
        int t;
        if (Num > MAXTHREADS)
            Num = MAXTHREADS;
        NumThreads = Num;
        for (t = 0; t < NumThreads - 1; t++)
        {
            ThreadData[t] = t;
            // create and start thread
            hThreads[t] = CreateThread(NULL, // default security attributes
                ThreadStackSize,             // stack size
                ThreadProc1,                 // thread function name
                &ThreadData[t],              // argument to thread function
                0,                           // use default creation flags
                NULL);
            if (hThreads[t] == 0)
            {
                printf("\nFailed to create thread %i", t);
            }
        }
        // last thread = this thread
        t = NumThreads - 1;
        ThreadData[t] = t;
        ThreadProc1(&ThreadData[t]);
    }

    void Stop()
    {
        // wait for threads to finish
        if (NumThreads == 0)
            return;
        WaitForMultipleObjects(NumThreads, hThreads, TRUE, INFINITE);
        for (int t = 0; t < NumThreads; t++)
        {
            if (hThreads[t])
                CloseHandle(hThreads[t]);
        }
        NumThreads = 0;
    }

    ~ThreadHandler()
    {
        Stop();
    }

protected:
    int NumThreads;
    HANDLE hThreads[MAXTHREADS];
    int ThreadData[MAXTHREADS];
};

