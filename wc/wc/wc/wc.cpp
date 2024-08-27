#include <iostream>
#include <windows.h>
#include <random>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <shellapi.h>
#include <functional>
#define IDD_DIALOG_CPS 101
#define IDC_EDIT_CPS 1001
#define IDI_TRAYICON 102  // Add this line

#pragma comment(lib, "winmm.lib")

namespace vars {
    int cps{ 0 };
    HWND minecraft_window{};
    std::atomic<bool> running{ true };
    std::vector<int> delays;
    NOTIFYICONDATA nid = {};
    HMENU hMenu = nullptr;
    std::vector<int> right_click_delays;
    std::atomic<bool> paused{ false };
}

namespace math {
    int get_random_int(int min, int max) {
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(min, max);
        return distribution(generator);
    }
}

namespace nt {
    typedef NTSTATUS(NTAPI* NtDelayExecutionFunc)(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);
    NtDelayExecutionFunc NtDelayExecution = nullptr;

    void Sleep(LONGLONG delay) {
        if (!NtDelayExecution) {
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            if (ntdll) {
                NtDelayExecution = reinterpret_cast<NtDelayExecutionFunc>(
                    GetProcAddress(ntdll, "NtDelayExecution"));
            }
        }

        if (NtDelayExecution) {
            LARGE_INTEGER li;
            li.QuadPart = -delay * 10000LL;
            NtDelayExecution(FALSE, &li);
        }
        else {
            ::Sleep(static_cast<DWORD>(delay));
        }
    }
}

void generate_delays(int cps) {
    vars::delays.clear();
    double base_delay = 1000.0 / cps;
    double variation_range = base_delay * 0.1;

    for (int x = 0; x < 5000; x++) {
        double variation = math::get_random_int(-100, 100) * variation_range / 100.0;
        int delay = (std::max)(1, static_cast<int>(std::round(base_delay + variation)));
        vars::delays.push_back(delay);
    }
}

void rotate_delays() {
    std::rotate(vars::delays.begin(), vars::delays.begin() + 1, vars::delays.end());
}

void LeftMouseClick() {
    PostMessageA(vars::minecraft_window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
    PostMessageA(vars::minecraft_window, WM_LBUTTONUP, MK_LBUTTON, 0);
    nt::Sleep(static_cast<LONGLONG>(vars::delays.front()));
    rotate_delays();
}

void generate_right_click_delays(int cps) {
    vars::right_click_delays.clear();
    double base_delay = 1000.0 / (cps * 0.7);  // 70% of the left-click speed (30% slower)
    double variation_range = base_delay * 0.1;

    for (int x = 0; x < 5000; x++) {
        double variation = math::get_random_int(-100, 100) * variation_range / 100.0;
        int delay = (std::max)(1, static_cast<int>(std::round(base_delay + variation)));
        vars::right_click_delays.push_back(delay);
    }
}

void rotate_right_click_delays() {
    std::rotate(vars::right_click_delays.begin(), vars::right_click_delays.begin() + 1, vars::right_click_delays.end());
}

void RightMouseClick() {
    PostMessageA(vars::minecraft_window, WM_RBUTTONDOWN, MK_RBUTTON, 0);
    PostMessageA(vars::minecraft_window, WM_RBUTTONUP, MK_RBUTTON, 0);
    nt::Sleep(static_cast<LONGLONG>(vars::right_click_delays.front()));
    rotate_right_click_delays();
}

void Mouse5Click() {
    keybd_event('2', 0, 0, 0);
    nt::Sleep(15);
    keybd_event('2', 0, KEYEVENTF_KEYUP, 0);
    nt::Sleep(7);
    PostMessageA(vars::minecraft_window, WM_RBUTTONDOWN, MK_RBUTTON, 0);
    nt::Sleep(7);
    PostMessageA(vars::minecraft_window, WM_RBUTTONUP, MK_RBUTTON, 0);
    nt::Sleep(15);
    keybd_event('1', 0, 0, 0);
    nt::Sleep(15);
    keybd_event('1', 0, KEYEVENTF_KEYUP, 0);
    nt::Sleep(15);
}

void RemoveSystemTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &vars::nid);
}

