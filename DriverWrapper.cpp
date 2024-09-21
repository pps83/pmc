#include "DriverWrapper.h"
#include <string_view>
#include <system_error>
#include <windows.h>

#define DW_DRIVER_INSTALL 1
#define DW_DRIVER_REMOVE 2
#define DW_DRIVER_SYSTEM_INSTALL 3
#define DW_DRIVER_SYSTEM_UNINSTALL 4

#define DW_DLL_NO_ERROR 0
#define DW_DLL_DRIVER_NOT_FOUND 1
#define DW_DLL_DRIVER_NOT_LOADED 2
#define DW_DLL_DRIVER_NOT_LOADED_ON_NETWORK 3

#define IOCTL_MSR_GET_REFCOUNT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

static bool g_adminCopyInitTested = DriverWrapper::adminCopyInit();

DriverWrapper::DriverWrapper(const char* driverFileName1, const char* driverIoFileName1, const char* serviceName1)
    : ioHandle(INVALID_HANDLE_VALUE)
    , initDll(false)
{
    if (!serviceName1)
        serviceName1 = driverFileName1;
    driverFileName = driverFileName1;
    driverIoFileName = driverIoFileName1;
    serviceName = serviceName1;
}

DriverWrapper::~DriverWrapper()
{
    if (isOpen())
        close();
    if (g_adminCopyInitTested) // pull g_adminCopyInitTested (ensure it's not eliminated)
        g_adminCopyInitTested = false;
}

int DriverWrapper::open()
{
    bool loaded = false;
    if (ioHandle == INVALID_HANDLE_VALUE && !initDll)
    {
        bool startAdminCopyTried = false;
        // Retry, Max 1000ms
        for (int i = 0; i < 4; i++)
        {
            DWORD dllStatus = Initialize(startAdminCopyTried, loaded);
            if (dllStatus == DW_DLL_NO_ERROR)
                break;
            Sleep(100 * i);
        }
        initDll = true;
    }
    return ioHandle != INVALID_HANDLE_VALUE ? (loaded ? 2 : 1) : 0;
}

void DriverWrapper::close()
{
    if (initDll && ioHandle != INVALID_HANDLE_VALUE)
    {
        if (GetRefCount() == 1)
        {
            CloseHandle(ioHandle);
            ioHandle = INVALID_HANDLE_VALUE;
            ManageDriver(serviceName.c_str(), driverFilePath.c_str(), DW_DRIVER_REMOVE);
        }

        if (ioHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(ioHandle);
            ioHandle = INVALID_HANDLE_VALUE;
        }
        initDll = false;
    }
}

bool DriverWrapper::isOpen() const
{
    return ioHandle != INVALID_HANDLE_VALUE;
}

bool DriverWrapper::IsWow64()
{
#ifdef _WIN64
    return true;
#else
    typedef BOOL(WINAPI * LPFN_ISWOW64PROCESS)(HANDLE hProcess, PBOOL Wow64Process);
    BOOL isWow64 = FALSE;
    LPFN_ISWOW64PROCESS isWow64Process =
        (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandleA("kernel32"), "IsWow64Process");
    if (isWow64Process != NULL)
    {
        if (!isWow64Process(GetCurrentProcess(), &isWow64))
            isWow64 = FALSE; // handle error
    }
    return isWow64 != FALSE;
#endif
}

