#include "gui.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx9.h"
#include "../imgui/imgui_impl_win32.h"
#include <d3d9.h>
#include <thread>
#include <atomic>
#include <vector>
#include <windows.h>
#include <random>
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <winternl.h>

#define IDM_EXIT 1001
#define IDM_RESTORE 1002
#define IDD_DIALOG_CPS 101
#define IDC_EDIT_CPS 1001
#define IDI_TRAYICON 102
#define IDI_ICON1 101  

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
	HWND hWnd, 
	UINT msg, 
	WPARAM wParam, 
	LPARAM lParam
);
namespace math {
	int get_random_int(const int min, const int max) {
		thread_local std::mt19937 generator(std::random_device{}());
		std::uniform_int_distribution<int> distribution(min, max);
		return distribution(generator);
	}
}
namespace gui {
    HWND window = nullptr;
    std::atomic<bool> should_exit{ false };
	std::vector<int> delays;
	std::atomic<bool> paused{ false };
    bool autoclicker_running = false;
    int cps = 0;
    WNDCLASSEXA windowClass = { };
    LPDIRECT3D9 d3d = nullptr;
    LPDIRECT3DDEVICE9 d3dDevice = nullptr;
    D3DPRESENT_PARAMETERS d3dParams = { };
    POINTS position = { };
    NOTIFYICONDATA nid = { }; 

    void createTrayIcon(HWND hwnd) noexcept {
        ZeroMemory(&nid, sizeof(nid)); 
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_USER + 1;

        HICON hIcon = LoadIcon(windowClass.hInstance, MAKEINTRESOURCE(IDI_TRAYICON));
        if (hIcon) {
            nid.hIcon = hIcon;
        }
        else {
            nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        }

        wcscpy_s(nid.szTip, L"WC Autoclicker");

        Shell_NotifyIcon(NIM_ADD, &nid);
    }
    std::thread autoclicker_thread;


    int width = 333;   
    int height = 160;  

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
namespace {
    HWND minecraft_window{};
    std::atomic<bool> running{ false };
    std::vector<int> delays;
    std::atomic<bool> paused{ false };

    void left_mouse_click() {
        if (minecraft_window != nullptr) {
            PostMessageA(minecraft_window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
            PostMessageA(minecraft_window, WM_LBUTTONUP, MK_LBUTTON, 0);
            nt::sleep(delays.front());
            gui::rotate_delays();
        }
    }
}
HWND find_minecraft_window() {
    HWND hwnd = FindWindowA("LWJGL", nullptr); 
    if (hwnd == nullptr) {
        MessageBoxA(nullptr, "Minecraft window not found!", "Error", MB_ICONERROR | MB_OK);
    }
    return hwnd;
}
LRESULT CALLBACK WindowProcess(
    HWND hwnd,
    UINT message,
    WPARAM wideParameter,
    LPARAM longParameter)
{
	if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wideParameter, longParameter))
		return true;

	switch(message)
	{
	case WM_SIZE: {
		if (gui::d3dDevice && wideParameter != SIZE_MINIMIZED)
		{
			gui::d3dParams.BackBufferWidth = LOWORD(longParameter);
			gui::d3dParams.BackBufferHeight = HIWORD(longParameter);
			gui::resetD3D9();
		}
		return 0;
	}

	case WM_SYSCOMMAND: {
		if ((wideParameter & 0xfff0) == SC_KEYMENU)
			return 0;
		break;
	}

	case WM_LBUTTONDOWN: {
		gui::position = MAKEPOINTS(longParameter);
		break;
	} return 0;
	

	case WM_MOUSEMOVE: {
		if (wideParameter == MK_LBUTTON)
		{
			const auto points = MAKEPOINTS(longParameter);
			auto rect = ::RECT{ };

			GetWindowRect(gui::window, &rect);
			rect.left += points.x - gui::position.x;
			rect.top += points.y - gui::position.y;

			if (gui::position.x >= 0 &&
				gui::position.x <= gui::d3dParams.BackBufferWidth &&
				gui::position.y >= 0 && gui::position.y <= 19)
			{
				SetWindowPos(
					gui::window,
					HWND_TOPMOST,
					rect.left,
					rect.top,
					0, 0,
					SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOZORDER
				);
			}
		}
		return 0;
	}

	case WM_USER + 1:
		gui::handleTrayMessage(wideParameter, longParameter);
		return 0;

	case WM_COMMAND:
        if (LOWORD(wideParameter) == IDM_EXIT) {
            gui::should_exit = false;  
            DestroyWindow(hwnd);
            return 0;
        }
        else if (LOWORD(wideParameter) == IDM_RESTORE) {
            ShowWindow(gui::window, SW_SHOW);
            SetForegroundWindow(gui::window);
            return 0;
        }
        break;
     case WM_DESTROY:
        gui::should_exit = false;
        PostQuitMessage(0);
        return 0;
	default:
		break;
	}

