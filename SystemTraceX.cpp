#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

#include "src/web/web_helper.h"
#include "src/collector/collector.h"
#include "src/web/web.h"
#include "src/func/funk.h"


std::atomic<bool> running{ true };
Web* globalWebPtr = nullptr; 

// ==========================================
//                 WINDOWS
// ==========================================
#ifdef _WIN32

#include <windows.h>
#include <shellapi.h>

#include "src/resource/resource.h"

#define WM_TRAYICON (WM_USER + 1)

#define ID_TRAY_OPEN 1000
#define ID_TRAY_EXIT 1001

constexpr int WEB_PORT = 8080;

NOTIFYICONDATA fled;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_TRAYICON:
    {
        // double click
        if (lParam == WM_LBUTTONDBLCLK)
        {
            std::string url =
                "http://127.0.0.1:" + std::to_string(WEB_PORT);

            ShellExecuteA(
                NULL,
                "open",
                url.c_str(),
                NULL,
                NULL,
                SW_SHOWNORMAL
            );
        }

        // right click
        if (lParam == WM_RBUTTONUP)
        {
            HMENU hMenu = CreatePopupMenu();

            AppendMenu(hMenu, MF_STRING, ID_TRAY_OPEN, "Open Web");
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit SystemTraceX");

            POINT curPoint;
            GetCursorPos(&curPoint);

            SetForegroundWindow(hWnd);

            TrackPopupMenu(
                hMenu,
                TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                curPoint.x,
                curPoint.y,
                0,
                hWnd,
                NULL
            );

            DestroyMenu(hMenu);
        }

        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_TRAY_OPEN:
        {
            std::string url =
                "http://127.0.0.1:" + std::to_string(WEB_PORT);

            ShellExecuteA(
                NULL,
                "open",
                url.c_str(),
                NULL,
                NULL,
                SW_SHOWNORMAL
            );

            break;
        }

        case ID_TRAY_EXIT:
        {
            running.store(false);

            if (globalWebPtr)
            {
                globalWebPtr->Stop();
            }

            PostQuitMessage(0);
            break;
        }
        }

        break;
    }

    case WM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}

int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine,
    int nCmdShow
)
{
    Telemetry::TelemetryCollector collector("./telemetry_logs");
   

    WebTelemetryHelper webHelper(collector);
    TaskLogger taskLogger(collector);
    taskLogger.start();
    std::this_thread::sleep_for(
        std::chrono::seconds(2));
	Ping ping; 
    Web web(webHelper, taskLogger, ping);

    globalWebPtr = &web;

    // web
    std::thread webThread([&]()
        {
            web.Start("0.0.0.0", WEB_PORT);
        });

    // window class
    WNDCLASSEX wc = { 0 };

    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SystemTraceX_TrayClass";

    RegisterClassEx(&wc);

    // hidden window
    HWND hWnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        "SystemTraceX",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        NULL,
        hInstance,
        NULL
    );

    // tray icon
    memset(&fled, 0, sizeof(NOTIFYICONDATA));

    fled.cbSize = sizeof(NOTIFYICONDATA);
    fled.hWnd = hWnd;
    fled.uID = 1;

    fled.uFlags =
        NIF_ICON |
        NIF_MESSAGE |
        NIF_TIP;

    fled.uCallbackMessage = WM_TRAYICON;

    // icon from .rc
    fled.hIcon = (HICON)LoadImage(
        hInstance,
        MAKEINTRESOURCE(IDI_APP_ICON),
        IMAGE_ICON,
        16,
        16,
        LR_DEFAULTCOLOR
    );

    strcpy_s(fled.szTip, "SystemTraceX");

    Shell_NotifyIcon(
        NIM_ADD,
        &fled
    );

    // main loop
    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // cleanup
    Shell_NotifyIcon(
        NIM_DELETE,
        &fled
    );

    if (webThread.joinable())
        webThread.join();

    return 0;
}


// ==========================================
//                 LINUX
// ==========================================
#else
int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    WebTelemetryHelper webHelper(collector);
    TaskLogger taskLogger(collector);
    taskLogger.start();
    Ping ping;
    Web web(webHelper, taskLogger, ping);

    std::cout << "SystemTraceX started on Linux. Press Ctrl+C to exit..." << std::endl;

    web.Start("0.0.0.0", 8000);

    running.store(false);

    return 0;
}
#endif