static BOOL IsFileExist(const wchar_t* fileName)
{
    WIN32_FIND_DATAW findData;

    HANDLE hFile = FindFirstFileW(fileName, &findData);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        FindClose(hFile);
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

static BOOL IsOnNetworkDrive(const wchar_t* fileName)
{
    wchar_t root[4];
    root[0] = fileName[0];
    root[1] = ':';
    root[2] = '\\';
    root[3] = '\0';

    if (root[0] == '\\' || GetDriveTypeW(root) == DRIVE_REMOTE)
        return TRUE;
    return FALSE;
}

static BOOL InstallDriver(SC_HANDLE hSCManager, const char* DriverId, const wchar_t* DriverPath)
{
    SC_HANDLE hService = NULL;
    BOOL rCode = FALSE;
    DWORD error = NO_ERROR;

    //std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    //std::wstring id = converter.from_bytes(DriverId);
    std::string_view driverId = DriverId;
    std::wstring id(driverId.begin(), driverId.end());

    hService = CreateServiceW(hSCManager, id.c_str(), id.c_str(), SERVICE_ALL_ACCESS, SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, DriverPath, NULL, NULL, NULL, NULL, NULL);

    if (hService == NULL)
    {
        error = GetLastError();
        if (error == ERROR_SERVICE_EXISTS)
        {
            rCode = TRUE;
        }
    }
    else
    {
        rCode = TRUE;
        CloseServiceHandle(hService);
    }

    return rCode;
}

static BOOL StartDriver(SC_HANDLE hSCManager, const char* DriverId)
{
    SC_HANDLE hService = NULL;
    BOOL rCode = FALSE;
    DWORD error = NO_ERROR;
    hService = OpenServiceA(hSCManager, DriverId, SERVICE_ALL_ACCESS);
    if (hService != NULL)
    {
        if (!StartService(hService, 0, NULL))
        {
            error = GetLastError();
            if (error == ERROR_SERVICE_ALREADY_RUNNING)
                rCode = TRUE;
        }
        else
        {
            rCode = TRUE;
        }
        CloseServiceHandle(hService);
    }
    return rCode;
}

static BOOL StopDriver(SC_HANDLE hSCManager, const char* DriverId)
{
    SC_HANDLE hService = NULL;
    BOOL rCode = FALSE;
    SERVICE_STATUS serviceStatus;
    DWORD error = NO_ERROR;

    hService = OpenServiceA(hSCManager, DriverId, SERVICE_ALL_ACCESS);

    if (hService != NULL)
    {
        rCode = ControlService(hService, SERVICE_CONTROL_STOP, &serviceStatus);
        error = GetLastError();
        CloseServiceHandle(hService);
    }

    return rCode;
}

static BOOL IsSystemInstallDriver(SC_HANDLE hSCManager, const char* DriverId)
{
    SC_HANDLE hService = NULL;
    BOOL rCode = FALSE;
    DWORD dwSize;
    LPQUERY_SERVICE_CONFIG lpServiceConfig;

    hService = OpenServiceA(hSCManager, DriverId, SERVICE_ALL_ACCESS);

    if (hService != NULL)
    {
        QueryServiceConfig(hService, NULL, 0, &dwSize);
        lpServiceConfig = (LPQUERY_SERVICE_CONFIG)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, dwSize);
        QueryServiceConfig(hService, lpServiceConfig, dwSize, &dwSize);

        if (lpServiceConfig->dwStartType == SERVICE_AUTO_START)
            rCode = TRUE;

        CloseServiceHandle(hService);

        HeapFree(GetProcessHeap(), HEAP_NO_SERIALIZE, lpServiceConfig);
    }

    return rCode;
}

static BOOL RemoveDriver(SC_HANDLE hSCManager, const char* DriverId)
{
    SC_HANDLE hService = NULL;
    BOOL rCode = FALSE;

    hService = OpenServiceA(hSCManager, DriverId, SERVICE_ALL_ACCESS);
    if (hService == NULL)
    {
        rCode = TRUE;
    }
    else
    {
        rCode = DeleteService(hService);
        CloseServiceHandle(hService);
    }

    return rCode;
}

static BOOL SystemInstallDriver(SC_HANDLE hSCManager, const char* DriverId, const wchar_t* DriverPath)
{
    SC_HANDLE hService = NULL;
    BOOL rCode = FALSE;

    hService = OpenServiceA(hSCManager, DriverId, SERVICE_ALL_ACCESS);

    if (hService != NULL)
    {
        rCode = ChangeServiceConfigW(hService, SERVICE_KERNEL_DRIVER, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            DriverPath, NULL, NULL, NULL, NULL, NULL, NULL);
        CloseServiceHandle(hService);
    }

    return rCode;
}

