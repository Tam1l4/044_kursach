#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#include <wbemidl.h>
#include <ctime>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "wbemuuid.lib")

std::ofstream logFile("hardware_log.txt", std::ios::app);

std::string getCurrentTime() {
    time_t now = time(0);
    struct tm tstruct;
    char buf[80];
    localtime_s(&tstruct, &now);
    strftime(buf, sizeof(buf), "%Y-%m-%d %X", &tstruct);
    return buf;
}

double getCPUUsage() {
    static ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
    static int numProcessors;
    static bool firstRun = true;

    if (firstRun) {
        SYSTEM_INFO sysInfo;
        FILETIME ftime, fsys, fuser;
        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;
        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));
        GetSystemTimes(&fsys, &fuser, &ftime);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
        firstRun = false;
        return 0;
    }

    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;
    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));
    GetSystemTimes(&fsys, &fuser, &ftime);
    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));

    double cpuUsage = (double)((sys.QuadPart - lastSysCPU.QuadPart) + (user.QuadPart - lastUserCPU.QuadPart));
    cpuUsage /= (now.QuadPart - lastCPU.QuadPart);
    cpuUsage /= numProcessors;
    cpuUsage *= 100;

    lastCPU = now;
    lastSysCPU = sys;
    lastUserCPU = user;

    return cpuUsage;
}

double getRAMUsage() {
    MEMORYSTATUSEX mem;
    mem.dwLength = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&mem);
    return (mem.ullTotalPhys - mem.ullAvailPhys) / (1024.0 * 1024);
}

double getDiskUsage() {
    PDH_HQUERY query;
    PDH_HCOUNTER counter;
    PdhOpenQuery(NULL, 0, &query);
    PdhAddCounter(query, TEXT("\\PhysicalDisk(_Total)\\% Disk Time"), 0, &counter);
    PdhCollectQueryData(query);
    Sleep(1000);
    PDH_FMT_COUNTERVALUE value;
    PdhCollectQueryData(query);
    PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &value);
    PdhCloseQuery(query);
    return value.doubleValue;
}

double getNetworkUsage() {
    PDH_HQUERY query;
    PDH_HCOUNTER counter;
    PdhOpenQuery(NULL, 0, &query);
    PdhAddCounter(query, TEXT("\\Network Interface(*)\\Bytes Total/sec"), 0, &counter);
    PdhCollectQueryData(query);
    Sleep(1000);
    PDH_FMT_COUNTERVALUE value;
    PdhCollectQueryData(query);
    PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, NULL, &value);
    PdhCloseQuery(query);
    return value.doubleValue / (1024.0 * 1024);
}

double getCPUTemperature() {
    IWbemLocator* locator = nullptr;
    IWbemServices* services = nullptr;
    CoInitializeEx(0, COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&locator);
    locator->ConnectServer(BSTR(L"ROOT\\WMI"), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services);
    services->ExecQuery(BSTR(L"WQL"), BSTR(L"SELECT * FROM MSAcpi_ThermalZoneTemperature"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, nullptr);

    IEnumWbemClassObject* enumerator = nullptr;
    services->ExecQuery(BSTR(L"WQL"), BSTR(L"SELECT * FROM MSAcpi_ThermalZoneTemperature"), WBEM_FLAG_FORWARD_ONLY, nullptr, &enumerator);

    IWbemClassObject* object = nullptr;
    ULONG returned = 0;
    double temp = -1;

    if (enumerator) {
        enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
        VARIANT var;
        if (returned && object->Get(L"CurrentTemperature", 0, &var, nullptr, nullptr) == S_OK) {
            temp = (var.intVal - 2732) / 10.0;
            VariantClear(&var);
        }
        object->Release();
    }

    if (enumerator) enumerator->Release();
    if (services) services->Release();
    if (locator) locator->Release();
    CoUninitialize();

    return temp;
}

void logData(const std::string& time, double cpu, double temp, double ram, double disk, double net) {
    logFile << time << " | CPU: " << cpu << "% | Temp: " << temp << "°C | RAM: " << ram << " MB | Disk: "
        << disk << "% | Network: " << net << " MB/s\n";
    logFile.flush();
}

int main() {
    std::cout << "Logging system started. Press Ctrl+C to stop.\n";

    while (true) {
        std::string currentTime = getCurrentTime();
        double cpu = getCPUUsage();
        double temp = getCPUTemperature();
        double ram = getRAMUsage();
        double disk = getDiskUsage();
        double net = getNetworkUsage();

        system("cls");
        std::cout << "=== Hardware Resource Monitor ===\n";
        std::cout << "Time: " << currentTime << "\n";
        std::cout << "CPU Load: " << cpu << "%\n";
        std::cout << "CPU Temperature: " << temp << "°C\n";
        std::cout << "RAM Usage: " << ram << " MB\n";
        std::cout << "Disk Usage: " << disk << "%\n";
        std::cout << "Network Speed: " << net << " MB/s\n";

        logData(currentTime, cpu, temp, ram, disk, net);

        std::this_thread::sleep_for(std::chrono::seconds(1)); // Оновлення кожні 10 секунд
    }

    logFile.close();
    return 0;
}
