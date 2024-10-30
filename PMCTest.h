//                       PMCTest.h                2022-05-19 Agner Fog
//
//            Header file for multithreaded PMC Test program
//
// This header file is included by PMCTestA.cpp and PMCTestB.cpp.
// Please see PMCTest.txt for description of the program.
//
// This header file contains class declarations, function prototypes,
// constants and other definitions for this program.
//
// Copyright 2005-2021 by Agner Fog.
// GNU General Public License v. 3. www.gnu.org/licenses
//////////////////////////////////////////////////////////////////////////////
#pragma once

// maximum number of threads. Must be 4 or 8.
#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64)
#define MAXTHREADS 8
#else
#define MAXTHREADS 4
#endif

#include "MSRDriver.h"
#include "CCounters.h"

// maximum number of repetitions
const int MAXREPEAT = 128;

#if defined(__WINDOWS__) || defined(_WIN32) || defined(_WIN64) || defined(__CYGWIN__)
// System-specific definitions for Windows
#include "PMCTestWin.h"
#elif defined(__unix__) || defined(__linux__)
// System-specific definitions for Linux
#include "PMCTestLinux.h"
#else
#error Unknown platform
#endif


extern "C"
{

// Link to PMCTestB.cpp, PMCTestB32.asm or PMCTestB64.asm:
// The basic test loop containing the code to test
int TestLoop(int thread); // test loop
}

//////////////////////////////////////////////////////////////////////////////
//    Global variables imported from PMCTestBxx module
//////////////////////////////////////////////////////////////////////////////

extern "C"
{

extern SCounterDefinition CounterDefinitions[]; // List of all possible counters, in PMCTestA.cpp

extern int NumThreads; // number of threads
// performance counters used
extern int NumCounters;                      // Number of PMC counters defined Counters[]
extern int MaxNumCounters;                   // Maximum number of PMC counters
extern int UsePMC;                           // 0 if no PMC counters used
extern int CounterTypesDesired[MAXCOUNTERS]; // list of desired counter types
extern int EventRegistersUsed[MAXCOUNTERS];  // index of counter registers used
extern int Counters[MAXCOUNTERS];            // PMC register numbers

// count results (all threads)
extern int* PThreadData;   // Pointer to measured data for all threads
extern int ThreadDataSize; // Size of per-thread counter data block (bytes)
extern int ClockResultsOS; // offset of clock results of first thread into ThreadData (bytes)
extern int PMCResultsOS;   // offset of PMC results of first thread into ThreadData (bytes)

// optional extra output of ratio between two performance counts
extern int RatioOut[4]; // RatioOut[0] = 0: no ratio output, 1 = int, 2 = float
                        // RatioOut[1] = numerator (0 = clock, 1 = first PMC, etc., -1 = none)
                        // RatioOut[2] = denominator (0 = clock, 1 = first PMC, etc., -1 = none)
                        // RatioOut[3] = factor, int or float according to RatioOut[0]

extern int TempOut;               // Use CountTemp (possibly extended into CountOverhead) for arbitrary output
                                  // 0 = no extra output
                                  // 2 = signed 32-bit integer
                                  // 3 = signed 64-bit integer
                                  // 4 = 32-bit integer, hexadecimal
                                  // 5 = 64-bit integer, hexadecimal
                                  // 6 = float
                                  // 7 = double
extern const char* RatioOutTitle; // Column heading for optional extra output of ratio
extern const char* TempOutTitle;  // Column heading for optional arbitrary output
}
