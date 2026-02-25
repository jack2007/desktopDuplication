// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include <winsock2.h>
#include <ws2tcpip.h>
#include <limits.h>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "DisplayManager.h"
#include "DuplicationManager.h"
#include "ThreadManager.h"
#include "NetworkManager.h"

//
// Globals
//
NetworkManager NetMgr;

// Below are lists of errors expect from Dxgi API calls when a transition event like mode change, PnpStop, PnpStart
// desktop switch, TDR or session disconnect/reconnect. In all these cases we want the application to clean up the threads that process
// the desktop updates and attempt to recreate them.
// If we get an error that is not on the appropriate list then we exit the application

// These are the errors we expect from general Dxgi API due to a transition
HRESULT SystemTransitionsExpectedErrors[] = {
                                                DXGI_ERROR_DEVICE_REMOVED,
                                                DXGI_ERROR_ACCESS_LOST,
                                                static_cast<HRESULT>(WAIT_ABANDONED),
                                                S_OK                                    // Terminate list with zero valued HRESULT
                                            };

// These are the errors we expect from IDXGIOutput1::DuplicateOutput due to a transition
HRESULT CreateDuplicationExpectedErrors[] = {
                                                DXGI_ERROR_DEVICE_REMOVED,
                                                static_cast<HRESULT>(E_ACCESSDENIED),
                                                DXGI_ERROR_UNSUPPORTED,
                                                DXGI_ERROR_SESSION_DISCONNECTED,
                                                S_OK                                    // Terminate list with zero valued HRESULT
                                            };

// These are the errors we expect from IDXGIOutputDuplication methods due to a transition
HRESULT FrameInfoExpectedErrors[] = {
                                        DXGI_ERROR_DEVICE_REMOVED,
                                        DXGI_ERROR_ACCESS_LOST,
                                        S_OK                                    // Terminate list with zero valued HRESULT
                                    };

// These are the errors we expect from IDXGIAdapter::EnumOutputs methods due to outputs becoming stale during a transition
HRESULT EnumOutputsExpectedErrors[] = {
                                          DXGI_ERROR_NOT_FOUND,
                                          S_OK                                    // Terminate list with zero valued HRESULT
                                      };


//
// Forward Declarations
//
DWORD WINAPI DDProc(_In_ void* Param);
bool ProcessCmdline(_Out_ INT* Output, _Out_ INT* Port, _Out_ UINT* TargetFPS, UINT * CompressLevel);
void ShowHelp();
DUPL_RETURN GetOutputCountAndBounds(INT SingleOutput, _Out_ UINT* OutCount, _Out_ RECT* DeskBounds);

//
// Class for progressive waits
//
typedef struct
{
    UINT    WaitTime;
    UINT    WaitCount;
}WAIT_BAND;

#define WAIT_BAND_COUNT 3
#define WAIT_BAND_STOP 0

class DYNAMIC_WAIT
{
    public :
        DYNAMIC_WAIT();
        ~DYNAMIC_WAIT();

        void Wait();

    private :

    static const WAIT_BAND   m_WaitBands[WAIT_BAND_COUNT];

    // Period in seconds that a new wait call is considered part of the same wait sequence
    static const UINT       m_WaitSequenceTimeInSeconds = 2;

    UINT                    m_CurrentWaitBandIdx;
    UINT                    m_WaitCountInCurrentBand;
    LARGE_INTEGER           m_QPCFrequency;
    LARGE_INTEGER           m_LastWakeUpTime;
    BOOL                    m_QPCValid;
};
const WAIT_BAND DYNAMIC_WAIT::m_WaitBands[WAIT_BAND_COUNT] = {
                                                                 {250, 20},
                                                                 {2000, 60},
                                                                 {5000, WAIT_BAND_STOP}   // Never move past this band
                                                             };

DYNAMIC_WAIT::DYNAMIC_WAIT() : m_CurrentWaitBandIdx(0), m_WaitCountInCurrentBand(0)
{
    m_QPCValid = QueryPerformanceFrequency(&m_QPCFrequency);
    m_LastWakeUpTime.QuadPart = 0L;
}

