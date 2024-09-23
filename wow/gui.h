#pragma once
#include <d3d9.h>
#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <atomic>
#include <vector>
#define GUI_H

namespace gui {
    extern std::atomic<bool> should_exit; 
    extern bool autoclicker_running;
    extern int cps;
    extern HWND window;
    extern WNDCLASSEXA windowClass;
    extern LPDIRECT3D9 d3d;
    extern LPDIRECT3DDEVICE9 d3dDevice;
    extern D3DPRESENT_PARAMETERS d3dParams;
    extern POINTS position;
    extern NOTIFYICONDATA nid;
    extern std::thread autoclicker_thread;
    extern std::vector<int> delays;

    void mouse5_click();
    
    // Window dimensions
    extern int width;
    extern int height;

    // Function declarations
    void generate_delays(const int cps);
    void rotate_delays();
    void left_mouse_click();
    //void mouse5_click();
    void autoclicker_function(int cps);  

    void createHWindow(const char* windowName, const char* className) noexcept;
    void destroyWindow() noexcept;

    bool createD3D9() noexcept;
    void resetD3D9() noexcept;
    void destroyD3D9() noexcept;

    void createImgui() noexcept;
    void destroyImgui() noexcept;

    void beginRender() noexcept;
    void endRender() noexcept;
    void render() noexcept;

    // Tray icon functions
    void createTrayIcon(HWND hwnd) noexcept;
    void destroyTrayIcon() noexcept;
    void handleTrayMessage(WPARAM wParam, LPARAM lParam) noexcept;

    void start_autoclicker();
    void stop_autoclicker();

    // Inline to avoid multiple definitions
    inline std::atomic<bool> running{ true };
}