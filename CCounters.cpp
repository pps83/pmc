#include "CCounters.h"
#include <intrin.h>


extern "C"
{
extern SCounterDefinition CounterDefinitions[]; // List of all possible counters, in PMCTestA.cpp

extern int NumThreads; // number of threads
// performance counters used
extern int NumCounters;                         // Number of PMC counters defined Counters[]
extern int UsePMC;                              // 0 if no PMC counters used
extern int MaxNumCounters;                      // Maximum number of PMC counters
extern int CounterTypesDesired[MAXCOUNTERS];    // list of desired counter types
extern int EventRegistersUsed[MAXCOUNTERS];     // index of counter registers used
extern int Counters[MAXCOUNTERS];               // PMC register numbers
}

extern int ProcNum[];
extern double clockFactor[];
extern int diagnostics;

#define Cpuid __cpuid


CMSRInOutQue::CMSRInOutQue()
{
    n = 0;
    for (int i = 0; i < MAX_QUE_ENTRIES + 1; i++)
    {
        queue[i].msr_command = MSR_STOP;
    }
}

// Put data record in queue
int CMSRInOutQue::put(
    EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo, unsigned int value_hi)
{

    if (n >= MAX_QUE_ENTRIES)
        return -10;

    queue[n].msr_command = msr_command;
    queue[n].register_number = register_number;
    queue[n].val[0] = value_lo;
    queue[n].val[1] = value_hi;
    n++;
    return 0;
}

CCounters::CCounters()
{
    // Set everything to zero
    MVendor = VENDOR_UNKNOWN;
    MFamily = PRUNKNOWN;
    MScheme = S_UNKNOWN;
    NumPMCs = 0;
    NumFixedPMCs = 0;
    ProcessorNumber = 0;
    for (int i = 0; i < MAXCOUNTERS; i++)
        CounterNames[i] = 0;
}

void CCounters::QueueCounters()
{
    // Put counter definitions in queue
    int n = 0, CounterType;
    const char* err;
    while (CounterDefinitions[n].ProcessorFamily || CounterDefinitions[n].CounterType)
        n++;
    NumCounterDefinitions = n;

    // Get processor information
    GetProcessorVendor(); // get microprocessor vendor
    GetProcessorFamily(); // get microprocessor family
    GetPMCScheme();       // get PMC scheme

    if (UsePMC)
    {
        // Get all counter requests
        for (int i = 0; i < MaxNumCounters; i++)
        {
            CounterType = CounterTypesDesired[i];
            err = DefineCounter(CounterType);
            if (err)
            {
                printf("\nCannot make counter %i. %s\n", i + 1, err);
            }
        }

        if (MScheme == S_AMD2)
        {
            // AMD Zen processor has a core clock counter called APERF
            // which is only accessible in the driver.
            // Read TSC and APERF in the driver before and after the test.
            // This value is used for adjusting the clock count
            for (int thread = 0; thread < NumThreads; thread++)
            {
                queue1[thread].put(MSR_READ, rTSCounter, thread);
                queue1[thread].put(MSR_READ, rCoreCounter, thread);
                // queue1[thread].put(MSR_READ, rMPERF, 0);
                queue2[thread].put(MSR_READ, rTSCounter, thread);
                queue2[thread].put(MSR_READ, rCoreCounter, thread);
                // queue2[thread].put(MSR_READ, rMPERF, 0);
            }
        }
    }
}

void CCounters::LockProcessor()
{
    // Make program and driver use the same processor number if multiple processors
    // Enable RDMSR instruction
    int thread, procnum;

    // We must lock the driver call to the desired processor number
    for (thread = 0; thread < NumThreads; thread++)
    {
        procnum = ProcNum[thread];
        if (procnum >= 0)
        {
            // lock driver to the same processor number as thread
            queue1[thread].put(PROC_SET, 0, procnum);
            queue2[thread].put(PROC_SET, 0, procnum);
            // enable readpmc instruction (for this processor number)
            queue1[thread].put(PMC_ENABLE, 0, 0);
            // disable readpmc instruction after run
            queue2[thread].put(PMC_DISABLE, 0, 0); // This causes segmentation fault on AMD when thread hopping. Why is
                                                   // the processor not properly locked?
        }
    }
}