DYNAMIC_WAIT::~DYNAMIC_WAIT()
{
}

void DYNAMIC_WAIT::Wait()
{
    LARGE_INTEGER CurrentQPC = {0};

    // Is this wait being called with the period that we consider it to be part of the same wait sequence
    QueryPerformanceCounter(&CurrentQPC);
    if (m_QPCValid && (CurrentQPC.QuadPart <= (m_LastWakeUpTime.QuadPart + (m_QPCFrequency.QuadPart * m_WaitSequenceTimeInSeconds))))
    {
        // We are still in the same wait sequence, lets check if we should move to the next band
        if ((m_WaitBands[m_CurrentWaitBandIdx].WaitCount != WAIT_BAND_STOP) && (m_WaitCountInCurrentBand > m_WaitBands[m_CurrentWaitBandIdx].WaitCount))
        {
            m_CurrentWaitBandIdx++;
            m_WaitCountInCurrentBand = 0;
        }
    }
    else
    {
        // Either we could not get the current time or we are starting a new wait sequence
        m_WaitCountInCurrentBand = 0;
        m_CurrentWaitBandIdx = 0;
    }

    // Sleep for the required period of time
    Sleep(m_WaitBands[m_CurrentWaitBandIdx].WaitTime);

    // Record the time we woke up so we can detect wait sequences
    QueryPerformanceCounter(&m_LastWakeUpTime);
    m_WaitCountInCurrentBand++;
}


