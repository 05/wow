#include "gui.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    gui::should_exit = false;  

    
    gui::createHWindow("Autoclicker GUI", "AutoclickerClass");

    if (!gui::createD3D9()) {
        gui::destroyWindow();
        return 1;
    }

    
    gui::createImgui();

    HWND hwnd = gui::window;  
    gui::createTrayIcon(hwnd);  

  
    MSG msg;
    ZeroMemory(&msg, sizeof(MSG));
    while (!gui::should_exit) {  
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        
        if (msg.message == WM_QUIT) {
            gui::should_exit = true;
            break;
        }

        gui::beginRender();
        gui::render();
        gui::endRender();
    }

    
    gui::destroyImgui();
    gui::destroyD3D9();
    gui::destroyWindow();

    return 0;
}