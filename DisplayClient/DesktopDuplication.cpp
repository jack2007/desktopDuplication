// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved

#include <winsock2.h>
#include <ws2tcpip.h>
#include <limits.h>
#include <windowsx.h>

#include "ClientLogic.h"
#include "Logger.h"

//
// Globals
//
ClientLogic g_ClientLogic;

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
static const int SERVER_IP_BUFFER_SIZE = 64;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
bool ProcessCmdline(_Out_ INT* Output, _Out_writes_(SERVER_IP_BUFFER_SIZE) char* ServerIP, _Out_ int* Port);
void ShowHelp();

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
int WINAPI WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPSTR lpCmdLine, _In_ INT nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // Initialise logging: file goes to logs/display_client.log
    InitLogger("logs/display_client.log");
    LOG_INFO("DisplayClient starting up");

    INT SingleOutput;
    char ServerIP[SERVER_IP_BUFFER_SIZE];
    int ServerPort;

    // Window
    HWND WindowHandle = nullptr;

    bool CmdResult = ProcessCmdline(&SingleOutput, ServerIP, &ServerPort);
    if (!CmdResult)
    {
        ShowHelp();
        return 0;
    }

    LOG_INFO("Configuration: output={}, server={}:{}", SingleOutput, ServerIP, ServerPort);

    // Load simple cursor
    HCURSOR Cursor = nullptr;
    Cursor = LoadCursor(nullptr, IDC_ARROW);
    if (!Cursor)
    {
        ProcessFailure(nullptr, L"Cursor load failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Register class
    WNDCLASSEXW Wc;
    Wc.cbSize           = sizeof(WNDCLASSEXW);
    Wc.style            = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    Wc.lpfnWndProc      = WndProc;
    Wc.cbClsExtra       = 0;
    Wc.cbWndExtra       = 0;
    Wc.hInstance        = hInstance;
    Wc.hIcon            = nullptr;
    Wc.hCursor          = Cursor;
    Wc.hbrBackground    = nullptr;
    Wc.lpszMenuName     = nullptr;
    Wc.lpszClassName    = L"ddasample";
    Wc.hIconSm          = nullptr;
    if (!RegisterClassExW(&Wc))
    {
        ProcessFailure(nullptr, L"Window class registration failed", L"Error", E_UNEXPECTED);
        return 0;
    }

    // Create window
    RECT WindowRect = {0, 0, 800, 600};
    AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE);
    WindowHandle = CreateWindowW(L"ddasample", L"DXGI desktop duplication sample",
                           WS_OVERLAPPEDWINDOW,
                           0, 0,
                           WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top,
                           nullptr, nullptr, hInstance, nullptr);
    if (!WindowHandle)
    {
        ProcessFailure(nullptr, L"Window creation failed", L"Error", E_FAIL);
        return 0;
    }

    DestroyCursor(Cursor);

    ShowWindow(WindowHandle, nCmdShow);
    UpdateWindow(WindowHandle);

    DUPL_RETURN Ret = g_ClientLogic.Initialize(WindowHandle, ServerIP, ServerPort);
    if (Ret == DUPL_RETURN_SUCCESS)
    {
        LOG_INFO("ClientLogic initialized, entering main loop");
        DYNAMIC_WAIT DynWait;
        for (;;)
        {
            g_ClientLogic.RunLoop();

            // RunLoop exited; process any pending messages so WM_DESTROY can
            // post WM_QUIT before we decide whether to reconnect.
            bool shouldExit = false;
            MSG peekMsg = {};
            while (PeekMessage(&peekMsg, nullptr, 0, 0, PM_REMOVE))
            {
                if (peekMsg.message == WM_QUIT)
                {
                    shouldExit = true;
                    break;
                }
                TranslateMessage(&peekMsg);
                DispatchMessage(&peekMsg);
            }

            if (shouldExit || !IsWindow(WindowHandle))
            {
                LOG_INFO("WM_QUIT received, exiting");
                break;
            }

            // Connection was lost; attempt to reconnect with progressive backoff
            LOG_WARN("Connection lost, attempting to reconnect to {}:{}", ServerIP, ServerPort);
            g_ClientLogic.Clean();
            DynWait.Wait();
            Ret = g_ClientLogic.Initialize(WindowHandle, ServerIP, ServerPort);
            if (Ret == DUPL_RETURN_ERROR_UNEXPECTED)
            {
                LOG_ERROR("Reconnect failed with unexpected error, exiting");
                break;
            }
        }
    }
    else
    {
        LOG_ERROR("ClientLogic initialization failed");
    }

    // Clean up
    g_ClientLogic.Clean();

    LOG_INFO("DisplayClient shutting down");
    spdlog::shutdown();

    return 0;
}

//
// Shows help
//
void ShowHelp()
{
    DisplayMsg(L"The following optional parameters can be used -\n"
               L"  /output [all | n]\t\tto duplicate all outputs or the nth output\n"
               L"  /server <ip>\t\t\tto specify the capture server IP address (default: 127.0.0.1)\n"
               L"  /port <n>\t\t\tto specify the server port (default: 12306)\n"
               L"  /?\t\t\t\tto display this help section",
               L"Proper usage", S_OK);
}