int CCounters::StartDriver()
{
    // Install and load driver
    // return error code
    int ErrNo = 0;

    if (UsePMC /*&& !diagnostics*/)
    {
        // Load driver
        ErrNo = msr.LoadDriver();
    }

    return ErrNo;
}

void CCounters::CleanUp()
{
    // Things to do after measuring

    // Calculate clock correction factors for AMD Zen
    for (int thread = 0; thread < NumThreads; thread++)
    {
        if (MScheme == S_AMD2)
        {
            long long tscount, corecount;
            tscount = read2(rTSCounter, thread) - read1(rTSCounter, thread);
            corecount = read2(rCoreCounter, thread) - read1(rCoreCounter, thread);
            clockFactor[thread] = double(corecount) / double(tscount);
        }
        else
        {
            clockFactor[thread] = 1.0;
        }
    }

    // Any required cleanup of driver etc
    // Optionally unload driver
    // msr.UnloadDriver();
    // msr.UnInstallDriver();
}

static void putImpl(CMSRInOutQue* queue, int num_threads, EMSR_COMMAND msr_command, unsigned int register_number,
    unsigned int value_lo, unsigned int value_hi)
{
    for (int t = 0; t < num_threads; t++)
        queue[t].put(msr_command, register_number, value_lo, value_hi);
}

void CCounters::Put1(int num_threads, EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo,
    unsigned int value_hi)
{
    putImpl(queue1, num_threads, msr_command, register_number, value_lo, value_hi);
}

void CCounters::Put2(int num_threads, EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo,
    unsigned int value_hi)
{
    putImpl(queue2, num_threads, msr_command, register_number, value_lo, value_hi);
}

static long long readImpl(CMSRInOutQue* queue, unsigned int register_number, int thread)
{
    for (int i = 0; i < queue[thread].GetSize(); i++)
    {
        if (queue[thread].queue[i].msr_command == MSR_READ && queue[thread].queue[i].register_number == register_number)
            return queue[thread].queue[i].value;
    }
    return 0; // not found
}

long long CCounters::read1(unsigned int register_number, int thread)
{
    return readImpl(queue1, register_number, thread);
}

long long CCounters::read2(unsigned int register_number, int thread)
{
    return readImpl(queue2, register_number, thread);
}

// Start counting
void CCounters::StartCounters(int ThreadNum)
{
    if (UsePMC)
    {
        msr.AccessRegisters(queue1[ThreadNum]);
    }
}

// Stop and reset counters
void CCounters::StopCounters(int ThreadNum)
{
    if (UsePMC)
    {
        msr.AccessRegisters(queue2[ThreadNum]);
    }
}

void CCounters::GetProcessorVendor()
{
    // get microprocessor vendor
    int CpuIdOutput[4];

    // Call cpuid function 0
    Cpuid(CpuIdOutput, 0);

    // Interpret vendor string
    MVendor = VENDOR_UNKNOWN;
    if (CpuIdOutput[2] == 0x6C65746E)
        MVendor = INTEL; // Intel "GenuineIntel"
    if (CpuIdOutput[2] == 0x444D4163)
        MVendor = AMD; // AMD   "AuthenticAMD"
    if (CpuIdOutput[1] == 0x746E6543)
        MVendor = VIA; // VIA   "CentaurHauls"
    if (diagnostics)
    {
        printf("\nVendor = %X", MVendor);
    }
}

