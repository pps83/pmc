#pragma once
#include "MSRDriver.h"
#include "DriverWrapper.h"
#include <string>
#include <stdint.h>
#include <stdio.h>
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <windows.h>
#include <winsvc.h>

// maximum number of performance counters used
const int MAXCOUNTERS = 8;

// max name length of counters
const int COUNTERNAMELEN = 10;

#ifdef _MSC_VER // Use intrinsics for low level functions

static inline void Serialize()
{
    // serialize CPU by cpuid function 0
    int dummy[4];
    __cpuid(dummy, 0);
    // Prevent the compiler from optimizing away the whole Serialize function:
    volatile int DontSkip = dummy[0];
}
#define Readtsc __rdtsc
#define Readpmc __readpmc

#else // This version is for gas/AT&T syntax

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


// codes for processor vendor
enum EProcVendor
{
    VENDOR_UNKNOWN = 0,
    INTEL,
    AMD,
    VIA
};

// codes for processor family
enum EProcFamily
{
    PRUNKNOWN = 0,          // Unknown. cannot do performance monitoring
    PRALL = 0xFFFFFFFF,     // All processors with the specified scheme
    INTEL_P1MMX = 1,        // Intel Pentium 1 or Pentium MMX
    INTEL_P23 = 2,          // Pentium Pro, Pentium 2, Pentium 3
    INTEL_PM = 4,           // Pentium M, Core, Core 2
    INTEL_P4 = 8,           // Pentium 4 and Pentium 4 with EM64T
    INTEL_CORE = 0x10,      // Intel Core Solo/Duo
    INTEL_P23M = 0x16,      // Pentium Pro, Pentium 2, Pentium 3, Pentium M, Core1
    INTEL_CORE2 = 0x20,     // Intel Core 2
    INTEL_7 = 0x40,         // Intel Core i7, Nehalem, Sandy Bridge
    INTEL_IVY = 0x80,       // Intel Ivy Bridge
    INTEL_7I = 0xC0,        // Nehalem, Sandy Bridge, Ivy bridge
    INTEL_HASW = 0x100,     // Intel Haswell and Broadwell
    INTEL_SKYL = 0x200,     // Intel Skylake and later
    INTEL_ICE = 0x400,      // Intel Ice Lake and later
    INTEL_GOLDCV = 0x800,   // Intel Alder Lake P core and Golden Cove
    INTEL_ATOM = 0x1000,    // Intel Atom
    INTEL_SILV = 0x2000,    // Intel Silvermont
    INTEL_GOLDM = 0x4000,   // Intel Goldmont
    INTEL_KNIGHT = 0x8000,  // Intel Knights Landing (Xeon Phi 7200/5200/3200)
    AMD_ATHLON = 0x10000,   // AMD Athlon
    AMD_ATHLON64 = 0x20000, // AMD Athlon 64 or Opteron
    AMD_BULLD = 0x40000,    // AMD Family 15h (Bulldozer) and 16h?
    AMD_ZEN = 0x80000,      // AMD Family 17h (Zen)
    AMD_ALL = 0xF0000,      // AMD any processor
    VIA_NANO = 0x100000,    // VIA Nano (Centaur)
};

// codes for PMC scheme
enum EPMCScheme
{
    S_UNKNOWN = 0,   // unknown. can't do performance monitoring
    S_P1 = 0x0001,   // Intel Pentium, Pentium MMX
    S_P4 = 0x0002,   // Intel Pentium 4, Netburst
    S_P2 = 0x0010,   // Intel Pentium 2, Pentium M
    S_ID1 = 0x0020,  // Intel Core solo/duo
    S_ID2 = 0x0040,  // Intel Core 2
    S_ID3 = 0x0080,  // Intel Core i7 and later and Atom
    S_ID4 = 0x0100,  // Intel Skylake
    S_ID5 = 0x0200,  // Intel Ice Lake
    S_P2MC = 0x0030, // Intel Pentium 2, Pentium M, Core solo/duo
    S_ID23 = 0x00C0, // Intel Core 2 and later
    S_INTL = 0x0FF0, // Most Intel schemes
    S_AMD = 0x1000,  // AMD processors
    S_AMD2 = 0x2000, // AMD zen processors
    S_VIA = 0x100000 // VIA Nano processor and later
};