void CreateSystemTrayIcon(HWND hwnd) {
    vars::nid.cbSize = sizeof(NOTIFYICONDATA);
    vars::nid.hWnd = hwnd;
    vars::nid.uID = 1;
    vars::nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    vars::nid.uCallbackMessage = WM_USER + 1;
    
    // Load the custom icon
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_TRAYICON));
    if (hIcon) {
        vars::nid.hIcon = hIcon;
    } else {
        // Fallback to default application icon if custom icon fails to load
        vars::nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    
    wcscpy_s(vars::nid.szTip, L"WC Autoclicker");
    Shell_NotifyIcon(NIM_ADD, &vars::nid);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_USER + 1:
        // Handle system tray messages here
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            UINT clicked = TrackPopupMenu(vars::hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
            if (clicked == 1) {
                vars::running = false;
                PostQuitMessage(0);
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

INT_PTR CALLBACK DialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static HBRUSH hBrushBackground = CreateSolidBrush(RGB(0, 0, 0));

    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            // Set the background color to black for the dialog
            SetClassLongPtr(hwndDlg, GCLP_HBRBACKGROUND, (LONG_PTR)hBrushBackground);
            
            // Set the text color to white for all child controls
            EnumChildWindows(hwndDlg, [](HWND hwnd, LPARAM lParam) -> BOOL {
                SetWindowLongPtr(hwnd, GWL_STYLE, GetWindowLongPtr(hwnd, GWL_STYLE) | WS_CHILD);
                return TRUE;
            }, 0);

            return TRUE;
        }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
        {
            HDC hdcStatic = (HDC)wParam;
            SetTextColor(hdcStatic, RGB(255, 255, 255));  // white text
            SetBkColor(hdcStatic, RGB(0, 0, 0));  // gray background
            return (INT_PTR)hBrushBackground;
        }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
            // Get the CPS value from the edit control
            vars::cps = GetDlgItemInt(hwndDlg, IDC_EDIT_CPS, NULL, FALSE);
            EndDialog(hwndDlg, IDOK);
            return TRUE;

        case IDCANCEL:
            EndDialog(hwndDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        DeleteObject(hBrushBackground);
        return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    std::cout << "wc -- by big y\n\n";
    std::cout << "--------------------\n" << "CPS CONVERSION CHART\n";
    std::cout << "   20 = 12-14 cps\n   30 = 17-19 cps\n   40 = 21-23 cps\n   50 = 22-24 cps\n" << "--------------------\n\n";

    INT_PTR result = DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG_CPS), NULL, DialogProc);
    if (result == -1) {
        DWORD error = GetLastError();
        char errorMsg[256];
        sprintf_s(errorMsg, "Failed to create dialog. Error code: %d", error);
        MessageBoxA(NULL, errorMsg, "Error", MB_ICONERROR);
        return -1;
    }
    else if (result != IDOK) {
        MessageBoxA(NULL, "Dialog cancelled. Exiting application.", "Information", MB_ICONINFORMATION);
        return 0;
    }

    vars::minecraft_window = FindWindowA("LWJGL", nullptr);
    if (!vars::minecraft_window) {
        std::cerr << "minecraft window not found!\n";
        return -1;
    }
    
    generate_delays(vars::cps);
    Mouse5Click();

    std::cout << "autoclicker started. press 'F1' to exit.\n";

    auto lastMouse5Click = std::chrono::steady_clock::now();
    const auto mouse5Cooldown = std::chrono::milliseconds(3000);
    auto lastToggleClick = std::chrono::steady_clock::now();
    const auto toggleCooldown = std::chrono::milliseconds(200);  // Prevent rapid toggling

    // Create a hidden window to handle system tray messages
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"WCAutoclickerClass";
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(0, L"WCAutoclickerClass", L"WC Autoclicker",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr,
        GetModuleHandle(nullptr), nullptr);

    CreateSystemTrayIcon(hwnd);
    
    // Create system tray menu
    vars::hMenu = CreatePopupMenu();
    AppendMenu(vars::hMenu, MF_STRING, 1, L"Exit");

    MSG msg;
    while (vars::running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        auto currentTime = std::chrono::steady_clock::now();

        // Toggle pause with Mouse4
        if ((GetAsyncKeyState(VK_XBUTTON1) & 0x8000) &&
            (currentTime - lastToggleClick > toggleCooldown)) {
            vars::paused = !vars::paused;
            lastToggleClick = currentTime;
            // Optional: Add some feedback (e.g., console output or system tray notification)
            std::cout << (vars::paused ? "Autoclicker paused" : "Autoclicker resumed") << std::endl;
        }

        if (!vars::paused) {
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                LeftMouseClick();
            }

            if ((GetAsyncKeyState(VK_XBUTTON2) & 0x8000) &&
                (currentTime - lastMouse5Click > mouse5Cooldown)) {
                Mouse5Click();
                lastMouse5Click = currentTime;
            }
        }

        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            vars::running = false;
        }

        nt::Sleep(1);
    }
    RemoveSystemTrayIcon();
    return 0;
}