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

QUEUEHANDLE gInputQueue = nullptr;
QUEUEHANDLE gDbQueue = nullptr;
QUEUEHANDLE gErrorQueue = nullptr;

SERVICE_STATUS gStatus = {};
SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
HANDLE gStopEvent = nullptr;

void WINAPI ServiceMain(DWORD argc, LPWSTR* argv);
void WINAPI ServiceCtrlHandler(DWORD ctrlCode);
bool OpenPrivateQueue(const std::wstring& queueName, DWORD accessMode, QUEUEHANDLE* queueHandle);
void WriteLog(const std::wstring& text);
void CloseDispatcherQueues();
bool OpenDispatcherQueues();

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

    if (!OpenDispatcherQueues())
    {
        WriteLog(L"Dispatcher kann nicht starten, Queues fehlen.");

        gStatus.dwCurrentState = SERVICE_STOPPED;
        SetServiceStatus(gStatusHandle, &gStatus);
        return;
    }

    gStatus.dwCurrentState = SERVICE_RUNNING;
    SetServiceStatus(gStatusHandle, &gStatus);

    while (WaitForSingleObject(gStopEvent, 2000) == WAIT_TIMEOUT)
    {
        WriteLog(L"Dispatcher läuft...");
    }

    CloseDispatcherQueues();

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

bool OpenPrivateQueue(const std::wstring& queueName, DWORD accessMode, QUEUEHANDLE* queueHandle)
{
    std::wstring pathName =
        L".\\private$\\" + queueName;

    DWORD formatNameLength = 256;
    WCHAR formatName[256]{};

    HRESULT hr = MQPathNameToFormatName(
        pathName.c_str(),
        formatName,
        &formatNameLength);

    if (FAILED(hr))
    {
        WriteLog(L"MQPathNameToFormatName fehlgeschlagen: " + pathName);
        return false;
    }

    hr = MQOpenQueue(formatName, accessMode, MQ_DENY_NONE, queueHandle);

    if (FAILED(hr))
    {
        WriteLog(L"MQOpenQueue fehlgeschlagen: " + pathName);
        return false;
    }

    WriteLog(L"Queue geöffnet: " + pathName);

    return true;
}

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

bool OpenDispatcherQueues()
{
    bool ok = true;

    if (!OpenPrivateQueue(L"impuls_in", MQ_RECEIVE_ACCESS, &gInputQueue))
    {
        ok = false;
    }

    if (!OpenPrivateQueue(L"impuls_db",MQ_SEND_ACCESS, &gDbQueue))
    {
        ok = false;
    }

    if (!OpenPrivateQueue(L"impuls_error", MQ_SEND_ACCESS, &gErrorQueue))
    {
        ok = false;
    }

    if (ok)
    {
        WriteLog(L"Alle Dispatcher-Queues erfolgreich geöffnet.");
    }
    else
    {
        WriteLog(L"FEHLER: Nicht alle Dispatcher-Queues konnten geöffnet werden.");
    }

    return ok;
}

void CloseDispatcherQueues()
{
    if (gInputQueue)
    {
        MQCloseQueue(gInputQueue);
        gInputQueue = nullptr;
        WriteLog(L"Queue geschlossen: impuls_in");
    }

    if (gDbQueue)
    {
        MQCloseQueue(gDbQueue);
        gDbQueue = nullptr;
        WriteLog(L"Queue geschlossen: impuls_db");
    }

    if (gErrorQueue)
    {
        MQCloseQueue(gErrorQueue);
        gErrorQueue = nullptr;
        WriteLog(L"Queue geschlossen: impuls_error");
    }
}