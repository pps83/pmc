#pragma once
#include <string>

class DriverWrapper
{
public:
    DriverWrapper(const char* driverFileName, const char* driverIoFileName, const char* serviceName = nullptr);
    ~DriverWrapper();

    int open(); // return 1 if loaded or was loaded, 2 if loaded by this process
    void close();
    bool isOpen() const;

    void* io() // get file i/o HANDLE
    {
        return ioHandle;
    }

    static bool IsWow64();

private:
    void* ioHandle;
    bool initDll;

    std::string driverFileName;
    std::string driverIoFileName;
    std::string serviceName;
    std::wstring driverFilePath;

    unsigned Initialize(bool& startAdminCopyTried, bool& loadedDriver);
    unsigned GetRefCount();
    bool OpenDriver();
    bool ManageDriver(const char* DriverId, const wchar_t* DriverPath, unsigned short Function);

    DriverWrapper& operator=(const DriverWrapper&) = delete;
    DriverWrapper(const DriverWrapper&) = delete;
    DriverWrapper() = delete;

public:
    static bool adminCopyInit();
};