void CCounters::GetProcessorFamily()
{
    // get microprocessor family
    int CpuIdOutput[4];
    int Family, Model;

    MFamily = PRUNKNOWN; // default = unknown

    // Call cpuid function 0
    Cpuid(CpuIdOutput, 0);
    if (CpuIdOutput[0] == 0)
        return; // cpuid function 1 not supported

    // call cpuid function 1 to get family and model number
    Cpuid(CpuIdOutput, 1);
    Family = ((CpuIdOutput[0] >> 8) & 0x0F) + ((CpuIdOutput[0] >> 20) & 0xFF); // family code
    Model = ((CpuIdOutput[0] >> 4) & 0x0F) | ((CpuIdOutput[0] >> 12) & 0xF0);  // model code
    // printf("\nCPU family 0x%X, model 0x%X\n", Family, Model);

    if (MVendor == INTEL)
    {
        // Intel processor
        if (Family < 5)
            MFamily = PRUNKNOWN; // less than Pentium
        if (Family == 5)
            MFamily = INTEL_P1MMX; // pentium 1 or mmx
        if (Family == 0x0F)
            MFamily = INTEL_P4; // pentium 4 or other netburst
        if (Family == 6)
        {
            switch (Model)
            { // list of known Intel families with different performance monitoring event tables
            case 0x09:
            case 0x0D:
                MFamily = INTEL_PM;
                break; // Pentium M
            case 0x0E:
                MFamily = INTEL_CORE;
                break; // Core 1
            case 0x0F:
            case 0x16:
                MFamily = INTEL_CORE2;
                break; // Core 2, 65 nm
            case 0x17:
            case 0x1D:
                MFamily = INTEL_CORE2;
                break; // Core 2, 45 nm
            case 0x1A:
            case 0x1E:
            case 0x1F:
            case 0x2E:
                MFamily = INTEL_7;
                break; // Nehalem
            case 0x25:
            case 0x2C:
            case 0x2F:
                MFamily = INTEL_7;
                break; // Westmere
            case 0x2A:
            case 0x2D:
                MFamily = INTEL_IVY;
                break; // Sandy Bridge
            case 0x3A:
            case 0x3E:
                MFamily = INTEL_IVY;
                break; // Ivy Bridge
            case 0x3C:
            case 0x3F:
            case 0x45:
            case 0x46:
                MFamily = INTEL_HASW;
                break; // Haswell
            case 0x3D:
            case 0x47:
            case 0x4F:
            case 0x56:
                MFamily = INTEL_HASW;
                break; // Broadwell
            case 0x5E:
            case 0x55:
                MFamily = INTEL_SKYL;
                break; // Skylake
            case 0x8C:
                MFamily = INTEL_ICE;
                break; // Ice Lake, Tiger Lake
            case 0x97:
            case 0x9A:
                MFamily = INTEL_GOLDCV;
                break; // Alder Lake, Golden Cove

            // low power processors:
            case 0x1C:
            case 0x26:
            case 0x27:
            case 0x35:
            case 0x36:
                MFamily = INTEL_ATOM;
                break; // Atom
            case 0x37:
            case 0x4A:
            case 0x4D:
                MFamily = INTEL_SILV;
                break; // Silvermont
            case 0x5C:
            case 0x5F:
            case 0x7A: // Goldmont
            case 0x86:
            case 0x9C: // Tremont
                MFamily = INTEL_GOLDM;
                break;
            case 0x57:
                MFamily = INTEL_KNIGHT;
                break; // Knights Landing
            // unknown and future
            default:
                MFamily = INTEL_P23; // Pentium 2 or 3
                if (Model >= 0x3C)
                    MFamily = INTEL_HASW; // Haswell
                if (Model >= 0x5E)
                    MFamily = INTEL_SKYL; // Skylake
                if (Model >= 0x7E)
                    MFamily = INTEL_ICE; // Ice Lake
                if (Model >= 0x97)
                    MFamily = INTEL_GOLDCV; // Golden Cove
            }
        }
    }

    if (MVendor == AMD)
    {
        // AMD processor
        MFamily = PRUNKNOWN; // old or unknown AMD
        if (Family == 6)
            MFamily = AMD_ATHLON; // AMD Athlon
        if (Family >= 0x0F && Family <= 0x14)
        {
            MFamily = AMD_ATHLON64; // Athlon 64, Opteron, etc
        }
        if (Family >= 0x15)
            MFamily = AMD_BULLD; // Family 15h
        if (Family >= 0x17)
            MFamily = AMD_ZEN; // Family 17h
    }

    if (MVendor == VIA)
    {
        // VIA processor
        if (Family == 6 && Model >= 0x0F)
            MFamily = VIA_NANO; // VIA Nano
    }
    if (diagnostics)
    {
        printf(" Family %X, Model %X, MFamily %X", Family, Model, MFamily);
    }
}