//
// Program entry point
//
int main()
{
    // Set system timer resolution to 1ms for accurate waits/sleeps
    timeBeginPeriod(1);

    INT SingleOutput;
    INT ServerPort;
    UINT TargetFPS;
    UINT CompressLevel = 1;

    // Synchronization
    HANDLE UnexpectedErrorEvent = nullptr;
    HANDLE ExpectedErrorEvent = nullptr;
    HANDLE TerminateThreadsEvent = nullptr;

    bool CmdResult = ProcessCmdline(&SingleOutput, &ServerPort, &TargetFPS, &CompressLevel);
    if (!CmdResult)
    {
        ShowHelp();
        return 0;
    }

    // Force single output for network streaming
    if (SingleOutput < 0)
    {
        SingleOutput = 0;
    }

    // Event used by the threads to signal an unexpected error and we want to quit the app
    UnexpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!UnexpectedErrorEvent)
    {
        ProcessFailure(nullptr, L"UnexpectedErrorEvent creation failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Event for when a thread encounters an expected error
    ExpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ExpectedErrorEvent)
    {
        ProcessFailure(nullptr, L"ExpectedErrorEvent creation failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Event to tell spawned threads to quit
    TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!TerminateThreadsEvent)
    {
        ProcessFailure(nullptr, L"TerminateThreadsEvent creation failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Initialize Network Manager
    if (!NetMgr.Initialize(ServerPort, CompressLevel))
    {
        ProcessFailure(nullptr, L"Failed to initialize network manager", L"Error", E_FAIL);
        return 0;
    }

    THREADMANAGER ThreadMgr;
    RECT DeskBounds;
    UINT OutputCount;

    bool FirstTime = true;
    DYNAMIC_WAIT DynamicWait;
    HANDLE events[2] = { UnexpectedErrorEvent, ExpectedErrorEvent };

    while (true)
    {
        DUPL_RETURN Ret = DUPL_RETURN_SUCCESS;

        DWORD waitResult;
        if (FirstTime) {
            waitResult = WAIT_OBJECT_0 + 1;
        } else {
            waitResult = WaitForMultipleObjectsEx(2, events, FALSE, INFINITE, FALSE);
        }

        if (waitResult == WAIT_OBJECT_0)
        {
            // Unexpected error occurred so exit the application
            break;
        }
        else if (waitResult == WAIT_OBJECT_0 + 1)
        {
            if (!FirstTime)
            {
                // Terminate other threads
                SetEvent(TerminateThreadsEvent);
                ThreadMgr.WaitForThreadTermination();
                ResetEvent(TerminateThreadsEvent);
                ResetEvent(ExpectedErrorEvent);

                // Clean up
                ThreadMgr.Clean();

                // As we have encountered an error due to a system transition we wait before trying again, using this dynamic wait
                // the wait periods will get progressively long to avoid wasting too much system resource if this state lasts a long time
                DynamicWait.Wait();
            }
            else
            {
                // First time through the loop so nothing to clean up
                FirstTime = false;
            }

            // Re-initialize
            Ret = GetOutputCountAndBounds(SingleOutput, &OutputCount, &DeskBounds);
            if (Ret == DUPL_RETURN_SUCCESS)
            {
                // Wait for client connection if not connected
                if (!NetMgr.IsConnected())
                {
                    wprintf(L"Waiting for client connection on port %d...\n", ServerPort);
                    if (!NetMgr.WaitForClient())
                    {
                        Ret = DUPL_RETURN_ERROR_UNEXPECTED;
                    }
                    else
                    {
                        wprintf(L"Client connected!\n");
                    }
                }

                if (Ret == DUPL_RETURN_SUCCESS)
                {
                    Ret = ThreadMgr.Initialize(SingleOutput, OutputCount, UnexpectedErrorEvent, ExpectedErrorEvent, TerminateThreadsEvent, nullptr, &DeskBounds, TargetFPS);
                    if (Ret != DUPL_RETURN_SUCCESS)
                    {
                        DisplayMsg(L"Failed to initialize threads", L"Error", S_OK);
                        Ret = DUPL_RETURN_ERROR_UNEXPECTED;
                    }
                }
            }

            // Check if for errors
            if (Ret != DUPL_RETURN_SUCCESS)
            {
                if (Ret == DUPL_RETURN_ERROR_EXPECTED)
                {
                    // Some type of system transition is occurring so retry
                    SetEvent(ExpectedErrorEvent);
                }
                else
                {
                    // Unexpected error so exit
                    break;
                }
            }
        }
    }

    // Make sure all other threads have exited
    if (SetEvent(TerminateThreadsEvent))
    {
        ThreadMgr.WaitForThreadTermination();
    }

    // Clean up
    CloseHandle(UnexpectedErrorEvent);
    CloseHandle(ExpectedErrorEvent);
    CloseHandle(TerminateThreadsEvent);

    // Restore system timer resolution
    timeEndPeriod(1);

    return 0;
}

//
// Shows help
//
void ShowHelp()
{
    DisplayMsg(L"The following optional parameters can be used -\n  /output [all | n]\t\tto duplicate all outputs or the nth output\n  /port [n]\t\t\tto listen on port n (default: 12306)\n  /fps [n]\t\t\tto limit capture to n frames per second (default: unlimited)\n  /?\t\t\t\tto display this help section",
               L"Proper usage", S_OK);
}

//
// Process command line parameters
//
bool ProcessCmdline(_Out_ INT* Output, _Out_ INT* Port, _Out_ UINT* TargetFPS, UINT* CompressLevel)
{
    *Output = -1;
    *Port = DEFAULT_SERVER_PORT;
    *TargetFPS = 0;

    // __argv and __argc are global vars set by system
    for (UINT i = 1; i < static_cast<UINT>(__argc); ++i)
    {
        if ((strcmp(__argv[i], "-output") == 0) ||
            (strcmp(__argv[i], "/output") == 0))
        {
            if (++i >= static_cast<UINT>(__argc))
            {
                return false;
            }

            if (strcmp(__argv[i], "all") == 0)
            {
                *Output = -1;
            }
            else
            {
                *Output = atoi(__argv[i]);
            }
            continue;
        }
        else if ((strcmp(__argv[i], "-port") == 0) ||
                 (strcmp(__argv[i], "/port") == 0))
        {
            if (++i >= static_cast<UINT>(__argc))
            {
                return false;
            }
            int port = atoi(__argv[i]);
            if (port <= 0 || port > 65535)
            {
                return false;
            }
            *Port = port;
            continue;
        }
        else if ((strcmp(__argv[i], "-fps") == 0) ||
                 (strcmp(__argv[i], "/fps") == 0))
        {
            if (++i >= static_cast<UINT>(__argc))
            {
                return false;
            }
            int fps = atoi(__argv[i]);
            if (fps < 0)
            {
                return false;
            }
            *TargetFPS = static_cast<UINT>(fps);
            continue;
        }
        else if ((strcmp(__argv[i], "-compress") == 0) ||
            (strcmp(__argv[i], "/compress") == 0))
        {
            if (++i >= static_cast<UINT>(__argc))
            {
                return false;
            }
            int level = atoi(__argv[i]);
            if (level < 0 || level > 9)
            {
                return false;
            }
            *CompressLevel = static_cast<UINT>(level);
			continue;
        }
        else
        {
            return false;
        }
    }
    return true;
}

//
// Get output count and bounds
//
DUPL_RETURN GetOutputCountAndBounds(INT SingleOutput, _Out_ UINT* OutCount, _Out_ RECT* DeskBounds)
{
    HRESULT hr;
    IDXGIFactory1* Factory = nullptr;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&Factory));
    if (FAILED(hr)) return DUPL_RETURN_ERROR_UNEXPECTED;

    IDXGIAdapter* Adapter = nullptr;
    hr = Factory->EnumAdapters(0, &Adapter);
    Factory->Release();
    if (FAILED(hr)) return DUPL_RETURN_ERROR_UNEXPECTED;

    DeskBounds->left = INT_MAX;
    DeskBounds->right = INT_MIN;
    DeskBounds->top = INT_MAX;
    DeskBounds->bottom = INT_MIN;

    IDXGIOutput* Output = nullptr;
    UINT OutputCount = 0;

    if (SingleOutput < 0)
    {
        for (OutputCount = 0; SUCCEEDED(hr); ++OutputCount)
        {
            if (Output)
            {
                Output->Release();
                Output = nullptr;
            }
            hr = Adapter->EnumOutputs(OutputCount, &Output);
            if (Output && (hr != DXGI_ERROR_NOT_FOUND))
            {
                DXGI_OUTPUT_DESC DesktopDesc;
                Output->GetDesc(&DesktopDesc);

                DeskBounds->left = min(DesktopDesc.DesktopCoordinates.left, DeskBounds->left);
                DeskBounds->top = min(DesktopDesc.DesktopCoordinates.top, DeskBounds->top);
                DeskBounds->right = max(DesktopDesc.DesktopCoordinates.right, DeskBounds->right);
                DeskBounds->bottom = max(DesktopDesc.DesktopCoordinates.bottom, DeskBounds->bottom);
            }
        }
        --OutputCount;
    }
    else
    {
        hr = Adapter->EnumOutputs(SingleOutput, &Output);
        if (FAILED(hr))
        {
            Adapter->Release();
            return DUPL_RETURN_ERROR_UNEXPECTED;
        }
        DXGI_OUTPUT_DESC DesktopDesc;
        Output->GetDesc(&DesktopDesc);
        *DeskBounds = DesktopDesc.DesktopCoordinates;
        Output->Release();
        OutputCount = 1;
    }

    Adapter->Release();
    *OutCount = OutputCount;

    if (OutputCount == 0) return DUPL_RETURN_ERROR_EXPECTED;
    return DUPL_RETURN_SUCCESS;
}

//
// Entry point for new duplication threads
//
DWORD WINAPI DDProc(_In_ void* Param)
{
    // Classes
    DUPLICATIONMANAGER DuplMgr;

    // Data passed in from thread creation
    THREAD_DATA* TData = reinterpret_cast<THREAD_DATA*>(Param);

    // Get desktop
    DUPL_RETURN Ret;
    HDESK CurrentDesktop = nullptr;
    CurrentDesktop = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (!CurrentDesktop)
    {
        // We do not have access to the desktop so request a retry
        SetEvent(TData->ExpectedErrorEvent);
        Ret = DUPL_RETURN_ERROR_EXPECTED;
        goto Exit;
    }

    // Attach desktop to this thread
    bool DesktopAttached = SetThreadDesktop(CurrentDesktop) != 0;
    CloseDesktop(CurrentDesktop);
    CurrentDesktop = nullptr;
    if (!DesktopAttached)
    {
        // We do not have access to the desktop so request a retry
        Ret = DUPL_RETURN_ERROR_EXPECTED;
        goto Exit;
    }

    // Make duplication manager
    Ret = DuplMgr.InitDupl(TData->DxRes.Device, TData->Output);
    if (Ret != DUPL_RETURN_SUCCESS)
    {
        goto Exit;
    }

    // Get output description
    DXGI_OUTPUT_DESC DesktopDesc;
    RtlZeroMemory(&DesktopDesc, sizeof(DXGI_OUTPUT_DESC));
    DuplMgr.GetOutputDesc(&DesktopDesc);

    // Send Init Packet
    if (!NetMgr.SendInitPacket(DesktopDesc.DesktopCoordinates.right - DesktopDesc.DesktopCoordinates.left,
                          DesktopDesc.DesktopCoordinates.bottom - DesktopDesc.DesktopCoordinates.top,
                          DXGI_FORMAT_B8G8R8A8_UNORM))
    {
        Ret = DUPL_RETURN_ERROR_EXPECTED;
        SetEvent(TData->ExpectedErrorEvent);
        goto Exit;
    }

    // Main duplication loop
    bool WaitToProcessCurrentFrame = false;
    FRAME_DATA CurrentData;

    // FPS throttling and server-side stats
    LARGE_INTEGER qpcFreq = {};
    LARGE_INTEGER lastSentFrameTime = {};
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&lastSentFrameTime);
    LONGLONG frameIntervalTicks = (TData->TargetFPS > 0 && qpcFreq.QuadPart > 0)
        ? (qpcFreq.QuadPart / static_cast<LONGLONG>(TData->TargetFPS)) : 0LL;

    printf("frameIntervalTicks %ld\n", frameIntervalTicks);

    UINT statsFrameCount = 0;
    ULONGLONG statsLastTick = GetTickCount64();

    while ((WaitForSingleObjectEx(TData->TerminateThreadsEvent, 10, FALSE) == WAIT_TIMEOUT))
    {
        if (!WaitToProcessCurrentFrame)
        {
            // Get new frame from desktop duplication
            bool TimeOut;
            Ret = DuplMgr.GetFrame(&CurrentData, &TimeOut);
            if (Ret != DUPL_RETURN_SUCCESS)
            {
                // An error occurred getting the next frame drop out of loop which
                // will check if it was expected or not
                break;
            }

            // Check for timeout
            if (TimeOut)
            {
                // No new frame at the moment
                NetMgr.ProcessPendingMouseInput();
                continue;
            }
        }

        // We have a new frame so try and process it
        WaitToProcessCurrentFrame = false;

        // Get mouse info
        //Ret = DuplMgr.GetMouse(TData->PtrInfo, &(CurrentData.FrameInfo), TData->OffsetX, TData->OffsetY);
        //if (Ret != DUPL_RETURN_SUCCESS)
        //{
        //    DuplMgr.DoneWithFrame();
        //    break;
        //}

        // Update FPS tracking and log stats every second
        if (frameIntervalTicks > 0)
        {
            // 按理论间隔推进，保持长期平均帧率稳定
            lastSentFrameTime.QuadPart += frameIntervalTicks;

            // 如果落后太多（例如画面静止很久没更新），则重置为当前时间
            LARGE_INTEGER currentTime;
            QueryPerformanceCounter(&currentTime);
            if (currentTime.QuadPart - lastSentFrameTime.QuadPart > frameIntervalTicks)
            {
                lastSentFrameTime.QuadPart = currentTime.QuadPart;
            }
        }
        else
        {
            QueryPerformanceCounter(&lastSentFrameTime);
        }

        // Process new frame and send over network
        if (!NetMgr.SendFramePacket(&CurrentData, TData->PtrInfo, TData->DxRes.Device, TData->DxRes.Context))
        {
            DuplMgr.DoneWithFrame();
            Ret = DUPL_RETURN_ERROR_EXPECTED;
            SetEvent(TData->ExpectedErrorEvent);
            break;
        }

        // Update FPS tracking and log stats every second
        QueryPerformanceCounter(&lastSentFrameTime);
        statsFrameCount++;
        ULONGLONG nowMs = GetTickCount64();
        ULONGLONG elapsed = nowMs - statsLastTick;
        if (elapsed >= 1000)
        {
            float fps = static_cast<float>(statsFrameCount) * 1000.0f / static_cast<float>(elapsed);
            wprintf(L"Capture FPS: %.1f\n", fps);
            statsFrameCount = 0;
            statsLastTick = nowMs;
        }

        // Release frame back to desktop duplication
        Ret = DuplMgr.DoneWithFrame();
        if (Ret != DUPL_RETURN_SUCCESS)
        {
            break;
        }

        // Check for mouse input from client (non-blocking)
        NetMgr.ProcessPendingMouseInput();
    }

Exit:
    NetMgr.Disconnect();

    if (Ret != DUPL_RETURN_SUCCESS)
    {
        if (Ret == DUPL_RETURN_ERROR_EXPECTED)
        {
            // The system is in a transition state so request the duplication be restarted
            SetEvent(TData->ExpectedErrorEvent);
        }
        else
        {
            // Unexpected error so exit the application
            SetEvent(TData->UnexpectedErrorEvent);
        }
    }

    return 0;
}

