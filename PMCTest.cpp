//                       PMCTestA.cpp                2022-08-29 Agner Fog
//
//          Multithread PMC Test program for Windows and Linux
//
//
// This program is intended for testing the performance of a little piece of
// code written in C, C++ or assembly.
// The code to test is inserted at the place marked "Test code start" in
// PMCTestB.cpp, PMCTestB32.asm or PMCTestB64.asm.
//
// In 64-bit Windows: Run as administrator, with driver signature enforcement
// off.
//
// See PMCTest.txt for further instructions.
//
// To turn on counters for use in another program, run with command line option
//     startcounters
// To turn counters off again, use command line option
//     stopcounters
//
// © 2000-2022 GNU General Public License v. 3. www.gnu.org/licenses
//////////////////////////////////////////////////////////////////////////////

#include "MSRDriver.h"
#include "CCounters.h"
#include <windows.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <stdlib.h>
#include <stdio.h>

// number of repetitions of test. You may change this up to MAXREPEAT
#define REPETITIONS 8

// Use performance monitor counters. Set to 0 if not used
#define USE_PERFORMANCE_COUNTERS 1

// Subtract overhead from counts (0 if not)
#define SUBTRACT_OVERHEAD 1

// Number of repetitions in loop to find overhead
#define OVERHEAD_REPETITIONS 5

// Cache line size (for preventing threads using same cache lines)
#define CACHELINESIZE 64

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

/*############################################################################
#
#        list of desired counter types
#
############################################################################*/
//
// Here you can select which performance monitor counters you want for your test.
// Select id numbers from the table CounterDefinitions[] in PMCTestA.cpp.
// The maximum number of counters you can have is MAXCOUNTERS.
// Insert zeros if you have less than MAXCOUNTERS counters.

int CounterTypesDesired[MAXCOUNTERS] = {
    1,   // core clock cycles (Intel Core 2 and later)
    9,   // instructions (not P4)
    100, // micro-operations
    311  // data cache mises
};

struct SCounterData
{
    int CountTemp[MAXCOUNTERS + 1];            // temporary storage of clock counts and PMC counts
    int CountOverhead[MAXCOUNTERS + 1];        // temporary storage of count overhead
    int ClockResults[REPETITIONS];             // clock count results
    int PMCResults[REPETITIONS * MAXCOUNTERS]; // PMC count results
};

SCounterData CounterData;                 // Results
int NumCounters = 0;                      // Number of valid PMC counters in Counters[]
int MaxNumCounters = MAXCOUNTERS;         // Maximum number of PMC counters
int UsePMC = USE_PERFORMANCE_COUNTERS;    // 0 if no PMC counters used
int* PCounterData = (int*)&CounterData;   // Pointer to measured data
// offset of clock results into CounterData (bytes)
int ClockResultsOS = int(CounterData.ClockResults - CounterData.CountTemp) * sizeof(int);
// offset of PMC results into CounterData (bytes)
int PMCResultsOS = int(CounterData.PMCResults - CounterData.CountTemp) * sizeof(int);
// counter register numbers used
int Counters[MAXCOUNTERS] = {0};
int EventRegistersUsed[MAXCOUNTERS] = {0};

/*############################################################################
#
#        User data
#
############################################################################*/

// Put any data definitions your test code needs here:

#define ROUND_UP(A, B) ((A + B - 1) / B * B) // Round up A to nearest multiple of B

// Make sure USER_DATA_SIZE is a multiple of the cache line size, because there
// is a penalty if multiple threads access the same cache line:
#define USER_DATA_SIZE ROUND_UP(1000, CACHELINESIZE)

int UserData[USER_DATA_SIZE];

int diagnostics = 0; // 1 for output of CPU model and PMC scheme

// desired processor number
int ProcNum0 = 0;

// clock correction factor for AMD Zen processor
double clockFactor = 0;

// number of test repetitions
int repetitions;

// Create CCounters instance
CCounters MSRCounters;