void CCounters::GetPMCScheme()
{
    // get PMC scheme
    // Default values
    MScheme = S_UNKNOWN;
    NumPMCs = 2;
    NumFixedPMCs = 0;

    if (MVendor == AMD)
    {
        // AMD processor
        MScheme = S_AMD;
        NumPMCs = 4;
        int CpuIdOutput[4];
        Cpuid(CpuIdOutput, 6); // Call cpuid function 6
        if (CpuIdOutput[2] & 1)
        {                                   // APERF AND MPERF counters present
            Cpuid(CpuIdOutput, 0x80000001); // Call cpuid function 0x80000001
            if (CpuIdOutput[2] & (1 << 28))
            {                              // L3 performance counter extensions
                MScheme = S_AMD2;          // AMD Zen scheme
                NumPMCs = 6;               // 6 counters
                rTSCounter = 0x00000010;   // PMC register number of time stamp counter in S_AMD2 scheme
                rCoreCounter = 0xC00000E8; // PMC register number of core clock counter in S_AMD2 scheme
                // rMPERF = 0xC00000E7
            }
        }
    }

    if (MVendor == VIA)
    {
        // VIA processor
        MScheme = S_VIA;
    }

    if (MVendor == INTEL)
    {
        // Intel processor
        int CpuIdOutput[4];

        // Call cpuid function 0
        Cpuid(CpuIdOutput, 0);
        if (CpuIdOutput[0] >= 0x0A)
        {
            // PMC scheme defined by cpuid function A
            Cpuid(CpuIdOutput, 0x0A);
            if (CpuIdOutput[0] & 0xFF)
            {
                MScheme = EPMCScheme(S_ID1 << ((CpuIdOutput[0] & 0xFF) - 1));
                NumPMCs = (CpuIdOutput[0] >> 8) & 0xFF;
                // NumFixedPMCs = CpuIdOutput[0] & 0x1F;
                NumFixedPMCs = CpuIdOutput[3] & 0x1F;
                // printf("\nCounters:\nMScheme = 0x%X, NumPMCs = %i, NumFixedPMCs = %i\n\n", MScheme, NumPMCs,
                // NumFixedPMCs);
            }
        }

        if (MScheme == S_UNKNOWN)
        {
            // PMC scheme not defined by cpuid
            switch (MFamily)
            {
            case INTEL_P1MMX:
                MScheme = S_P1;
                break;
            case INTEL_P23:
            case INTEL_PM:
                MScheme = S_P2;
                break;
            case INTEL_P4:
                MScheme = S_P4;
                break;
            case INTEL_CORE:
                MScheme = S_ID1;
                break;
            case INTEL_CORE2:
                MScheme = S_ID2;
                break;
            case INTEL_7:
            case INTEL_ATOM:
            case INTEL_SILV:
                MScheme = S_ID3;
                break;
            }
        }
    }
    if (diagnostics)
    {
        if (MVendor == INTEL)
            printf(", NumPMCs %X, NumFixedPMCs %X", NumPMCs, NumFixedPMCs);
        printf(", MScheme %X\n", MScheme);
    }
}

// Request a counter setup
// (return value is error message)
const char* CCounters::DefineCounter(int CounterType)
{
    if (CounterType == 0)
        return NULL;
    int i;
    SCounterDefinition* p;

    // Search for matching counter definition
    for (i = 0, p = CounterDefinitions; i < NumCounterDefinitions; i++, p++)
    {
        if (p->CounterType == CounterType && (p->PMCScheme & MScheme) && (p->ProcessorFamily & MFamily))
        {
            // Match found
            break;
        }
    }
    if (i >= NumCounterDefinitions)
    {
        // printf("\nCounterType = %X, MScheme = %X, MFamily = %X\n", CounterType, MScheme, MFamily);
        return "No matching counter definition found"; // not found in list
    }
    return DefineCounter(*p);
}

