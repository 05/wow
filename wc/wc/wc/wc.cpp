#include <iostream>
#include <windows.h>
#include <random>
#include <thread>
#include <atomic>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>

#pragma comment(lib, "winmm.lib")

namespace vars {
    int cps{ 0 };
    HWND minecraft_window{};
    std::atomic<bool> running{ true };
    std::vector<int> delays;
    std::atomic<bool> paused{ false }; // variable to track pause state
    bool previousMouse4State{ false }; // to detect Mouse4 button press
}

namespace math {
    int get_random_int(int min, int max) {
        static thread_local std::mt19937 generator(std::random_device{}());
        std::uniform_int_distribution<int> distribution(min, max);
        return distribution(generator);
    }

    double get_variance(const std::vector<double>& data) {
        double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
        double sq_sum = std::inner_product(data.begin(), data.end(), data.begin(), 0.0,
            std::plus<>(), [mean](double a, double b) { return std::pow(a - mean, 2); });
        return sq_sum / data.size();
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
    double variation_range = base_delay * 0.1; // 10% variation

    std::vector<double> temp_delays;
    do {
        temp_delays.clear();
        for (int x = 0; x < 5000; x++) {
            double variation = math::get_random_int(-100, 100) * variation_range / 100.0;
            double delay = (std::max)(1.0, base_delay + variation);
            temp_delays.push_back(delay);
        }
    } while (math::get_variance(temp_delays) < 1); // reduced variance threshold

    // normalize delays to ensure average CPS is correct
    double total_delay = std::accumulate(temp_delays.begin(), temp_delays.end(), 0.0);
    double actual_cps = 5000.0 / (total_delay / 1000.0);
    double scale_factor = static_cast<double>(cps) / actual_cps;

    for (const auto& delay : temp_delays) {
        int adjusted_delay = static_cast<int>(std::round(delay * scale_factor));
        vars::delays.push_back((std::max)(1, adjusted_delay)); // Ensure minimum 1ms delay
    }
}

void rotate_delays() {
    std::rotate(vars::delays.begin(), vars::delays.begin() + 1, vars::delays.end());
}

void LeftMouseClick() {
    PostMessageA(vars::minecraft_window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(0, 0));
    PostMessageA(vars::minecraft_window, WM_LBUTTONUP, MK_LBUTTON, MAKELPARAM(0, 0));

    nt::Sleep(static_cast<LONGLONG>(vars::delays.front()));
    rotate_delays();
}

void Mouse5Click() {
    // press 2 key
    keybd_event('2', 0, 0, 0);
    nt::Sleep(20);
    keybd_event('2', 0, KEYEVENTF_KEYUP, 0);
    nt::Sleep(10);

    // right click
    PostMessageA(vars::minecraft_window, WM_RBUTTONDOWN, MK_RBUTTON, MAKELPARAM(0, 0));
    nt::Sleep(10);
    PostMessageA(vars::minecraft_window, WM_RBUTTONUP, MK_RBUTTON, MAKELPARAM(0, 0));
    nt::Sleep(20);

    // press 1 key
    keybd_event('1', 0, 0, 0);
    nt::Sleep(20);
    keybd_event('1', 0, KEYEVENTF_KEYUP, 0);
    nt::Sleep(20);
}

int main() {
    using namespace vars;

    std::cout << "   wc -- by big y\n";
    std::cout << "--------------------\n" << "CPS CONVERSION CHART\n";
    std::cout << "   20 = 12-14 cps\n   30 = 17-19 cps\n   40 = 21-23 cps\n   50 = 22-24 cps\n--------------------\n\n";

    std::cout << "enter your desired cps: ";
    std::cin >> cps;

    minecraft_window = FindWindowA("LWJGL", nullptr);
    if (!minecraft_window) {
        std::cerr << "minecraft window not found!\n";
        return -1;
    }

    generate_delays(cps);
    Mouse5Click();

    std::cout << "autoclicker started. press 'f1' to exit, 'mouse4' to pause/unpause.\n";

    auto lastMouse5Click = std::chrono::steady_clock::now();
    const auto mouse5Cooldown = std::chrono::milliseconds(3000);

    while (running) {
        auto currentTime = std::chrono::steady_clock::now();

        // check for pause/unpause key (Mouse4)
        bool currentMouse4State = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
        if (currentMouse4State && !previousMouse4State) {
            paused = !paused;
        }
        previousMouse4State = currentMouse4State;

        // only perform clicks if not paused
        if (!paused) {
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
            running = false;
        }

        nt::Sleep(1LL);
    }

    std::cout << "wc stopped.\n";
    return 0;
}