int TestLoop()
{
    // this function runs the code to test REPETITIONS times
    // and reads the counters before and after each run:
    int i;    // counter index
    int repi; // repetition index

    for (i = 0; i < MAXCOUNTERS + 1; i++)
    {
        CounterData.CountOverhead[i] = 0x7FFFFFFF;
    }

    /*############################################################################
    #
    #        Initializations
    #
    ############################################################################*/

    // place any user initializations here:

    /*############################################################################
    #
    #        Initializations end
    #
    ############################################################################*/

    // first test loop.
    // Measure overhead = the test count produced by the test program itself
    for (repi = 0; repi < OVERHEAD_REPETITIONS; repi++)
    {

        Serialize();

#if USE_PERFORMANCE_COUNTERS
        // Read counters
        for (i = 0; i < MAXCOUNTERS; i++)
        {
            CounterData.CountTemp[i + 1] = (int)Readpmc(Counters[i]);
        }
#endif

        Serialize();
        CounterData.CountTemp[0] = (int)Readtsc();
        Serialize();

        // no test code here

        Serialize();
        CounterData.CountTemp[0] -= (int)Readtsc();
        Serialize();

#if USE_PERFORMANCE_COUNTERS
        // Read counters
        for (i = 0; i < MAXCOUNTERS; i++)
        {
            CounterData.CountTemp[i + 1] -= (int)Readpmc(Counters[i]);
        }
#endif
        Serialize();

        // find minimum counts
        for (i = 0; i < MAXCOUNTERS + 1; i++)
        {
            if (-CounterData.CountTemp[i] < CounterData.CountOverhead[i])
            {
                CounterData.CountOverhead[i] = -CounterData.CountTemp[i];
            }
        }
    }

    // Second test loop. Includes code to test.
    // This must be identical to first test loop, except for the test code
    for (repi = 0; repi < REPETITIONS; repi++)
    {

        Serialize();

#if USE_PERFORMANCE_COUNTERS
        // Read counters
        for (i = 0; i < MAXCOUNTERS; i++)
        {
            CounterData.CountTemp[i + 1] = (int)Readpmc(Counters[i]);
        }
#endif

        Serialize();
        CounterData.CountTemp[0] = (int)Readtsc();
        Serialize();

        /*############################################################################
        #
        #        Test code start
        #
        ############################################################################*/

        // Put the code to test here,
        // or a call to a function defined in a separate module

        for (i = 0; i < 1000; i++)
            UserData[i] *= 99;

        /*############################################################################
        #
        #        Test code end
        #
        ############################################################################*/

        Serialize();
        CounterData.CountTemp[0] -= (int)Readtsc();
        Serialize();

#if USE_PERFORMANCE_COUNTERS
        // Read counters
        for (i = 0; i < MAXCOUNTERS; i++)
        {
            CounterData.CountTemp[i + 1] -= (int)Readpmc(Counters[i]);
        }
#endif
        Serialize();

        // subtract overhead
        CounterData.ClockResults[repi] = -CounterData.CountTemp[0] - CounterData.CountOverhead[0];
        for (i = 0; i < MAXCOUNTERS; i++)
        {
            CounterData.PMCResults[repi + i * REPETITIONS] =
                -CounterData.CountTemp[i + 1] - CounterData.CountOverhead[i + 1];
        }
    }

    return REPETITIONS;
}

void TestProc()
{
    // Lock process to the desired processor number
    SyS::SetProcessMask(ProcNum0);

    // Start MSR counters
    MSRCounters.StartCounters();

    // Wait for rest of timeslice
    SyS::Sleep0();

    // Run the test code
    repetitions = TestLoop();

    // Wait for rest of timeslice
    SyS::Sleep0();

    // Start MSR counters
    MSRCounters.StopCounters();
}

int main(int argc, char* argv[])
{
    // diagnostics = 1;

    // Get mask of possible CPU cores
    SyS::ProcMaskType ProcessAffMask = SyS::GetProcessMask();

    // Fix a processor number
    int proc0 = 0;
    while (!SyS::TestProcessMask(proc0, &ProcessAffMask))
        proc0++; // check if proc0 is available

    ProcNum0 = proc0;

    if (!SyS::TestProcessMask(ProcNum0, &ProcessAffMask))
    {
        // this processor core is not available
        printf("\nProcessor %i not available. Processors available:\n", ProcNum0);
        for (int p = 0; p < MAXTHREADS; p++)
        {
            if (SyS::TestProcessMask(p, &ProcessAffMask))
                printf("%i  ", p);
        }
        printf("\n");
        return 1;
    }

    // Make program and driver use the same processor number
    MSRCounters.LockProcessor();

    // Find counter definitions and put them in queue for driver
    MSRCounters.QueueCounters();

    if (diagnostics)
        return 0; // just return CPU info, don't run test

    // Install and load driver
    int err = MSRCounters.StartDriver();
    if (err)
    {
        printf("Error: failed to load driver\n");
        return 1;
    }

    // Set high priority to minimize risk of interrupts during test
    SyS::SetProcessPriorityHigh();

    TestProc();

    // Set priority back normal
    SyS::SetProcessPriorityNormal();

    // Clean up
    MSRCounters.CleanUp();

    // Print results
    {
        // calculate offsets into CounterData
        int ClockOS = ClockResultsOS / sizeof(int);
        int PMCOS = PMCResultsOS / sizeof(int);

        // print column headings
        printf("\n     Clock ");
        if (UsePMC)
        {
            if (MSRCounters.MScheme == S_AMD2)
            {
                printf("%10s ", "Corrected");
            }
            for (int i = 0; i < NumCounters; i++)
            {
                printf("%10s ", MSRCounters.CounterNames[i]);
            }
        }

        // print counter outputs
        for (int repi = 0; repi < repetitions; repi++)
        {
            int tscClock = PCounterData[repi + ClockOS];
            printf("\n%10i ", tscClock);
            if (UsePMC)
            {
                if (MSRCounters.MScheme == S_AMD2)
                {
                    printf("%10i ", int(tscClock * clockFactor + 0.5)); // Calculated core clock count
                }
                for (int i = 0; i < NumCounters; i++)
                {
                    printf("%10i ", PCounterData[repi + i * repetitions + PMCOS]);
                }
            }
        }
        if (MSRCounters.MScheme == S_AMD2)
        {
            printf("\nClock factor %.4f", clockFactor);
        }
    }

    printf("\n");

    return 0;
}