// Request a counter setup
// (return value is error message)
const char* CCounters::DefineCounter(SCounterDefinition& CDef)
{
    int i, counternr, a, b, reg, eventreg, tag;
    static int CountersEnabled = 0, FixedCountersEnabled = 0;

    if (!(CDef.ProcessorFamily & MFamily))
    {
        return "Counter not defined for present microprocessor family";
    }
    if (NumCounters >= MaxNumCounters)
        return "Too many counters";

    if (CDef.CounterFirst & 0x40000000)
    {
        // Fixed function counter
        counternr = CDef.CounterFirst;
    }
    else
    {
        // check CounterLast
        if (CDef.CounterLast < CDef.CounterFirst)
        {
            CDef.CounterLast = CDef.CounterFirst;
        }
        if (CDef.CounterLast >= NumPMCs && (MScheme & S_INTL))
        {
        }

        // Find vacant counter
        for (counternr = CDef.CounterFirst; counternr <= CDef.CounterLast; counternr++)
        {
            // Check if this counter register is already in use
            for (i = 0; i < NumCounters; i++)
            {
                if (counternr == Counters[i])
                {
                    // This counter is already in use, find another
                    goto USED;
                }
            }
            if (MFamily == INTEL_P4)
            {
                // Check if the corresponding event register ESCR is already in use
                eventreg = GetP4EventSelectRegAddress(counternr, CDef.EventSelectReg);
                for (i = 0; i < NumCounters; i++)
                {
                    if (EventRegistersUsed[i] == eventreg)
                    {
                        goto USED;
                    }
                }
            }

            // Vacant counter found. stop searching
            break;

        USED:;
            // This counter is occupied. keep searching
        }

        if (counternr > CDef.CounterLast)
        {
            // No vacant counter found
            return "Counter registers are already in use";
        }
    }

    // Vacant counter found. Save name
    CounterNames[NumCounters] = CDef.Description;

    // Put MSR commands for this counter in queues
    switch (MScheme)
    {

    case S_P1:
        // Pentium 1 and Pentium MMX
        a = CDef.Event | (CDef.EventMask << 6);
        if (counternr == 1)
            a = EventRegistersUsed[0] | (a << 16);
        Put1(NumThreads, MSR_WRITE, 0x11, a);
        Put2(NumThreads, MSR_WRITE, 0x11, 0);
        Put1(NumThreads, MSR_WRITE, 0x12 + counternr, 0);
        Put2(NumThreads, MSR_WRITE, 0x12 + counternr, 0);
        EventRegistersUsed[0] = a;
        break;

    case S_ID2:
    case S_ID3:
    case S_ID4:
    case S_ID5:
        // Intel Core 2 and later
        if (counternr & 0x40000000)
        {
            // This is a fixed function counter
            if (!(FixedCountersEnabled++))
            {
                // Enable fixed function counters
                for (a = i = 0; i < NumFixedPMCs; i++)
                {
                    b = 2; // 1=privileged level, 2=user level, 4=any thread
                    a |= b << (4 * i);
                }
                // Set MSR_PERF_FIXED_CTR_CTRL
                Put1(NumThreads, MSR_WRITE, 0x38D, a);
                Put2(NumThreads, MSR_WRITE, 0x38D, 0);
            }
            break;
        }
        if (!(CountersEnabled++))
        {
            // Enable counters
            a = (1 << NumPMCs) - 1;      // one bit for each pmc counter
            b = (1 << NumFixedPMCs) - 1; // one bit for each fixed counter
            // set MSR_PERF_GLOBAL_CTRL
            Put1(NumThreads, MSR_WRITE, 0x38F, a, b);
            Put2(NumThreads, MSR_WRITE, 0x38F, 0);
        }
        // All other counters continue in next case:

    case S_P2:
    case S_ID1:
        // Pentium Pro, Pentium II, Pentium III, Pentium M, Core 1, (Core 2 continued):

        a = CDef.Event | (CDef.EventMask << 8) | (1 << 16) | (1 << 22);
        if (MScheme == S_ID1)
            a |= (1 << 14); // Means this core only
        // if (MScheme == S_ID3) a |= (1 << 22);  // Means any thread in this core!

        eventreg = 0x186 + counternr; // IA32_PERFEVTSEL0,1,..
        reg = 0xc1 + counternr;       // IA32_PMC0,1,..
        Put1(NumThreads, MSR_WRITE, eventreg, a);
        Put2(NumThreads, MSR_WRITE, eventreg, 0);
        Put1(NumThreads, MSR_WRITE, reg, 0);
        Put2(NumThreads, MSR_WRITE, reg, 0);
        break;

    case S_P4:
        // Pentium 4 and Pentium 4 with EM64T
        // ESCR register
        eventreg = GetP4EventSelectRegAddress(counternr, CDef.EventSelectReg);
        tag = 1;
        a = 0x1C | (tag << 5) | (CDef.EventMask << 9) | (CDef.Event << 25);
        Put1(NumThreads, MSR_WRITE, eventreg, a);
        Put2(NumThreads, MSR_WRITE, eventreg, 0);
        // Remember this event register is used
        EventRegistersUsed[NumCounters] = eventreg;
        // CCCR register
        reg = counternr + 0x360;
        a = (1 << 12) | (3 << 16) | (CDef.EventSelectReg << 13);
        Put1(NumThreads, MSR_WRITE, reg, a);
        Put2(NumThreads, MSR_WRITE, reg, 0);
        // Reset counter register
        reg = counternr + 0x300;
        Put1(NumThreads, MSR_WRITE, reg, 0);
        Put2(NumThreads, MSR_WRITE, reg, 0);
        // Set high bit for fast readpmc
        counternr |= 0x80000000;
        break;

    case S_AMD:
        // AMD
        a = CDef.Event | (CDef.EventMask << 8) | (1 << 16) | (1 << 22);
        eventreg = 0xc0010000 + counternr;
        reg = 0xc0010004 + counternr;
        Put1(NumThreads, MSR_WRITE, eventreg, a);
        Put2(NumThreads, MSR_WRITE, eventreg, 0);
        Put1(NumThreads, MSR_WRITE, reg, 0);
        Put2(NumThreads, MSR_WRITE, reg, 0);
        break;

    case S_AMD2:
        // AMD Zen
        reg = 0xC0010200 + counternr * 2;
        b = CDef.Event | (CDef.EventMask << 8) | (1 << 16) | (1 << 22);
        Put1(NumThreads, MSR_WRITE, reg, b);
        Put2(NumThreads, MSR_WRITE, reg, 0);
        break;

    case S_VIA:
        // VIA Nano. Undocumented!
        a = CDef.Event | (1 << 16) | (1 << 22);
        eventreg = 0x186 + counternr;
        reg = 0xc1 + counternr;
        Put1(NumThreads, MSR_WRITE, eventreg, a);
        Put2(NumThreads, MSR_WRITE, eventreg, 0);
        Put1(NumThreads, MSR_WRITE, reg, 0);
        Put2(NumThreads, MSR_WRITE, reg, 0);
        break;

    default:
        return "No counters defined for present microprocessor family";
    }

    // Save counter register number in Counters list
    Counters[NumCounters++] = counternr;

    return NULL; // NULL = success
}

