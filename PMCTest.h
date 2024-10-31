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

#include "MSRDriver.h"
#include "CCounters.h"
#include "PMCTestWin.h"

int TestLoop(); // The basic test loop containing the code to test

// performance counters used
extern int NumCounters;                      // Number of PMC counters defined Counters[]
extern int MaxNumCounters;                   // Maximum number of PMC counters
extern int UsePMC;                           // 0 if no PMC counters used
extern int CounterTypesDesired[MAXCOUNTERS]; // list of desired counter types
extern int EventRegistersUsed[MAXCOUNTERS];  // index of counter registers used
extern int Counters[MAXCOUNTERS];            // PMC register numbers

// count results
extern int* PCounterData;  // Pointer to measured data
extern int ClockResultsOS; // offset of clock results into CounterData (bytes)
extern int PMCResultsOS;   // offset of PMC results into CounterData (bytes)
