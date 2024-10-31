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

#include "PMCTest.h"

int diagnostics = 0; // 1 for output of CPU model and PMC scheme

// desired processor number
int ProcNum0 = 0;

// clock correction factor for AMD Zen processor
double clockFactor = 0;

// number of test repetitions
int repetitions;

// Create CCounters instance
CCounters MSRCounters;

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

    // Find counter defitions and put them in queue for driver
    MSRCounters.QueueCounters();

    if (diagnostics)
        return 0; // just return CPU info, don't run test

    // Install and load driver
    MSRCounters.StartDriver();

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