// Translate event select register number to register address for P4 processor
int CCounters::GetP4EventSelectRegAddress(int CounterNr, int EventSelectNo)
{
    // On Pentium 4 processors, the Event Select Control Registers (ESCR) are
    // identified in a very confusing way. Each ESCR has both an ESCRx-number which
    // is part of its name, an event select number to specify in the Counter
    // Configuration Control Register (CCCR), and a register address to specify
    // in the WRMSR instruction.
    // This function gets the register address based on table 15-6 in Intel manual
    // 25366815, IA-32 Intel Architecture Software Developer's Manual Volume 3:
    // System Programming Guide, 2005.
    // Returns -1 if error.
    static int TranslationTables[4][8] = {
        {0x3B2, 0x3B4, 0x3AA, 0x3B6, 0x3AC, 0x3C8, 0x3A2, 0x3A0}, // counter 0-3
        {0x3C0, 0x3C4, 0x3C2,    -1,    -1,    -1,    -1,    -1}, // counter 4-7
        {0x3A6, 0x3A4, 0x3AE, 0x3B0,    -1, 0x3A8,    -1,    -1}, // counter 8-11
        {0x3BA, 0x3CA, 0x3BC, 0x3BE, 0x3B8, 0x3CC, 0x3E0,    -1}  // counter 12-17
    };
    unsigned int n = CounterNr;
    if (n > 17)
        return -1;
    if (n > 15)
        n -= 3;
    if ((unsigned int)EventSelectNo > 7)
        return -1;

    int a = TranslationTables[n / 4][EventSelectNo];
    if (a < 0)
        return a;
    if (n & 2)
        a++;
    return a;
}
