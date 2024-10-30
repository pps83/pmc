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
// � 2000-2022 GNU General Public License v. 3. www.gnu.org/licenses
//////////////////////////////////////////////////////////////////////////////

#include "PMCTest.h"

// #include <math.h> // for warmup only

int diagnostics = 0; // 1 for output of CPU model and PMC scheme

//////////////////////////////////////////////////////////////////////
//
//        Thread synchronizer
//
//////////////////////////////////////////////////////////////////////

union USync
{
#if MAXTHREADS > 4
    int64_t allflags; // for MAXTHREADS = 8
#else
    int allflags; // for MAXTHREADS = 4
#endif
    char flag[MAXTHREADS];
};
volatile USync TSync = {0};

// processornumber for each thread
int ProcNum[MAXTHREADS + 64] = {0};

// clock correction factor for AMD Zen processor
double clockFactor[MAXTHREADS] = {0};

// number of repetitions in each thread
int repetitions;

// Create CCounters instance
CCounters MSRCounters;

//////////////////////////////////////////////////////////////////////
//
//        Thread procedure
//
//////////////////////////////////////////////////////////////////////

ThreadProcedureDeclaration(ThreadProc1)
{
    // DWORD WINAPI ThreadProc1(LPVOID parm) {
    //  check thread number
    unsigned int threadnum = *(unsigned int*)parm;

    if (threadnum >= (unsigned int)NumThreads)
    {
        printf("\nThread number out of range %i", threadnum);
        return 0;
    }

    // get desired processornumber
    int ProcessorNumber = ProcNum[threadnum];

    // Lock process to this processor number
    SyS::SetProcessMask(ProcessorNumber);

    // Start MSR counters
    MSRCounters.StartCounters(threadnum);

    // Wait for rest of timeslice
    SyS::Sleep0();

    // wait for other threads
    // Initialize synchronizer
    USync WaitTo;
    WaitTo.allflags = 0;
    for (int t = 0; t < NumThreads; t++)
        WaitTo.flag[t] = 1;
    // flag for this thead ready
    TSync.flag[threadnum] = 1;
    // wait for other threads to be ready
    while (TSync.allflags != WaitTo.allflags) // Note: will wait forever if a thread is not created
    {
    }

    // Run the test code
    repetitions = TestLoop(threadnum);

    // Wait for rest of timeslice
    SyS::Sleep0();

    // Start MSR counters
    MSRCounters.StopCounters(threadnum);

    return 0;
};

//////////////////////////////////////////////////////////////////////
//
//        Start counters and leave them on, or stop counters
//
//////////////////////////////////////////////////////////////////////
int setcounters(int argc, char* argv[])
{
    int i, cnt, thread;
    int countthreads = 0;
    int command = 0; // 1: start counters, 2: stop counters

    if (strstr(argv[1], "startcounters"))
        command = 1;
    else if (strstr(argv[1], "stopcounters"))
        command = 2;
    else
    {
        printf("\nUnknown command line parameter %s\n", argv[1]);
        return 1;
    }

    // find counter definitions on command line, if any
    if (argc > 2)
    {
        for (i = 0; i < MAXCOUNTERS; i++)
        {
            cnt = 0;
            if (command == 2)
                cnt = 100; // dummy value that is valid for all CPUs
            if (i + 2 < argc)
                cnt = atoi(argv[i + 2]);
            CounterTypesDesired[i] = cnt;
        }
    }

    // Get mask of possible CPU cores
    SyS::ProcMaskType ProcessAffMask = SyS::GetProcessMask();

    // find all possible CPU cores
    NumThreads = (int)sizeof(void*) * 8;
    if (NumThreads > 64)
        NumThreads = 64;

    for (thread = 0; thread < NumThreads; thread++)
    {
        if (SyS::TestProcessMask(thread, &ProcessAffMask))
        {
            ProcNum[thread] = thread;
            countthreads++;
        }
        else
        {
            ProcNum[thread] = -1;
        }
    }

    // Lock processor number for each thread
    MSRCounters.LockProcessor();

    // Find counter definitions and put them in queue for driver
    MSRCounters.QueueCounters();

    // Install and load driver
    int e = MSRCounters.StartDriver();
    if (e)
        return e;

    // Start MSR counters
    for (thread = 0; thread < NumThreads; thread++)
    {
        if (ProcNum[thread] >= 0)
        {

#if defined(__unix__) || defined(__linux__)
            // get desired processornumber
            int ProcessorNumber = ProcNum[thread];
            // Lock process to this processor number
            SyS::SetProcessMask(ProcessorNumber);
#else
            // In Windows, the thread number needs only be fixed inside the driver
#endif

            if (command == 1)
            {
                MSRCounters.StartCounters(thread);
            }
            else
            {
                MSRCounters.StopCounters(thread);
            }
        }
    }

    // print output
    if (command == 1)
    {
        printf("\nEnabled %i counters in each of %i CPU cores", NumCounters, countthreads);
        printf("\n\nPMC number:   Counter name:");
        for (i = 0; i < NumCounters; i++)
        {
            printf("\n0x%08X    %-10s ", Counters[i], MSRCounters.CounterNames[i]);
        }
    }
    else
    {
        printf("\nDisabled %i counters in each of %i CPU cores", NumCounters, countthreads);
    }
    printf("\n");

    // Clean up driver
    MSRCounters.CleanUp();

    return 0;
}

