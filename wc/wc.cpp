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

namespace {
    int cps = 0;
    HWND minecraft_window{};
    std::atomic<bool> running{ true };
    std::vector<int> delays;
    NOTIFYICONDATA nid = {};
    HMENU menu = nullptr;
    std::vector<int> right_click_delays;
    std::atomic<bool> paused{ false };
}

namespace math {
    int get_random_int(const int min, const int max) {
        thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(min, max);
        return distribution(generator);
    }
}

namespace nt {
    typedef NTSTATUS(NTAPI* NtDelayExecutionFunc)(BOOLEAN alert, PLARGE_INTEGER delay_interval);
    NtDelayExecutionFunc nt_delay_execution = nullptr;

    void sleep(const LONGLONG delay) {
        if (!nt_delay_execution) {
            const HMODULE ntdll = GetModuleHandleA("ntdll.dll");

            if (ntdll)
                nt_delay_execution = reinterpret_cast<NtDelayExecutionFunc>(
                    GetProcAddress(ntdll, "NtDelayExecution"));
        }

        if (nt_delay_execution) {
            LARGE_INTEGER li;
            li.QuadPart = -delay * 10000LL;
            nt_delay_execution(FALSE, &li);
        }
        else {
            ::Sleep(static_cast<DWORD>(delay));
        }
    }
}

void generate_delays(const int cps) {
    delays.clear();
    const double base_delay = 1000.0 / cps;
    const double variation_range = base_delay * 0.1;

    for (int x = 0; x < 5000; x++) {
        const double variation = math::get_random_int(-100, 100) * variation_range / 100.0;
        int delay = (std::max)(1, static_cast<int>(std::round(base_delay + variation)));
        delays.push_back(delay);
    }
}

void rotate_delays() {
    std::rotate(delays.begin(), delays.begin() + 1, delays.end());
}

void left_mouse_click() {
    PostMessageA(minecraft_window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
    PostMessageA(minecraft_window, WM_LBUTTONUP, MK_LBUTTON, 0);
    nt::sleep(delays.front());
    rotate_delays();
}

void generate_right_click_delays(int cps) {
    right_click_delays.clear();
    const double base_delay = 1000.0 / (cps * 0.7);  // 70% of the left-click speed (30% slower)
    const double variation_range = base_delay * 0.1;

    for (int x = 0; x < 5000; x++) {
        const double variation = math::get_random_int(-100, 100) * variation_range / 100.0;
        int delay = (std::max)(1, static_cast<int>(std::round(base_delay + variation)));
        right_click_delays.push_back(delay);
    }
}

void rotate_right_click_delays() {
    std::rotate(right_click_delays.begin(), right_click_delays.begin() + 1, right_click_delays.end());
}

void right_mouse_click() {
    PostMessageA(minecraft_window, WM_RBUTTONDOWN, MK_RBUTTON, 0);
    PostMessageA(minecraft_window, WM_RBUTTONUP, MK_RBUTTON, 0);
    nt::sleep(static_cast<LONGLONG>(right_click_delays.front()));
    rotate_right_click_delays();
}

void mouse5_click() {
    keybd_event('2', 0, 0, 0);
    nt::sleep(5);
    keybd_event('2', 0, KEYEVENTF_KEYUP, 0);
    nt::sleep(5);
    PostMessageA(minecraft_window, WM_RBUTTONDOWN, MK_RBUTTON, 0);
    nt::sleep(1);
    PostMessageA(minecraft_window, WM_RBUTTONUP, MK_RBUTTON, 0);
    nt::sleep(20);  // Increased delay after right-click
    keybd_event('1', 0, 0, 0);
    nt::sleep(10);
    keybd_event('1', 0, KEYEVENTF_KEYUP, 0);
    nt::sleep(5);
}

void RemoveSystemTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

void CreateSystemTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_USER + 1;

    // Load the custom icon
    HICON hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_TRAYICON));
    if (hIcon) {
        nid.hIcon = hIcon;
    }
    else {
        // Fallback to default application icon if custom icon fails to load
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    wcscpy_s(nid.szTip, L"WC Autoclicker");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_USER + 1:
        // Handle system tray messages here
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            const UINT clicked = TrackPopupMenu(
                menu,
                TPM_RETURNCMD | TPM_NONOTIFY,
                pt.x, pt.y,
                0,
                hwnd,
                nullptr
            );

            if (clicked == 1) {
                running = false;
                PostQuitMessage(0);
            }
        }
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default: break;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