static bool isLeftCtrlPressed()
{
    return GetAsyncKeyState(VK_LCONTROL) < 0;
}

static bool isRunningAsAdmin()
{
    BOOL isAdmin = FALSE;
    HANDLE token = NULL;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        TOKEN_ELEVATION elevation;
        DWORD cbSize = sizeof(TOKEN_ELEVATION);
        if (GetTokenInformation(token, TokenElevation, &elevation, cbSize, &cbSize))
            isAdmin = elevation.TokenIsElevated;
        CloseHandle(token);
    }
    return !!isAdmin;
}

static const wchar_t g_installCmdParam[] = L"InstallAdminDriver";

static void addCmdlineParam(std::wstring& cmdline, const wchar_t* name, const std::wstring& value)
{
    cmdline += ' ';
    cmdline += g_installCmdParam;
    cmdline += name;
    cmdline += L"=\"";
    cmdline += value;
    cmdline += '"';
}

static void getCmdlineParam(std::wstring_view cmdline, const wchar_t* name, std::wstring& value)
{
    if (!cmdline.starts_with(g_installCmdParam))
        return;
    cmdline = cmdline.substr(std::size(g_installCmdParam)-1);
    if (!cmdline.starts_with(name))
        return;
    cmdline = cmdline.substr(wcslen(name));
    if (cmdline.starts_with(L"=\""))
        value = cmdline.substr(2, cmdline.size() - 3);
    else if (cmdline.starts_with(L"="))
        value = cmdline.substr(1);
}

static bool hasInstallCmdParam(std::wstring& driverFileName, std::wstring& driverIoFileName, std::wstring& serviceName,
    std::wstring& driverFilePath)
{
    bool hasInstall = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 0; i < argc; i++)
    {
        if (wcscmp(argv[i], g_installCmdParam) == 0)
            hasInstall = true;
        getCmdlineParam(argv[i], L"FileName", driverFileName);
        getCmdlineParam(argv[i], L"IoFileName", driverIoFileName);
        getCmdlineParam(argv[i], L"ServiceName", serviceName);
        getCmdlineParam(argv[i], L"DriverFilePath", driverFilePath);
    }
    LocalFree(argv);
    return hasInstall;
}

static bool hasInstallCmdParam()
{
    bool hasInstall = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 0; i < argc; i++)
    {
        if (wcscmp(argv[i], g_installCmdParam) == 0)
        {
            hasInstall = true;
            break;
        }
    }
    LocalFree(argv);
    return hasInstall;
}

static bool startAdminCopy()
{
    int result = MessageBoxA(GetConsoleWindow(),
        "A copy of the process with Administrator access rights has to be started. Do you want to proceed?\n\n"
        "(Hold left Ctrl key to auto accept this prompt)",
        "Confirm", MB_YESNO | MB_ICONQUESTION);
    return result == IDYES;
}

static bool installAsAdmin(const std::string& driverFileName, const std::string& driverIoFileName,
    const std::string& serviceName, const std::wstring& driverFilePath)
{
    std::wstring cmdline = g_installCmdParam;
    addCmdlineParam(cmdline, L"FileName", std::wstring(driverFileName.begin(), driverFileName.end()));
    addCmdlineParam(cmdline, L"IoFileName", std::wstring(driverIoFileName.begin(), driverIoFileName.end()));
    addCmdlineParam(cmdline, L"ServiceName", std::wstring(serviceName.begin(), serviceName.end()));
    addCmdlineParam(cmdline, L"DriverFilePath", std::wstring(driverFilePath.begin(), driverFilePath.end()));

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(NULL, path, MAX_PATH);

    SHELLEXECUTEINFOW sei = {0};
    sei.cbSize = sizeof(SHELLEXECUTEINFO);
    sei.fMask = /*SEE_MASK_NOCLOSEPROCESS |*/ SEE_MASK_FLAG_NO_UI; // SEE_MASK_DEFAULT | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";                                         // Run as administrator
    sei.lpFile = path;
    sei.lpParameters = cmdline.c_str();
    sei.nShow = SW_HIDE;
    BOOL ret = ShellExecuteExW(&sei);
#ifdef _DEBUG
    if (TRUE != ret && GetLastError() != ERROR_CANCELLED)
        throw std::system_error(std::error_code(GetLastError(), std::system_category()), "Exception occurred");
#endif
    return !!ret;
}