//////////////////////////////////////////////////////////////////////
//
//        Main
//
//////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    int repi;        // repetition counter
    int i;           // loop counter
    int t;           // thread counter
    int e;           // error number
    int procthreads; // number of threads supported by processor

    if (argc > 1)
    {
        // Interpret command line parameters
        if (strstr(argv[1], "diagnostics"))
            diagnostics = 1;
        else if (strstr(argv[1], "counters"))
        {
            // not running test. setting or resetting PMC counters
            return setcounters(argc, argv);
        }
        else
        {
            printf("\nUnknown command line parameter %s\n", argv[1]);
            return 1;
        }
    }

    // Limit number of threads
    if (NumThreads > MAXTHREADS)
    {
        NumThreads = MAXTHREADS;
        printf("\nToo many threads");
    }
    if (NumThreads < 1)
        NumThreads = 1;

    // Get mask of possible CPU cores
    SyS::ProcMaskType ProcessAffMask = SyS::GetProcessMask();
    // Count possible threads
    int maxProcThreads = (int)sizeof(void*) * 8;
    if (maxProcThreads > 64)
        maxProcThreads = 64;
    for (procthreads = i = 0; i < maxProcThreads; i++)
    {
        if (SyS::TestProcessMask(i, &ProcessAffMask))
            procthreads++;
    }

    // Fix a processornumber for each thread
    int proc0 = 0;
    while (!SyS::TestProcessMask(proc0, &ProcessAffMask))
        proc0++; // check if proc0 is available

    for (t = 0, i = NumThreads - 1; t < NumThreads; t++, i--)
    {
        // make processornumbers different, and last thread = MainThreadProcNum:
        // ProcNum[t] = MainThreadProcNum ^ i;
        if (procthreads < 4)
        {
            ProcNum[t] = i + proc0;
        }
        else
        {
            ProcNum[t] = (i % 2) * (procthreads / 2) + i / 2 + proc0;
        }
        if (!SyS::TestProcessMask(ProcNum[t], &ProcessAffMask))
        {
            // this processor core is not available
            printf("\nProcessor %i not available. Processors available:\n", ProcNum[t]);
            for (int p = 0; p < MAXTHREADS; p++)
            {
                if (SyS::TestProcessMask(p, &ProcessAffMask))
                    printf("%i  ", p);
            }
            printf("\n");
            return 1;
        }
    }

    // Make program and driver use the same processor number
    MSRCounters.LockProcessor();

    // Find counter defitions and put them in queue for driver
    MSRCounters.QueueCounters();

    if (diagnostics)
        return 0; // just return CPU info, don't run test

    // Install and load driver
    e = MSRCounters.StartDriver();

    // Set high priority to minimize risk of interrupts during test
    SyS::SetProcessPriorityHigh();

    // Make multiple threads
    ThreadHandler Threads;
    Threads.Start(NumThreads);

    // Stop threads
    Threads.Stop();

    // Set priority back normal
    SyS::SetProcessPriorityNormal();

    // Clean up
    MSRCounters.CleanUp();

    // Print results
    for (t = 0; t < NumThreads; t++)
    {
        // calculate offsets into ThreadData[]
        int TOffset = t * (ThreadDataSize / sizeof(int));
        int ClockOS = ClockResultsOS / sizeof(int);
        int PMCOS = PMCResultsOS / sizeof(int);

        // print column headings
        if (NumThreads > 1)
            printf("\nProcessor %i", ProcNum[t]);
        printf("\n     Clock ");
        if (UsePMC)
        {
            if (MSRCounters.MScheme == S_AMD2)
            {
                printf("%10s ", "Corrected");
            }
            for (i = 0; i < NumCounters; i++)
            {
                printf("%10s ", MSRCounters.CounterNames[i]);
            }
        }
        if (RatioOut[0])
            printf("%10s ", RatioOutTitle ? RatioOutTitle : "Ratio");
        if (TempOut)
            printf("%10s ", TempOutTitle ? TempOutTitle : "Extra out");

        // print counter outputs
        for (repi = 0; repi < repetitions; repi++)
        {
            int tscClock = PThreadData[repi + TOffset + ClockOS];
            printf("\n%10i ", tscClock);
            if (UsePMC)
            {
                if (MSRCounters.MScheme == S_AMD2)
                {
                    printf("%10i ", int(tscClock * clockFactor[t] + 0.5)); // Calculated core clock count
                }
                for (i = 0; i < NumCounters; i++)
                {
                    printf("%10i ", PThreadData[repi + i * repetitions + TOffset + PMCOS]);
                }
            }
            // optional ratio output
            if (RatioOut[0])
            {
                union
                {
                    int i;
                    float f;
                } factor;
                factor.i = RatioOut[3];
                int a, b;
                if (RatioOut[1] == 0)
                {
                    a = PThreadData[repi + TOffset + ClockOS];
                    if (MSRCounters.MScheme == S_AMD2)
                        a = int(a * clockFactor[t] + 0.5); // Calculated core clock count
                }
                else if ((unsigned int)RatioOut[1] <= (unsigned int)NumCounters)
                {
                    a = PThreadData[repi + (RatioOut[1] - 1) * repetitions + TOffset + PMCOS];
                }
                else
                {
                    a = 1;
                }
                if (RatioOut[2] == 0)
                {
                    b = PThreadData[repi + TOffset + ClockOS];
                    if (MSRCounters.MScheme == S_AMD2)
                        b = int(b * clockFactor[t] + 0.5); // Calculated core clock count
                }
                else if ((unsigned int)RatioOut[2] <= (unsigned int)NumCounters)
                {
                    b = PThreadData[repi + (RatioOut[2] - 1) * repetitions + TOffset + PMCOS];
                }
                else
                {
                    b = 1;
                }
                if (b == 0)
                {
                    printf("%10s", "inf");
                }
                else if (RatioOut[0] == 1)
                {
                    printf("%10i ", factor.i * a / b);
                }
                else
                {
                    printf("%10.6f ", factor.f * (double)a / (double)b);
                }
            }
            // optional arbitrary output
            if (TempOut)
            {
                union
                {
                    int* pi;
                    int64_t* pl;
                    float* pf;
                    double* pd;
                } pu;
                pu.pi = PThreadData + repi + TOffset; // pointer to CountTemp
                if (TempOut & 1)
                    pu.pi += repi; // double size
                switch (TempOut)
                {
                case 2: // int
                    printf("%10i", *pu.pi);
                    break;
                case 3: // 64 bit int
                    printf("%10lli", *pu.pl);
                    break;
                case 4: // hexadecimal int
                    printf("0x%08X", *pu.pi);
                    break;
                case 5: // hexadecimal 64-bit int
                    printf("0x%08X%08X", pu.pi[1], pu.pi[0]);
                    break;
                case 6: // float
                    printf("%10.6f", *pu.pf);
                    break;
                case 7: // double
                    printf("%10.6f", *pu.pd);
                    break;
                case 8: // float, corrected for clock factor
                    printf("%10.6f", *pu.pf / clockFactor[t]);
                    break;
                default:
                    printf("unknown TempOut %i", TempOut);
                }
            }
        }
        if (MSRCounters.MScheme == S_AMD2)
        {
            printf("\nClock factor %.4f", clockFactor[t]);
        }
    }

    printf("\n");
    // Optional: wait for key press
    // printf("\npress any key");
    // getch();

    // Exit
    return 0;
}