//
// Process command line parameters
//
bool ProcessCmdline(_Out_ INT* Output, _Out_writes_(SERVER_IP_BUFFER_SIZE) char* ServerIP, _Out_ int* Port)
{
    *Output = -1;
    strcpy_s(ServerIP, SERVER_IP_BUFFER_SIZE, "127.0.0.1");
    *Port = DEFAULT_SERVER_PORT;

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
        else if ((strcmp(__argv[i], "-server") == 0) ||
                 (strcmp(__argv[i], "/server") == 0))
        {
            if (++i >= static_cast<UINT>(__argc))
            {
                return false;
            }
            strcpy_s(ServerIP, SERVER_IP_BUFFER_SIZE, __argv[i]);
            continue;
        }
        else if ((strcmp(__argv[i], "-port") == 0) ||
                 (strcmp(__argv[i], "/port") == 0))
        {
            if (++i >= static_cast<UINT>(__argc))
            {
                return false;
            }
            *Port = atoi(__argv[i]);
            if (*Port <= 0 || *Port > 65535)
            {
                return false;
            }
            continue;
        }
        else
        {
            return false;
        }
    }
    return true;
}

static WINDOWPLACEMENT g_wpPrev = { sizeof(g_wpPrev) };

void ToggleFullscreen(HWND hWnd, bool enterFullscreen)
{
    DWORD dwStyle = GetWindowLong(hWnd, GWL_STYLE);
    if (enterFullscreen)
    {
        if (dwStyle & WS_OVERLAPPEDWINDOW)
        {
            MONITORINFO mi = { sizeof(mi) };
            if (GetWindowPlacement(hWnd, &g_wpPrev) &&
                GetMonitorInfo(MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY), &mi))
            {
                SetWindowLong(hWnd, GWL_STYLE, dwStyle & ~WS_OVERLAPPEDWINDOW);
                SetWindowPos(hWnd, HWND_TOP,
                             mi.rcMonitor.left, mi.rcMonitor.top,
                             mi.rcMonitor.right - mi.rcMonitor.left,
                             mi.rcMonitor.bottom - mi.rcMonitor.top,
                             SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            }
        }
    }
    else
    {
        if (!(dwStyle & WS_OVERLAPPEDWINDOW))
        {
            SetWindowLong(hWnd, GWL_STYLE, dwStyle | WS_OVERLAPPEDWINDOW);
            SetWindowPlacement(hWnd, &g_wpPrev);
            SetWindowPos(hWnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                         SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
    }
}

//
// Window message processor
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        case WM_SIZE:
        {
            // Tell output manager that window size has changed
            g_ClientLogic.WindowResize();
            break;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        {
            if (wParam == 'P') // Ctrl + Shift + P
            {
                if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000))
                {
                    ToggleFullscreen(hWnd, true);
                    break;
                }
            }
            else if (wParam == 'L') // Ctrl + Shift + L
            {
                if ((GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_SHIFT) & 0x8000))
                {
                    ToggleFullscreen(hWnd, false);
                    break;
                }
            }
            g_ClientLogic.OnKeyboardInput(static_cast<UINT>(wParam), lParam, false);
            break;
        }
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            g_ClientLogic.OnKeyboardInput(static_cast<UINT>(wParam), lParam, true);
            break;
        }
        case WM_MOUSEMOVE:
        {
            g_ClientLogic.OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_LBUTTONDOWN:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_LBUTTON_DOWN, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_LBUTTONUP:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_LBUTTON_UP, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_RBUTTONDOWN:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_RBUTTON_DOWN, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_RBUTTONUP:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_RBUTTON_UP, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_MBUTTONDOWN:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_MBUTTON_DOWN, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_MBUTTONUP:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_MBUTTON_UP, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_LBUTTONDBLCLK:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_LBUTTON_DBLCLK, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_RBUTTONDBLCLK:
        {
            g_ClientLogic.OnMouseButton(MOUSE_INPUT_RBUTTON_DBLCLK, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            break;
        }
        case WM_MOUSEWHEEL:
        {
            POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &pt);
            g_ClientLogic.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam), pt.x, pt.y);
            break;
        }
        case WM_SETCURSOR:
        {
            if (LOWORD(lParam) == HTCLIENT)
            {
                if (g_ClientLogic.ShouldHideLocalCursor())
                {
                    SetCursor(nullptr);
                }
                else
                {
                    SetCursor(LoadCursor(nullptr, IDC_ARROW));
                }
                return TRUE;
            }
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
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
                LOG_WARN("ProcessFailure: expected error - {} (HRESULT=0x{:08X})",
                         WStrToStr(Str), static_cast<unsigned>(TranslatedHr));
                return DUPL_RETURN_ERROR_EXPECTED;
            }
        }
    }

    // Error was not expected so display the message box
    LOG_ERROR("ProcessFailure: unexpected error - {} (HRESULT=0x{:08X})",
              WStrToStr(Str), static_cast<unsigned>(TranslatedHr));
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
        LOG_INFO("{}: {}", WStrToStr(Title), WStrToStr(Str));
        MessageBoxW(nullptr, Str, Title, MB_OK);
        return;
    }

    LOG_ERROR("{}: {} (HRESULT=0x{:08X})", WStrToStr(Title), WStrToStr(Str), static_cast<unsigned>(hr));

    const UINT StringLen = (UINT)(wcslen(Str) + sizeof(" with HRESULT 0x########."));
    wchar_t* OutStr = new wchar_t[StringLen];
    if (!OutStr)
    {
        return;
    }

    INT LenWritten = swprintf_s(OutStr, StringLen, L"%s with 0x%X.", Str, hr);
    if (LenWritten != -1)
    {
        MessageBoxW(nullptr, OutStr, Title, MB_OK);
    }

    delete [] OutStr;
}