unsigned DriverWrapper::Initialize(bool& startAdminCopyTried, bool& loadedDriver)
{
    loadedDriver = false;

    if (driverFilePath.empty())
    {
        wchar_t* ptr;
        wchar_t dir[MAX_PATH];
        wchar_t gDriverPath[MAX_PATH];

        GetModuleFileNameW(NULL, dir, MAX_PATH);

        // if (IsRunningAsAdmin())
        //     MessageBox(0, _T("IsRunningAsAdmin!!"), NULL, NULL);
        // else
        //     MessageBox(0, _T("NOT IsRunningAsAdmin!!"), NULL, NULL);

        if ((ptr = wcsrchr(dir, '\\')) != NULL)
            *ptr = '\0';
        swprintf(gDriverPath, std::size(gDriverPath), L"%s\\%S", dir, driverFileName.c_str());
        driverFilePath = gDriverPath;
    }

    if (IsFileExist(driverFilePath.c_str()) == FALSE)
        return DW_DLL_DRIVER_NOT_FOUND;

    if (IsOnNetworkDrive(driverFilePath.c_str()) == TRUE)
        return DW_DLL_DRIVER_NOT_LOADED_ON_NETWORK;

    if (1) // TODO: if (gIsNT)
    {
        if (OpenDriver())
            return DW_DLL_NO_ERROR;

        // TODO: start admin copy
        if (!startAdminCopyTried && !isRunningAsAdmin() && !hasInstallCmdParam())
        {
            startAdminCopyTried = true;
            if (isLeftCtrlPressed() || startAdminCopy())
            {
                // run adminCopyInit from admin account
                bool ret = installAsAdmin(driverFileName, driverIoFileName, serviceName, driverFilePath);
                for (int i = 0; i < (ret ? 1000 : 10); ++i)
                {
                    if (OpenDriver())
                        return DW_DLL_NO_ERROR;
                    Sleep(10);
                }
            }
        }

        ManageDriver(serviceName.c_str(), driverFilePath.c_str(), DW_DRIVER_REMOVE);
        if (!ManageDriver(serviceName.c_str(), driverFilePath.c_str(), DW_DRIVER_INSTALL))
        {
            ManageDriver(serviceName.c_str(), driverFilePath.c_str(), DW_DRIVER_REMOVE);
            return DW_DLL_DRIVER_NOT_LOADED;
        }

        if (OpenDriver())
        {
            loadedDriver = true;
            return DW_DLL_NO_ERROR;
        }
        return DW_DLL_DRIVER_NOT_LOADED;
    }
    else
    {
        ioHandle = INVALID_HANDLE_VALUE;
        return DW_DLL_NO_ERROR;
    }
}

unsigned DriverWrapper::GetRefCount()
{
    if (ioHandle == INVALID_HANDLE_VALUE)
        return 0;
    DWORD refCount = 0, length, result;
    result = DeviceIoControl(ioHandle, IOCTL_MSR_GET_REFCOUNT, NULL, 0, &refCount, sizeof(refCount), &length, NULL);
    return result ? refCount : 0;
}