	return DefWindowProcA(hwnd, message, wideParameter, longParameter);
}

void gui::createHWindow(
    const char* windowName,
    const char* className) noexcept
{
    windowClass.cbSize = sizeof(WNDCLASSEXA);
    windowClass.style = CS_CLASSDC;
    windowClass.lpfnWndProc = WindowProcess;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = GetModuleHandleA(0);

    

    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = 0;
    windowClass.lpszMenuName = 0;
    windowClass.lpszClassName = className;

    if (!RegisterClassExA(&windowClass)) {
        MessageBoxA(NULL, "Window Registration Failed!", "Error", MB_ICONEXCLAMATION | MB_OK);
        return;
    }

    
    DWORD style = WS_POPUP | WS_SYSMENU;
    window = CreateWindowExA(
        0,
        windowClass.lpszClassName,
        windowName,
        style,
        100, 100, 
        width, height,
        nullptr,
        nullptr,
        windowClass.hInstance,
        nullptr
    );

    SendMessage(window, WM_SETICON, ICON_BIG, (LPARAM)windowClass.hIcon);
    SendMessage(window, WM_SETICON, ICON_SMALL, (LPARAM)windowClass.hIconSm);

    ShowWindow(window, SW_SHOWDEFAULT);
    UpdateWindow(window);
    createTrayIcon(window);
}

	void gui::destroyWindow() noexcept
	{
		stop_autoclicker();
		running = false;
		if (autoclicker_thread.joinable()) {
			autoclicker_thread.join();
		}

		destroyTrayIcon();
		DestroyWindow(window);
		UnregisterClassA(windowClass.lpszClassName, windowClass.hInstance);
	}

	bool gui::createD3D9() noexcept
	{
		d3d = Direct3DCreate9(D3D_SDK_VERSION);
		if (!d3d) 
			return false;

		ZeroMemory(&d3dParams, sizeof(d3dParams));
		
		d3dParams.Windowed = TRUE;
		d3dParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
		d3dParams.BackBufferFormat = D3DFMT_UNKNOWN;
		d3dParams.EnableAutoDepthStencil = TRUE;
		d3dParams.AutoDepthStencilFormat = D3DFMT_D16;
		d3dParams.PresentationInterval = D3DPRESENT_INTERVAL_ONE;

		if (d3d->CreateDevice(
			D3DADAPTER_DEFAULT,
			D3DDEVTYPE_HAL,
			window,
			D3DCREATE_HARDWARE_VERTEXPROCESSING,
			&d3dParams,
			&d3dDevice) < 0)
			return false;

		return true;
	}

	void gui::resetD3D9() noexcept
	{
		ImGui_ImplDX9_InvalidateDeviceObjects();

		const auto result = d3dDevice->Reset(&d3dParams);

		if (result == D3DERR_INVALIDCALL)
			IM_ASSERT(0);

		ImGui_ImplDX9_CreateDeviceObjects();
	}

	void gui::destroyD3D9() noexcept
	{
		if (d3dDevice)
		{
			d3dDevice->Release();
			d3dDevice = nullptr;
		}

		if (d3d)
		{
			d3d->Release();
			d3d = nullptr;
		}
	}

	void gui::createImgui() noexcept
	{

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();

		io.IniFilename = NULL;

		ImGui::StyleColorsDark();

		ImGui_ImplWin32_Init(window);
		ImGui_ImplDX9_Init(d3dDevice);

	}
	
	void gui::destroyImgui() noexcept
	{
		ImGui_ImplDX9_Shutdown();
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();	
	}

	void gui::beginRender() noexcept
	{
		MSG message;
		while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&message);
			DispatchMessageA(&message);
		}

		ImGui_ImplWin32_NewFrame();
		ImGui_ImplDX9_NewFrame();
		ImGui::NewFrame();
	}

	void gui::endRender() noexcept
	{
		ImGui::EndFrame();

		d3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
		d3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
		d3dDevice->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

		d3dDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_RGBA(0, 0, 0, 255), 1.0f, 0);
		
		if (d3dDevice->BeginScene() >= 0)
		{
			ImGui::Render();
			ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
			d3dDevice->EndScene();
		}

		
		RECT rect = { 0, 0, width, height };
		const auto result = d3dDevice->Present(&rect, &rect, nullptr, nullptr);

		if (result == D3DERR_DEVICELOST && d3dDevice->TestCooperativeLevel() == D3DERR_DEVICENOTRESET)
			resetD3D9();
	}

	void gui::render() noexcept {
    ImGui::SetNextWindowPos({ 0, 0 });
    ImGui::SetNextWindowSize({ static_cast<float>(width), static_cast<float>(height) });
    ImGui::Begin(
        "wowclicker",
        nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove
    );

    static char cpsInput[4] = "0"; // 0 Default value
    ImGui::InputText("cps", cpsInput, IM_ARRAYSIZE(cpsInput), ImGuiInputTextFlags_CharsDecimal);

    // Start Button
    if (ImGui::Button("start clicker")) {
        int inputCps = std::atoi(cpsInput);
        if (inputCps > 0 && inputCps <= 100) { 
            cps = inputCps;
            if (!autoclicker_running) {
                start_autoclicker();
            }
        } else {
            
            ImGui::OpenPopup("cps error");
        }
    }

    // Error Popup
    if (ImGui::BeginPopupModal("cps error", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("please enter a CPS value between 1 and 100.");
        if (ImGui::Button("OK", ImVec2(120, 0))) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    // Stop Button
    if (ImGui::Button("stop clicker")) {
        if (autoclicker_running) {
            stop_autoclicker();
        }
    }

    // Display current status
    const char* status = "stopped";
    if (autoclicker_running) {
        status = paused ? "paused" : "running";
    }
    ImGui::Text("clicker status: %s", status);
    ImGui::Text("current cps: %d", cps);

    // Minimize button
    if (ImGui::Button("minimize")) {
        ShowWindow(window, SW_HIDE);
    }

    ImGui::End();
}

void gui::destroyTrayIcon() noexcept
{
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (nid.hIcon != NULL) {
        DestroyIcon(nid.hIcon);
    }
}

void gui::handleTrayMessage(WPARAM wParam, LPARAM lParam) noexcept 
{
    if (lParam == WM_RBUTTONUP)
    {
        POINT curPoint;
        GetCursorPos(&curPoint);
        SetForegroundWindow(window);

        HMENU hMenu = CreatePopupMenu();
        InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_EXIT, TEXT("exit"));
        InsertMenu(hMenu, 0, MF_BYPOSITION | MF_STRING, IDM_RESTORE, TEXT("restore menu"));

        SetMenuDefaultItem(hMenu, IDM_RESTORE, FALSE);

        TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_BOTTOMALIGN,
            curPoint.x, curPoint.y, 0, window, NULL);
        DestroyMenu(hMenu);
    }
    else if (lParam == WM_LBUTTONUP)
    {
        // Restore Window
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
    }
}
void gui::left_mouse_click() { 
    if (minecraft_window != nullptr) {
        PostMessageA(minecraft_window, WM_LBUTTONDOWN, MK_LBUTTON, 0);
        PostMessageA(minecraft_window, WM_LBUTTONUP, MK_LBUTTON, 0);
        nt::sleep(delays.front());
        rotate_delays();
    }
}
void gui::mouse5_click() { // Perfect Delays (1.8.9)
    keybd_event('2', 0, 0, 0);
    nt::sleep(10);
    keybd_event('2', 0, KEYEVENTF_KEYUP, 0);
    nt::sleep(10);
    PostMessageA(minecraft_window, WM_RBUTTONDOWN, MK_RBUTTON, 0);
    nt::sleep(15);
    PostMessageA(minecraft_window, WM_RBUTTONUP, MK_RBUTTON, 0);
    nt::sleep(45);  
    keybd_event('1', 0, 0, 0);
    nt::sleep(30);
    keybd_event('1', 0, KEYEVENTF_KEYUP, 0);
    nt::sleep(10);
}
void gui::autoclicker_function(int cps) {
    running = true;
    generate_delays(cps);
    mouse5_click();

    auto lastMouse5Click = std::chrono::steady_clock::now();
    const auto mouse5Cooldown = std::chrono::milliseconds(3000);

    minecraft_window = FindWindowA("LWJGL", nullptr);
    if (minecraft_window == nullptr) {
        MessageBoxA(nullptr, "Minecraft window not found!", "Error", MB_ICONERROR | MB_OK);
        autoclicker_running = false;
        running = false;
        return;
    }

    while (running) {
        // Check for mouse4 button press to toggle pause
        if ((GetAsyncKeyState(VK_XBUTTON1) & 0x1) != 0) {
            paused = !paused; 
            nt::sleep(200);   
        }

        if (paused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        auto current_time = std::chrono::steady_clock::now();

        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
            left_mouse_click();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        if ((GetAsyncKeyState(VK_XBUTTON2) & 0x8000) &&
            (current_time - lastMouse5Click > mouse5Cooldown)) {
            mouse5_click();
            lastMouse5Click = current_time;
        }

        // allow the thread to be stopped
        if (!autoclicker_running) {
            running = false;
        }
    }
}

void gui::start_autoclicker() {
    if (!autoclicker_running) {
        autoclicker_running = true;
        autoclicker_thread = std::thread(autoclicker_function, cps);
    }
}

void gui::stop_autoclicker() {
    if (autoclicker_running) {
        autoclicker_running = false;
        running = false;
        if (autoclicker_thread.joinable()) {
            autoclicker_thread.join();
        }
    }
}