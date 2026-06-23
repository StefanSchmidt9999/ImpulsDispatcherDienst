#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objbase.h>
#include <propidl.h>
#include <mq.h>
#include <vector>

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

bool ReceiveMessageFromInput(std::wstring& xmlText);
std::wstring ExtractTagValue(const std::wstring& xml, const std::wstring& tagName);
void SendMessageToQueue(QUEUEHANDLE queueHandle, const std::wstring& xmlText, const std::wstring& label);

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

   /* while (WaitForSingleObject(gStopEvent, 2000) == WAIT_TIMEOUT)
    {
        WriteLog(L"Dispatcher läuft...");
    }*/

    while (WaitForSingleObject(gStopEvent, 1000) == WAIT_TIMEOUT)
    {
        std::wstring xmlText;

        if (ReceiveMessageFromInput(xmlText))
        {
            WriteLog(L"Nachricht aus impuls_in empfangen.");

            std::wstring commandId = ExtractTagValue(xmlText, L"CommandId");

            if (commandId.empty())
            {
                WriteLog(L"CommandId fehlt. Nachricht geht nach impuls_error.");
                SendMessageToQueue(gErrorQueue, xmlText, L"ERROR_NO_COMMANDID");
            }
            else if (commandId[0] == L'9')
            {
                WriteLog(L"CommandId " + commandId + L" erkannt. Nachricht geht nach impuls_db.");
                SendMessageToQueue(gDbQueue, xmlText, L"DB_" + commandId);
            }
            else
            {
                WriteLog(L"CommandId " + commandId + L" unbekannter Bereich. Nachricht geht nach impuls_error.");
                SendMessageToQueue(gErrorQueue, xmlText, L"ERROR_UNKNOWN_COMMANDID");
            }
        }
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

bool ReceiveMessageFromInput(std::wstring& xmlText)
{
    const DWORD BODY_BUFFER_SIZE = 64 * 1024;

    std::vector<UCHAR> bodyBuffer(BODY_BUFFER_SIZE);

    MQMSGPROPS msgProps{};
    MSGPROPID propId[1]{};
    MQPROPVARIANT propVar[1]{};
    HRESULT status[1]{};

    propId[0] = PROPID_M_BODY;

    propVar[0].vt = VT_VECTOR | VT_UI1;
    propVar[0].caub.pElems = bodyBuffer.data();
    propVar[0].caub.cElems = BODY_BUFFER_SIZE;

    msgProps.cProp = 1;
    msgProps.aPropID = propId;
    msgProps.aPropVar = propVar;
    msgProps.aStatus = status;

    HRESULT hr = MQReceiveMessage(
        gInputQueue,
        1000,
        MQ_ACTION_RECEIVE,
        &msgProps,
        nullptr,
        nullptr,
        nullptr,
        MQ_NO_TRANSACTION
    );

    if (hr == MQ_ERROR_IO_TIMEOUT)
    {
        return false;
    }

    if (FAILED(hr))
    {
        WriteLog(L"MQReceiveMessage fehlgeschlagen.");
        return false;
    }

    DWORD bytesRead = propVar[0].caub.cElems;

    if (bytesRead == 0)
    {
        WriteLog(L"Leere Nachricht empfangen.");
        return false;
    }

    int needed = MultiByteToWideChar(
        CP_UTF8,
        0,
        reinterpret_cast<LPCCH>(bodyBuffer.data()),
        static_cast<int>(bytesRead),
        nullptr,
        0
    );

    if (needed <= 0)
    {
        WriteLog(L"UTF-8 nach UTF-16 Konvertierung fehlgeschlagen.");
        return false;
    }

    std::wstring result(needed, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        reinterpret_cast<LPCCH>(bodyBuffer.data()),
        static_cast<int>(bytesRead),
        result.data(),
        needed
    );

    xmlText = result;

    return true;
}

std::wstring ExtractTagValue(const std::wstring& xml, const std::wstring& tagName)
{
    std::wstring startTag = L"<" + tagName + L">";
    std::wstring endTag = L"</" + tagName + L">";

    size_t start = xml.find(startTag);

    if (start == std::wstring::npos)
    {
        return L"";
    }

    start += startTag.length();

    size_t end = xml.find(endTag, start);

    if (end == std::wstring::npos)
    {
        return L"";
    }

    std::wstring value = xml.substr(start, end - start);

    while (!value.empty() && iswspace(value.front()))
    {
        value.erase(value.begin());
    }

    while (!value.empty() && iswspace(value.back()))
    {
        value.pop_back();
    }

    return value;
}

void SendMessageToQueue(
    QUEUEHANDLE queueHandle,
    const std::wstring& xmlText,
    const std::wstring& label)
{
    int needed = WideCharToMultiByte(
        CP_UTF8,
        0,
        xmlText.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr
    );

    if (needed <= 0)
    {
        WriteLog(L"UTF-16 nach UTF-8 Konvertierung fehlgeschlagen.");
        return;
    }

    std::string utf8Text(needed - 1, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        xmlText.c_str(),
        -1,
        utf8Text.data(),
        needed,
        nullptr,
        nullptr
    );

    MQMSGPROPS msgProps{};
    MSGPROPID propId[2]{};
    MQPROPVARIANT propVar[2]{};
    HRESULT status[2]{};

    propId[0] = PROPID_M_LABEL;
    propVar[0].vt = VT_LPWSTR;
    propVar[0].pwszVal = const_cast<LPWSTR>(label.c_str());

    propId[1] = PROPID_M_BODY;
    propVar[1].vt = VT_VECTOR | VT_UI1;
    propVar[1].caub.pElems = reinterpret_cast<UCHAR*>(utf8Text.data());
    propVar[1].caub.cElems = static_cast<ULONG>(utf8Text.size());

    msgProps.cProp = 2;
    msgProps.aPropID = propId;
    msgProps.aPropVar = propVar;
    msgProps.aStatus = status;

    HRESULT hr = MQSendMessage(
        queueHandle,
        &msgProps,
        MQ_NO_TRANSACTION
    );

    if (FAILED(hr))
    {
        WriteLog(L"MQSendMessage fehlgeschlagen.");
    }
    else
    {
        WriteLog(L"Nachricht weitergeleitet: " + label);
    }
}