#pragma once

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif

#include "d3dUtil.h"
#include "GameTimer.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")


using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

class Window
{
protected:

    Window(HINSTANCE hInstance);
    Window(const Window& rhs) = delete;
    Window& operator=(const Window& rhs) = delete;
    virtual ~Window();

public:
    static Window* GetWindow();

    HINSTANCE WindowInst()const;
    HWND      MainWnd()const;
    float     AspectRatio()const;

    int Run();

    virtual bool Initialize();
    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
    virtual void OnResize();
    virtual void Update(const GameTimer& gt) = 0;
    virtual void Draw(const GameTimer& gt) = 0;

    // Convenience overrides for handling mouse input.
    virtual void OnMouseDown(WPARAM btnState, int x, int y) { }
    virtual void OnMouseUp(WPARAM btnState, int x, int y) { }
    virtual void OnMouseMove(WPARAM btnState, int x, int y) { }

protected:

    bool InitMainWindow();

    // FPS와 시간 계산 함수
    void CalculateFrameStats();

protected:

    static Window* mWindow;

    HINSTANCE mhWindowInst = nullptr; // Windowlication instance handle
    HWND      mhMainWnd = nullptr; // main window handle
    bool      mWindowPaused = false;  // is the Windowlication paused?
    bool      mMinimized = false;  // is the Windowlication minimized?
    bool      mMaximized = false;  // is the Windowlication maximized?
    bool      mResizing = false;   // are the resize bars being dragged?
    bool      mFullscreenState = false;// fullscreen enabled

    // Set true to use 4X MSAA (?.1.8).  The default is false.
    bool      m4xMsaaState = false;    // 4X MSAA enabled
    UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

    // Used to keep track of the 밺elta-time?and game time (?.4).
    GameTimer mTimer;

    wstring mMainWndCaption = L"DirectX12 Rendering";
    int mClientWidth = 1920;
    int mClientHeight = 1200;
};