_Post_satisfies_(return != DUPL_RETURN_SUCCESS)
DUPL_RETURN ProcessFailure(_In_opt_ ID3D11Device* Device, _In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr, _In_opt_z_ HRESULT* ExpectedErrors)
{
    HRESULT TranslatedHr;

    // On an error check if the DX device is lost
    if (Device)
    {
        HRESULT DeviceRemovedReason = Device->GetDeviceRemovedReason();

        switch (DeviceRemovedReason)
        {
            case DXGI_ERROR_DEVICE_REMOVED :
            case DXGI_ERROR_DEVICE_RESET :
            case static_cast<HRESULT>(E_OUTOFMEMORY) :
            {
                // Our device has been stopped due to an external event on the GPU so map them all to
                // device removed and continue processing the condition
                TranslatedHr = DXGI_ERROR_DEVICE_REMOVED;
                break;
            }

            case S_OK :
            {
                // Device is not removed so use original error
                TranslatedHr = hr;
                break;
            }

            default :
            {
                // Device is removed but not a error we want to remap
                TranslatedHr = DeviceRemovedReason;
            }
        }
    }
    else
    {
        TranslatedHr = hr;
    }

    // Check if this error was expected or not
    if (ExpectedErrors)
    {
        HRESULT* CurrentResult = ExpectedErrors;

        while (*CurrentResult != S_OK)
        {
            if (*(CurrentResult++) == TranslatedHr)
            {
                return DUPL_RETURN_ERROR_EXPECTED;
            }
        }
    }

    // Error was not expected so display the message box
    DisplayMsg(Str, Title, TranslatedHr);

    return DUPL_RETURN_ERROR_UNEXPECTED;
}

//
// Displays a message
//
void DisplayMsg(_In_ LPCWSTR Str, _In_ LPCWSTR Title, HRESULT hr)
{
    if (SUCCEEDED(hr))
    {
        wprintf(L"%s: %s\n", Title, Str);
        return;
    }

    wprintf(L"%s: %s with HRESULT 0x%08X.\n", Title, Str, hr);
}
