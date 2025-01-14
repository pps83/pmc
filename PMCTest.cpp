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

#include "CCounters.h"
#include <windows.h>
#include <stdlib.h>
#include <stdio.h>

// number of repetitions of test. You may change this up to MAXREPEAT
#define REPETITIONS 8

// Subtract overhead from counts (0 if not)
#define SUBTRACT_OVERHEAD 1

// Number of repetitions in loop to find overhead
#define OVERHEAD_REPETITIONS 5

// Cache line size (for preventing threads using same cache lines)
#define CACHELINESIZE 64

/*############################################################################
#
#        list of desired counter types
#
############################################################################*/
//
// Here you can select which performance monitor counters you want for your test.
// Select id numbers from the table CounterDefinitions[] in PMCTest.cpp.
// The maximum number of counters you can have is MAXCOUNTERS.
// Insert zeros if you have less than MAXCOUNTERS counters.

static const int counterTypesDesired[] = {
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
int* PCounterData = (int*)&CounterData;   // Pointer to measured data
// offset of clock results into CounterData (bytes)
int ClockResultsOS = int(CounterData.ClockResults - CounterData.CountTemp) * sizeof(int);
// offset of PMC results into CounterData (bytes)
int PMCResultsOS = int(CounterData.PMCResults - CounterData.CountTemp) * sizeof(int);

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

int TestLoop(const CCounters& MSRCounters)
{
    // this function runs the code to test REPETITIONS times
    // and reads the counters before and after each run:
    int repi; // repetition index

    for (int i = 0; i < MSRCounters.countersCount() + 1; i++)
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

        if (MSRCounters.usePMC()) // Read counters
        {
            for (int i = 0; i < MSRCounters.countersCount(); i++)
                CounterData.CountTemp[i + 1] = (int)MSRCounters.counterRead(i);
        }

        Serialize();
        CounterData.CountTemp[0] = (int)Readtsc();
        Serialize();

        // no test code here

        Serialize();
        CounterData.CountTemp[0] -= (int)Readtsc();
        Serialize();

        if (MSRCounters.usePMC()) // Read counters
        {
            for (int i = 0; i < MSRCounters.countersCount(); i++)
                CounterData.CountTemp[i + 1] -= (int)MSRCounters.counterRead(i);
        }

        Serialize();

        // find minimum counts
        for (int i = 0; i < MSRCounters.countersCount() + 1; i++)
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

        if (MSRCounters.usePMC()) // Read counters
        {
            for (int i = 0; i < MSRCounters.countersCount(); i++)
                CounterData.CountTemp[i + 1] = (int)MSRCounters.counterRead(i);
        }

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

        for (int i = 0; i < 1000; i++)
            UserData[i] *= 99;

        /*############################################################################
        #
        #        Test code end
        #
        ############################################################################*/

        Serialize();
        CounterData.CountTemp[0] -= (int)Readtsc();
        Serialize();

        if (MSRCounters.usePMC()) // Read counters
        {
            for (int i = 0; i < MSRCounters.countersCount(); i++)
                CounterData.CountTemp[i + 1] -= (int)MSRCounters.counterRead(i);
        }

        Serialize();

        // subtract overhead
        CounterData.ClockResults[repi] = -CounterData.CountTemp[0] - CounterData.CountOverhead[0];
        for (int i = 0; i < MSRCounters.countersCount(); i++)
        {
            CounterData.PMCResults[repi + i * REPETITIONS] =
                -CounterData.CountTemp[i + 1] - CounterData.CountOverhead[i + 1];
        }
    }

    return REPETITIONS;
}

int main(int argc, char* argv[])
{
    CCounters MSRCounters;

    if (!MSRCounters.init(counterTypesDesired, std::size(counterTypesDesired)))
        return 1;

    int repetitions = TestLoop(MSRCounters); // Run the test code

    MSRCounters.deinit();

    // Print results
    {
        // calculate offsets into CounterData
        int ClockOS = ClockResultsOS / sizeof(int);
        int PMCOS = PMCResultsOS / sizeof(int);

        // print column headings
        printf("\n     Clock ");
        if (MSRCounters.usePMC())
        {
            if (MSRCounters.MScheme == S_AMD2)
            {
                printf("%10s ", "Corrected");
            }
            for (int i = 0; i < MSRCounters.countersCount(); i++)
            {
                printf("%10s ", MSRCounters.counterName(i));
            }
        }

        // print counter outputs
        for (int repi = 0; repi < repetitions; repi++)
        {
            int tscClock = PCounterData[repi + ClockOS];
            printf("\n%10i ", tscClock);
            if (MSRCounters.usePMC())
            {
                if (MSRCounters.MScheme == S_AMD2)
                {
                    printf("%10i ", int(tscClock * MSRCounters.getClockFactor() + 0.5)); // Calculated core clock count
                }
                for (int i = 0; i < MSRCounters.countersCount(); i++)
                {
                    printf("%10i ", PCounterData[repi + i * repetitions + PMCOS]);
                }
            }
        }
        if (MSRCounters.MScheme == S_AMD2)
        {
            printf("\nClock factor %.4f", MSRCounters.getClockFactor());
        }
    }

    printf("\n");

    return 0;
}