// record specifying how to count a particular event on a particular CPU family
struct SCounterDefinition
{
    int CounterType;                  // ID identifying what to count
    EPMCScheme PMCScheme;             // PMC scheme. values may be OR'ed
    EProcFamily ProcessorFamily;      // processor family. values may be OR'ed
    int CounterFirst, CounterLast;    // counter number or a range of possible alternative counter numbers
    int EventSelectReg;               // event select register
    int Event;                        // event code
    int EventMask;                    // event mask
    char Description[COUNTERNAMELEN]; // name of counter. length must be < COUNTERNAMELEN
};

class CMSRInOutQue
{
public:
    CMSRInOutQue();

    // put record in queue
    int put(EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo, unsigned int value_hi = 0);
    // list of entries
    SMSRInOut queue[MAX_QUE_ENTRIES + 1];
    // get size of queue
    int GetSize() const
    {
        return n;
    }

protected:
    // number of entries
    int n;
};

//////////////////////////////////////////////////////////////////////
//
//                         class CMSRDriver
//
// Thie class encapsulates the interface to the driver MSRDriver32.sys
// or MSRDriver64.sys which is needed for privileged access to set up
// the model specific registers in the CPU.
// This class loads, unloads and sends commands to MSRDriver
//
//////////////////////////////////////////////////////////////////////

class CMSRDriver
{
private:
    DriverWrapper impl;
    HANDLE hDriver;

public:
    CMSRDriver()
        : impl(DriverWrapper::IsWow64() ? "MSRDriver64.sys" : "MSRDriver32.sys", "\\\\.\\slMSRDriver")
        , hDriver(NULL)
    {
    }

    int LoadDriver()
    {
        int r = impl.open();
        if (r)
            hDriver = impl.io();
        return r ? 0 : 1;
    }

public:
    // send commands to driver to read or write MSR registers
    int AccessRegisters(SMSRInOut* pnIn, int nInLen, SMSRInOut* pnOut, int nOutLen)
    {
        if (nInLen <= 0)
            return 0;

        const int DeviceType = 0x22; // FILE_DEVICE_UNKNOWN;
        const int Function = 0x800;
        const int Method = 0;     // METHOD_BUFFERED;
        const int Access = 1 | 2; // FILE_READ_ACCESS | FILE_WRITE_ACCESS;
        const int IOCTL_MSR_DRIVER = DeviceType << 16 | Access << 14 | Function << 2 | Method;

        DWORD len = 0;

        // This call results in a call to the driver routine DispatchControl()
        int res = ::DeviceIoControl(
            hDriver, IOCTL_MSR_DRIVER, pnIn, nInLen * sizeof(*pnIn), pnOut, nOutLen * sizeof(*pnOut), &len, NULL);
        if (!res)
        {
            // Error
            int e = GetLastError();
            printf("\nCan't access driver. error %i", e);
            return e;
        }

        // Check return error codes from driver
        for (int i = 0; i < nOutLen; i++)
        {
            if (pnOut[i].msr_command == PROC_SET && pnOut[i].val[0])
            {
                printf("\nSetting processor number in driver failed, error 0x%X", pnOut[i].val[0]);
            }
        }
        return 0;
    }

    int AccessRegisters(SMSRInOut& q)
    {
        return AccessRegisters(&q, 1, &q, 1);
    }

    // send commands to driver to read or write MSR registers
    int AccessRegisters(CMSRInOutQue& q)
    {
        // Number of bytes in/out
        int n = q.GetSize();
        if (n <= 0)
            return 0;
        return AccessRegisters(q.queue, n, q.queue, n);
    }

    // read performance monitor counter
    // send command to driver to read one MSR register
    int64_t MSRRead(int r)
    {
        SMSRInOut a;
        a.msr_command = MSR_READ;
        a.register_number = r;
        a.value = 0;
        AccessRegisters(a);
        return a.val[0];
    }

