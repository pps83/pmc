#pragma once
#include "MSRDriver.h"
#include "DriverWrapper.h"
#include <windows.h>
#include <winsvc.h>
#include <stdio.h>

#define USE_DRIVERWRAPPER 1

// maximum number of performance counters used
const int MAXCOUNTERS = 8;

// max name length of counters
const int COUNTERNAMELEN = 10;

// define 64 bit integer
typedef __int64 int64;
typedef unsigned __int64 uint64;

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
    // constructor
    CMSRInOutQue();
    // put record in queue
    int put(EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo, unsigned int value_hi = 0);
    // list of entries
    SMSRInOut queue[MAX_QUE_ENTRIES + 1];
    // get size of queue
    int GetSize()
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
#if USE_DRIVERWRAPPER
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

#else
protected:
    SC_HANDLE scm;
    SC_HANDLE service;
    HANDLE hDriver;
    const char* DriverFileName;
    const char* DriverSymbolicName;
    char DriverFileNameE[MAX_PATH], DriverFilePath[MAX_PATH];

public:
    CMSRDriver()
    {
        // Define Driver filename
        if (Need64BitDriver())
        {
            DriverFileName = "MSRDriver64";
        }
        else
        {
            DriverFileName = "MSRDriver32";
        }
        // Define driver symbolic link name
        DriverSymbolicName = "\\\\.\\slMSRDriver";

        // Get the full path of the driver file name
        strcpy(DriverFileNameE, DriverFileName);
        strcat(DriverFileNameE, ".sys"); // append .sys to DriverName
        ::GetFullPathName(DriverFileNameE, MAX_PATH, DriverFilePath, NULL);

        // Initialize
        service = NULL;
        hDriver = NULL;
        scm = NULL;
    }

    ~CMSRDriver()
    {
        // Unload driver if not already unloaded and close SCM handle
        // if (hDriver) UnloadDriver();
        if (service)
        {
            ::CloseServiceHandle(service);
            service = NULL;
        }
        if (scm)
        {
            // Optionally unload driver
            // UnloadDriver();
            // Don't uninstall driver, you may need reboot before you can install it again
            // UnInstallDriver();
            ::CloseServiceHandle(scm);
            scm = NULL;
        }
    }

    const char* GetDriverName() // get name of driver
    {
        return DriverFileName;
    }

    int LoadDriver() // load MSRDriver
    {
        int r = 0, e = 0;
        // open driver service
        r = OpenDriver();
        if (r == 1060) // ERROR_SERVICE_DOES_NOT_EXIST
        {
            // Driver not installed. Install it
            e = InstallDriver();
            if (e)
                return e;
            r = OpenDriver();
        }
        if (r)
        {
            printf("\nError %i loading driver\n", r);
            return r;
        }

        // Start the service
        r = ::StartService(service, 0, NULL);
        if (r == 0)
        {
            e = ::GetLastError();
            switch (e)
            {
            case ERROR_PATH_NOT_FOUND:
                printf("\nDriver file %s path not found (please try to uninstall and reinstall)\n", DriverFileNameE);
                break;

            case ERROR_FILE_NOT_FOUND: // .sys file not found
                printf("\nDriver file %s not found\n", DriverFileNameE);
                break;

            case 577: // ERROR_INVALID_IMAGE_HASH
                // driver not signed (Vista and Windows 7)
                printf("\nThe driver %s is not signed by Microsoft\nPlease press F8 during boot and select 'Disable "
                       "Driver Signature Enforcement'\n",
                    DriverFileNameE);
                break;

            case 1056: // ERROR_SERVICE_ALREADY_RUNNING
                // Driver already loaded. Ignore
                // printf("\nDriver already loaded\n");
                e = 0;
                break;

            case 1058: // ERROR_SERVICE_DISABLED
                printf("\nError: Driver disabled\n");
                break;

            default:
                printf("\nCannot load driver %s\nError no. %i", DriverFileNameE, e);
            }
        }
        if (e == 0)
        {
            // Get handle to driver
            hDriver = ::CreateFile(DriverSymbolicName, GENERIC_READ + GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

            if (hDriver == NULL || hDriver == INVALID_HANDLE_VALUE)
            {
                hDriver = NULL;
                e = ::GetLastError();
                printf("\nCannot load driver\nError no. %i", e);
            }
        }
        return e;
    }

    int UnloadDriver() // unload MSRDriver
    {
        int r = 0, e = 0;
        if (GetScm() == NULL)
        {
            return -6;
        }

        if (hDriver)
        {
            r = ::CloseHandle(hDriver);
            hDriver = NULL;
            if (r == 0)
            {
                e = ::GetLastError();
                printf("\nCannot close driver handle\nError no. %i", e);
                return e;
            }
            printf("\nUnloading driver");
        }

        if (service)
        {
            SERVICE_STATUS ss;
            r = ::ControlService(service, SERVICE_CONTROL_STOP, &ss);
            if (r == 0)
            {
                e = ::GetLastError();
                if (e == 1062)
                {
                    printf("\nDriver not active\n");
                }
                else
                {
                    printf("\nCannot close driver\nError no. %i", e);
                }
                return e;
            }
        }
        return 0;
    }

protected:
    int InstallDriver() // install MSRDriver
    {
        int e = 0;
        if (GetScm() == NULL)
            return -1;

        // Install driver in database
        service = ::CreateService(scm, DriverFileNameE, "MSR driver", SERVICE_START + SERVICE_STOP + DELETE,
            SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE, DriverFilePath, NULL, NULL, NULL, NULL,
            NULL);

        if (service == NULL)
        {
            e = ::GetLastError();
            printf("\nCannot install driver %s\nError no. %i", DriverFileNameE, e);
        }
        else
        {
            printf("\nFirst time: Installing driver %s\n", DriverFileNameE);
        }
        return e;
    }

    int UnInstallDriver() // uninstall MSRDriver
    {
        int r = 0, e = 0;
        GetScm();
        if (service == 0)
        {
            service = ::OpenService(scm, DriverFileNameE, SERVICE_ALL_ACCESS);
        }
        if (service == 0)
        {
            e = ::GetLastError();
            if (e == 1060) // ERROR_SERVICE_DOES_NOT_EXIST
            {
                printf("\nDriver %s already uninstalled or never installed\n", DriverFileNameE);
            }
            else
            {
                printf("\nCannot open service, failed to uninstall driver %s\nError no. %i", DriverFileNameE, e);
            }
        }
        else
        {
            r = ::DeleteService(service);
            if (r == 0)
            {
                e = ::GetLastError();
                printf("\nFailed to uninstall driver %s\nError no. %i", DriverFileNameE, e);
                if (e == 1072)
                    printf("\nDriver already marked for deletion\n");
            }
            else
            {
                printf("\nUninstalling driver %s\n", DriverFileNameE);
            }
            r = ::CloseServiceHandle(service);
            if (r == 0)
            {
                e = ::GetLastError();
                printf("\nCannot close service\nError no. %i", e);
            }
            service = NULL;
        }
        return e;
    }

    SC_HANDLE GetScm() // Make scm handle
    {
        if (scm)
            return scm; // handle already made

        // Open connection to Windows Service Control Manager (SCM)
        scm = ::OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
        if (scm == NULL)
        {
            int e = ::GetLastError();
            if (e == ERROR_ACCESS_DENIED)
            {
                printf("\nAccess denied. Please run as administrator\n");
            }
            else if (e == 120)
            { // function not implemented
                printf("\nFunction not implemented on this operating system. Windows 2000 or later required.\n");
            }
            else
            {
                printf("\nCannot load Windows Service Control Manager\nError no. %i", e);
            }
        }
        return scm;
    }

    int OpenDriver() // open driver service
    {
        int e;
        // Open a service handle if not already open
        if (service == 0)
        {
            service = ::OpenService(GetScm(), DriverFileNameE, SERVICE_ALL_ACCESS);
        }
        if (service == 0)
        {
            e = ::GetLastError();

            switch (e)
            {          // Any other error than driver not installed
            case 1060: // ERROR_SERVICE_DOES_NOT_EXIST, Driver not installed. Install it
                break;
            case 6: // access denied
                printf("\nAccess denied\n");
                break;
            default: // Any other error
                printf("\nCannot open service, failed to load driver %s\nError no. %i", DriverFileNameE, e);
            }
            return e;
        }
        return 0;
    }

    typedef BOOL(WINAPI* LPFN_ISWOW64PROCESS)(HANDLE, PBOOL);

    int Need64BitDriver() // tell whether we need 32 bit or 64 bit driver
    {
        // Return value:
        // 0: running in 32 bits Windows
        // 1: running 32 bits mode in 64 bits Windows
        // 2: running 64 bits mode in 64 bits Windows
#ifdef _WIN64
        return 2;
#else
        LPFN_ISWOW64PROCESS fnIsWow64Process =
            (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle("kernel32"), "IsWow64Process");
        if (fnIsWow64Process)
        {
            BOOL bIsWow64 = FALSE;
            if (!fnIsWow64Process(GetCurrentProcess(), &bIsWow64))
            {
                return 0;
            }
            return bIsWow64;
        }
        return 0;
#endif
    }
#endif /* USE_DRIVERWRAPPER */

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
    int64 MSRRead(int r)
    {
        SMSRInOut a;
        a.msr_command = MSR_READ;
        a.register_number = r;
        a.value = 0;
        AccessRegisters(a);
        return a.val[0];
    }

    // send command to driver to write one MSR register
    int MSRWrite(int r, int64 val)
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


// class CCounters defines, starts and stops MSR counters
class CCounters
{
public:
    CCounters();                                               // constructor
    const char* DefineCounter(int CounterType);                // request a counter setup
    const char* DefineCounter(SCounterDefinition& CounterDef); // request a counter setup
    void LockProcessor();                                      // Make program and driver use the same processor number
    void QueueCounters();                                      // Put counter definitions in queue
    int StartDriver();                                         // Install and load driver
    void StartCounters(int ThreadNum);                         // start counting
    void StopCounters(int ThreadNum);                          // stop and reset counters
    void CleanUp();                                            // Any required cleanup of driver etc
    CMSRDriver msr;                                            // interface to MSR access driver
    char* CounterNames[MAXCOUNTERS];                           // name of each counter
    void Put1(int num_threads,                                 // put record into multiple start queues
        EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo, unsigned int value_hi = 0);
    void Put2(int num_threads, // put record into multiple stop queues
        EMSR_COMMAND msr_command, unsigned int register_number, unsigned int value_lo, unsigned int value_hi = 0);
    void GetProcessorVendor();                                 // get microprocessor vendor
    void GetProcessorFamily();                                 // get microprocessor family
    void GetPMCScheme();                                       // get PMC scheme
    long long read1(unsigned int register_number, int thread); // get value from previous MSR_READ command in queue1
    long long read2(unsigned int register_number, int thread); // get value from previous MSR_READ command in queue2
    // protected:
    EProcVendor MVendor; // microprocessor vendor
    EProcFamily MFamily; // microprocessor type and family
    EPMCScheme MScheme;  // PMC monitoring scheme
protected:
    CMSRInOutQue queue1[64]; // que of MSR commands to do by StartCounters()
    CMSRInOutQue queue2[64]; // que of MSR commands to do by StopCounters()
    // translate event select number to register address for P4 processor:
    static int GetP4EventSelectRegAddress(int CounterNr, int EventSelectNo);
    int NumCounterDefinitions; // number of possible counter definitions in table CounterDefinitions
    int NumPMCs;               // Number of general PMCs
    int NumFixedPMCs;          // Number of fixed function PMCs
    int ProcessorNumber;       // main thread processor number in multiprocessor systems
    unsigned int rTSCounter;   // PMC register number of time stamp counter in S_AMD2 scheme
    unsigned int rCoreCounter; // PMC register number of core clock counter in S_AMD2 scheme
};