bool DriverWrapper::OpenDriver()
{
    ioHandle = CreateFileA(
        driverIoFileName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return ioHandle != INVALID_HANDLE_VALUE;
}

bool DriverWrapper::ManageDriver(const char* DriverId, const wchar_t* DriverPath, unsigned short Function)
{
    SC_HANDLE hSCManager = NULL;
    BOOL rCode = FALSE;

    if (DriverId == NULL || DriverPath == NULL)
    {
        return FALSE;
    }
    hSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (hSCManager == NULL)
    {
        return FALSE;
    }

    switch (Function)
    {
    case DW_DRIVER_INSTALL:
        if (InstallDriver(hSCManager, DriverId, DriverPath))
            rCode = StartDriver(hSCManager, DriverId);
        break;
    case DW_DRIVER_REMOVE:
        if (!IsSystemInstallDriver(hSCManager, DriverId))
        {
            StopDriver(hSCManager, DriverId);
            rCode = RemoveDriver(hSCManager, DriverId);
        }
        break;
    case DW_DRIVER_SYSTEM_INSTALL:
        if (IsSystemInstallDriver(hSCManager, DriverId))
            rCode = TRUE;
        else
        {
            if (!OpenDriver())
            {
                StopDriver(hSCManager, DriverId);
                RemoveDriver(hSCManager, DriverId);
                if (InstallDriver(hSCManager, DriverId, DriverPath))
                    StartDriver(hSCManager, DriverId);
                OpenDriver();
            }
            rCode = SystemInstallDriver(hSCManager, DriverId, DriverPath);
        }
        break;
    case DW_DRIVER_SYSTEM_UNINSTALL:
        if (!IsSystemInstallDriver(hSCManager, DriverId))
            rCode = TRUE;
        else
        {
            if (ioHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(ioHandle);
                ioHandle = INVALID_HANDLE_VALUE;
            }
            if (StopDriver(hSCManager, DriverId))
                rCode = RemoveDriver(hSCManager, DriverId);
        }
        break;
    default:
        rCode = FALSE;
        break;
    }

    if (hSCManager != NULL)
        CloseServiceHandle(hSCManager);
    return rCode;
}

static std::string w2a(const std::wstring& s)
{
    std::string ret;
    ret.resize(s.size());
    for (size_t i = 0; i < s.size(); ++i)
        ret[i] = (char)s[i];
    return ret;
}

bool DriverWrapper::adminCopyInit()
{
    STARTUPINFO si;
    GetStartupInfo(&si);
    bool isGui = true; // si.dwFlags& STARTF_USESHOWWINDOW;

    std::wstring driverFileNameW;
    std::wstring driverIoFileNameW;
    std::wstring serviceNameW;
    std::wstring driverFilePath;

    //if (isRunningAsAdmin()) // debugger attach
    //    MessageBoxA(0, "IsRunningAsAdmin!!", NULL, NULL);

    if (isGui && isRunningAsAdmin() && hasInstallCmdParam(driverFileNameW, driverIoFileNameW, serviceNameW, driverFilePath))
    {
        DriverWrapper driver(w2a(driverFileNameW).c_str(), w2a(driverIoFileNameW).c_str(), w2a(serviceNameW).c_str());
        driver.driverFilePath = driverFilePath;
        int ret = driver.open();
        if (ret)
        {
            int last_ref_count = driver.GetRefCount();
            time_t countdown_start = 0;
            bool waiting_for_increase = true;
            time_t start_time = time(NULL);

            for (;;)
            {
                int current_ref_count = driver.GetRefCount();
                time_t current_time = time(NULL);

                if (waiting_for_increase && current_time - start_time >= 20 && (ret != 2 || current_ref_count <= 1))
                    break; // Counter didn't increase within 10 seconds

                if (current_ref_count > last_ref_count)
                {
                    waiting_for_increase = false;
                    countdown_start = ret == 1 ? 0 : countdown_start; // Reset countdown for ret == 1
                }
                else if (current_ref_count < last_ref_count)
                    countdown_start = current_time;

                if (countdown_start && current_time - countdown_start >= 10 && (ret != 2 || current_ref_count <= 1))
                    break; // 10 seconds have passed since counter decreased

                last_ref_count = current_ref_count;
                Sleep(10);
            }
        }
        driver.close();
        ExitProcess(0);
        return true;
    }
    return false;
}