    // send command to driver to write one MSR register
    int MSRWrite(int r, int64_t val)
    {
        SMSRInOut a;
        a.msr_command = MSR_WRITE;
        a.register_number = r;
        a.value = val;
        return AccessRegisters(a);
    }

    // send command to driver to read one control register, cr0 or cr4
    size_t CRRead(int r)
    {
        if (r != 0 && r != 4)
            return -11;
        SMSRInOut a;
        a.msr_command = CR_READ;
        a.register_number = r;
        a.value = 0;
        AccessRegisters(a);
        return size_t(a.value);
    }

    // send command to driver to write one control register, cr0 or cr4
    int CRWrite(int r, size_t val)
    {
        if (r != 0 && r != 4)
            return -12;
        SMSRInOut a;
        a.msr_command = CR_WRITE;
        a.register_number = r;
        a.value = val;
        return AccessRegisters(a);
    }
};

// defines, starts and stops MSR counters
class CCounters
{
public:
    CCounters();

    const char* DefineCounter(int CounterType);                // request a counter setup
    const char* DefineCounter(SCounterDefinition& CounterDef); // request a counter setup
    void LockProcessor();                                      // Make program and driver use the same processor number
    void QueueCounters();                                      // Put counter definitions in queue
    int StartDriver();                                         // Install and load driver
    void StartCounters();                                      // start counting
    void StopCounters();                                       // stop and reset counters
    void CleanUp();                                            // Any required cleanup of driver etc

    void GetProcessorVendor(); // get microprocessor vendor
    void GetProcessorFamily(); // get microprocessor family
    void GetPMCScheme();       // get PMC scheme

    // put record into multiple start queues
    void Put1(EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo, unsigned int value_hi = 0);
    // put record into multiple stop queues
    void Put2(EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo, unsigned int value_hi = 0);

    long long read1(unsigned int register_number); // get value from previous MSR_READ command in queue1
    long long read2(unsigned int register_number); // get value from previous MSR_READ command in queue2

public:
    int countersCount() const
    {
        return NumCounters;
    }

    uint64_t counterRead(int counterNum) const
    {
        return Readpmc(Counters[counterNum]);
    }

    const char* counterName(int counterNum) const
    {
        return CounterNames[counterNum];
    }

    bool usePMC() const
    {
        return UsePMC;
    }

    double getClockFactor() const
    {
        return clockFactor;
    }

    std::string getDiagnostic() const;

protected:
    int NumCounters = 0; // Number of valid PMC counters in Counters[]

    const char* CounterNames[MAXCOUNTERS] = {}; // name of each counter
    int Counters[MAXCOUNTERS] = {};             // counter register numbers used
    int EventRegistersUsed[MAXCOUNTERS] = {};   // index of counter registers used

    int Family = -1, Model = -1; // these are used for diagnostic output
    int ProcNum0 = 0;            // desired processor number
    int UsePMC = 1;              // 0 if no PMC counters used

    double clockFactor = 1.0;    // clock correction factor for AMD Zen processor

    void setDesiredCpu();

public:
    EProcVendor MVendor; // microprocessor vendor
    EProcFamily MFamily; // microprocessor type and family
    EPMCScheme MScheme;  // PMC monitoring scheme

protected:
    CMSRInOutQue queue1; // queue of MSR commands to do by StartCounters()
    CMSRInOutQue queue2; // queue of MSR commands to do by StopCounters()
    // translate event select number to register address for P4 processor:
    static int GetP4EventSelectRegAddress(int CounterNr, int EventSelectNo);
    int NumCounterDefinitions = 0; // number of possible counter definitions in table CounterDefinitions
    int NumPMCs = 0;               // Number of general PMCs
    int NumFixedPMCs = 0;          // Number of fixed function PMCs
    unsigned int rTSCounter = 0;   // PMC register number of time stamp counter in S_AMD2 scheme
    unsigned int rCoreCounter = 0; // PMC register number of core clock counter in S_AMD2 scheme

private:
    CMSRDriver msr; // interface to MSR access driver
};
