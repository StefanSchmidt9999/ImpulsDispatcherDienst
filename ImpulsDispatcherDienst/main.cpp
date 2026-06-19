#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <mq.h>

#include <string>
#include <fstream>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Mqrt.lib")

#define SERVICE_NAME L"ImpulsDispatcher"

SERVICE_STATUS gStatus = {};
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
HANDLE gStopEvent = nullptr;

void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD ctrlCode);

void WriteLog(const std::wstring& text)
{
    std::wofstream file(L"C:\\Temp\\ImpulsDispatcher.log", std::ios::app);

    if (file.is_open())
    {
        SYSTEMTIME st;
        GetLocalTime(&st);

        file << L"["
            << st.wDay << L"." << st.wMonth << L"." << st.wYear
            << L" "
            << st.wHour << L":" << st.wMinute << L":" << st.wSecond
            << L"] "
            << text
            << std::endl;
    }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    SERVICE_TABLE_ENTRY table[] =
    {
        { (LPWSTR)SERVICE_NAME, ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcher(table))
    {
        MessageBox(nullptr,
            L"Läuft NICHT als Service. Debugmodus.",
            SERVICE_NAME,
            MB_OK);

        ServiceMain(0, nullptr);
    }

    return 0;
}

void WINAPI ServiceMain(DWORD, LPWSTR*)
{
    gStatusHandle = RegisterServiceCtrlHandler(
        SERVICE_NAME,
        ServiceCtrlHandler);

    gStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gStatus.dwCurrentState = SERVICE_START_PENDING;
    gStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    SetServiceStatus(gStatusHandle, &gStatus);

    gStopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

    WriteLog(L"ImpulsDispatcher gestartet.");

    gStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(gStatusHandle, &gStatus);

    while (WaitForSingleObject(gStopEvent, 2000) == WAIT_TIMEOUT)
    {
        WriteLog(L"Dispatcher läuft...");
    }

    WriteLog(L"ImpulsDispatcher wird beendet.");

    gStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(gStatusHandle, &gStatus);
}

void WINAPI ServiceCtrlHandler(DWORD ctrlCode)
{
    if (ctrlCode == SERVICE_CONTROL_STOP)
    {
        gStatus.dwCurrentState = SERVICE_STOP_PENDING;
        SetServiceStatus(gStatusHandle, &gStatus);

        SetEvent(gStopEvent);
    }
}