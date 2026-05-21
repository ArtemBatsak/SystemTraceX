#include <atomic>
#include <chrono>
#include <thread>
#include <iostream>

#include "src/web/web_helper.h"
#include "src/collector/collector.h"
#include "src/web/web.h"


std::atomic<bool> running{ true };
Web* globalWebPtr = nullptr; 

void TelemetryWorker(Telemetry::TelemetryCollector& collector) {
    int ticks = 0;
    while (running.load()) {
        collector.PushLiveSnapshot(collector.CollectRawSnapshot());
        ++ticks;
        if (ticks % 10 == 0) collector.FlushTenSecondAggregation();
        if (ticks % 60 == 0) collector.FlushMinuteAggregation();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// ==========================================
//                 WINDOWS
// ==========================================
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
NOTIFYICONDATA fled;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit SystemTraceX");

            POINT curPoint;
            GetCursorPos(&curPoint);
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, curPoint.x, curPoint.y, 0, hWnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            running.store(false);

            
            if (globalWebPtr) {
                
                globalWebPtr->Stop();
            }

            PostQuitMessage(0);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    WebTelemetryHelper webHelper(collector);
    Web web(webHelper);
    globalWebPtr = &web;

    // 1. Collector
    std::thread telemetryThread(TelemetryWorker, std::ref(collector));
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 2. Web
    std::thread webThread([&]() {
        web.Start("0.0.0.0", 8080);
        });
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 3. create 
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SystemTraceX_TrayClass";
    RegisterClassEx(&wc);

    HWND hWnd = CreateWindowEx(0, wc.lpszClassName, "SystemTraceX", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    memset(&fled, 0, sizeof(NOTIFYICONDATA));
    fled.cbSize = sizeof(NOTIFYICONDATA);
    fled.hWnd = hWnd;
    fled.uID = 1;
    fled.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    fled.uCallbackMessage = WM_TRAYICON;
    fled.hIcon = LoadIcon(NULL, IDI_APPLICATION); // icon
    strcpy(fled.szTip, "SystemTraceX");
    Shell_NotifyIcon(NIM_ADD, &fled);

    // 4. main
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 5. clear
    Shell_NotifyIcon(NIM_DELETE, &fled);

    if (webThread.joinable()) webThread.join();
    if (telemetryThread.joinable()) telemetryThread.join();

    return 0;
}

// ==========================================
//                 LINUX
// ==========================================
#else
int main() {
    Telemetry::TelemetryCollector collector("./telemetry_logs");
    WebTelemetryHelper webHelper(collector);
    Web web(webHelper);

    
    std::thread telemetryThread(TelemetryWorker, std::ref(collector));
    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "SystemTraceX started on Linux. Press Ctrl+C to exit..." << std::endl;

    
    web.Start("0.0.0.0", 8000);

    
    running.store(false);
    if (telemetryThread.joinable()) telemetryThread.join();

    return 0;
}
#endif