INT_PTR CALLBACK dialog_proc(const HWND hwnd_dlg, const UINT u_msg, const WPARAM w_param, LPARAM l_param)
{
    static HBRUSH h_brush_background = CreateSolidBrush(RGB(0, 0, 0));

    switch (u_msg)
    {
    case WM_INITDIALOG:
    {
        // Set the background color to black for the dialog
        SetClassLongPtr(hwnd_dlg, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(h_brush_background));

        // Set the text color to white for all child controls
        EnumChildWindows(hwnd_dlg, [](HWND hwnd, LPARAM l_param) -> BOOL {
            SetWindowLongPtr(hwnd, GWL_STYLE, GetWindowLongPtr(hwnd, GWL_STYLE) | WS_CHILD);
            return true;
            }, 0);

        return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    {
        const auto hdc_static = reinterpret_cast<HDC>(w_param);
        SetTextColor(hdc_static, RGB(255, 255, 255));  // white text
        SetBkColor(hdc_static, RGB(0, 0, 0));  // gray background
        return reinterpret_cast<INT_PTR>(h_brush_background);
    }

    case WM_COMMAND:
        switch (LOWORD(w_param))
        {
        case IDOK:
            // Get the CPS value from the edit control
            cps = GetDlgItemInt(hwnd_dlg, IDC_EDIT_CPS, NULL, FALSE);
            EndDialog(hwnd_dlg, IDOK);
            return TRUE;

        case IDCANCEL:
            EndDialog(hwnd_dlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_DESTROY:
        DeleteObject(h_brush_background);
        return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    std::cout << "wc -- by big y\n\n";
    std::cout << "--------------------\n" << "CPS CONVERSION CHART\n";
    std::cout << "   20 = 12-14 cps\n   30 = 17-19 cps\n   40 = 21-23 cps\n   50 = 22-24 cps\n" << "--------------------\n\n";

    const INT_PTR result = DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG_CPS), NULL, dialog_proc);
    if (result == -1) {
        const DWORD error = GetLastError();
        char errorMsg[256];
        sprintf_s(errorMsg, "Failed to create dialog. Error code: %d", error);
        MessageBoxA(nullptr, errorMsg, "Error", MB_ICONERROR);
        return -1;
    }
    if (result != IDOK) {
        MessageBoxA(nullptr, "Dialog cancelled. Exiting application.", "Information", MB_ICONINFORMATION);
        return 0;
    }

    minecraft_window = FindWindowA("LWJGL", nullptr);
    if (!minecraft_window) {
        std::cerr << "minecraft window not found!\n";
        return -1;
    }

    generate_delays(cps);
    mouse5_click();

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
    menu = CreatePopupMenu();
    AppendMenu(menu, MF_STRING, 1, L"Exit");

    MSG msg;
    while (running) {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        auto current_time = std::chrono::steady_clock::now();

        // Toggle pause with Mouse4
        if ((GetAsyncKeyState(VK_XBUTTON1) & 0x8000) &&
            (current_time - lastToggleClick > toggleCooldown)) {
            paused = !paused;
            lastToggleClick = current_time;
            // Optional: Add some feedback (e.g., console output or system tray notification)
            std::cout << (paused ? "Autoclicker paused" : "Autoclicker resumed") << std::endl;
        }

        if (!paused) {
            if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
                left_mouse_click();
            }

            if ((GetAsyncKeyState(VK_XBUTTON2) & 0x8000) &&
                (current_time - lastMouse5Click > mouse5Cooldown)) {
                mouse5_click();
                lastMouse5Click = current_time;
            }
        }

        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            running = false;
        }

        nt::sleep(1);
    }
    RemoveSystemTrayIcon();
    return 0;
}
