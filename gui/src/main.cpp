#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>
#include <d3d11.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "app.h"
#include "style.h"
#include "ui_main.h"
#include "win_integration.h"
#include "utils.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static AppState g_state;
static DX11State g_dx;

static void sanitizeWindowRect(AppSettings& settings) {
    constexpr int kDefaultX = 100;
    constexpr int kDefaultY = 100;
    constexpr int kDefaultW = 1200;
    constexpr int kDefaultH = 760;
    constexpr int kMinW = 640;
    constexpr int kMinH = 420;

    if (settings.windowW <= 0 || settings.windowH <= 0 ||
        settings.windowW > 10000 || settings.windowH > 10000) {
        settings.windowW = kDefaultW;
        settings.windowH = kDefaultH;
    }
    if (settings.windowW < kMinW) settings.windowW = kMinW;
    if (settings.windowH < kMinH) settings.windowH = kMinH;

    auto centerOnPrimaryMonitor = [&]() {
        POINT origin = {0, 0};
        HMONITOR primary = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(primary, &mi)) {
            settings.windowX = kDefaultX;
            settings.windowY = kDefaultY;
            settings.windowW = kDefaultW;
            settings.windowH = kDefaultH;
            return;
        }

        int workW = mi.rcWork.right - mi.rcWork.left;
        int workH = mi.rcWork.bottom - mi.rcWork.top;
        if (settings.windowW > workW) settings.windowW = workW;
        if (settings.windowH > workH) settings.windowH = workH;
        settings.windowX = mi.rcWork.left + (workW - settings.windowW) / 2;
        settings.windowY = mi.rcWork.top + (workH - settings.windowH) / 2;
    };

    // -32000 is a common minimized window coordinate on Windows.
    if (settings.windowX <= -30000 || settings.windowY <= -30000) {
        centerOnPrimaryMonitor();
        return;
    }

    RECT windowRect = {
        settings.windowX,
        settings.windowY,
        settings.windowX + settings.windowW,
        settings.windowY + settings.windowH
    };
    HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONULL);
    if (!monitor) {
        centerOnPrimaryMonitor();
        return;
    }

    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        centerOnPrimaryMonitor();
        return;
    }

    const int workW = mi.rcWork.right - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    if (settings.windowW > workW) settings.windowW = workW;
    if (settings.windowH > workH) settings.windowH = workH;

    if (settings.windowX < mi.rcWork.left) settings.windowX = mi.rcWork.left;
    if (settings.windowY < mi.rcWork.top) settings.windowY = mi.rcWork.top;
    if (settings.windowX + settings.windowW > mi.rcWork.right) {
        settings.windowX = mi.rcWork.right - settings.windowW;
    }
    if (settings.windowY + settings.windowH > mi.rcWork.bottom) {
        settings.windowY = mi.rcWork.bottom - settings.windowH;
    }
}

static void saveWindowRect(AppSettings& settings, HWND hWnd) {
    RECT rect = {};
    bool validRect = false;

    WINDOWPLACEMENT placement = {};
    placement.length = sizeof(placement);
    if (GetWindowPlacement(hWnd, &placement)) {
        rect = placement.rcNormalPosition;
        validRect = (rect.right > rect.left && rect.bottom > rect.top);
    }

    if (!validRect && GetWindowRect(hWnd, &rect)) {
        validRect = (rect.right > rect.left && rect.bottom > rect.top);
    }

    if (!validRect) return;

    settings.windowX = rect.left;
    settings.windowY = rect.top;
    settings.windowW = rect.right - rect.left;
    settings.windowH = rect.bottom - rect.top;
}

static std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, result.data(), sz, nullptr, nullptr);
    return result;
}

static void handleDropFiles(HDROP hDrop) {
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::string> paths;
    for (UINT i = 0; i < count; ++i) {
        wchar_t buf[MAX_PATH] = {};
        DragQueryFileW(hDrop, i, buf, MAX_PATH);
        std::string path = wideToUtf8(buf);
        if (!path.empty()) paths.push_back(path);
    }
    DragFinish(hDrop);

    if (paths.empty()) return;

    // Add files to the state
    const int beforeCount = static_cast<int>(g_state.fileList.size());
    for (const auto& path : paths) {
        namespace fs = std::filesystem;
        try {
            fs::path fsPath = fsPathFromUtf8(path);
            if (!fs::exists(fsPath)) continue;
            if (fs::is_directory(fsPath)) {
                // Will be handled by addFiles in left panel
                // For now just add supported files
                continue;
            }
            if (!isSupportedFile(path)) continue;
            std::string fullPath = fsPathToUtf8(fs::absolute(fsPath));
            if (g_state.fileSet.count(fullPath)) continue;
            g_state.fileList.push_back(fullPath);
            g_state.fileSet.insert(fullPath);
        } catch (...) {}
    }

    const int added = static_cast<int>(g_state.fileList.size()) - beforeCount;
    if (added > 0) {
        g_state.selectedFileIndex = static_cast<int>(g_state.fileList.size()) - 1;
        // Single file dropped -> 预览; multiple -> 网格.
        g_state.rightViewMode = (added > 1) ? 0 : 1;
    } else if (!g_state.fileList.empty() && g_state.selectedFileIndex < 0) {
        g_state.selectedFileIndex = 0;
    }
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        if (g_dx.device) {
            CleanupRenderTarget(&g_dx);
            g_dx.swapChain->ResizeBuffers(0,
                static_cast<UINT>(LOWORD(lParam)),
                static_cast<UINT>(HIWORD(lParam)),
                DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget(&g_dx);
        }
        return 0;

    case WM_DROPFILES:
        handleDropFiles(reinterpret_cast<HDROP>(wParam));
        return 0;

    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;

    case WM_DPICHANGED: {
        RECT* suggested = reinterpret_cast<RECT*>(lParam);
        SetWindowPos(hWnd, nullptr,
                     suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);

        float newDpi = static_cast<float>(HIWORD(wParam)) / 96.0f;
        g_state.dpiScale = newDpi;
        ImGuiStyle style;
        ApplyImGuiStyle(newDpi);
        LoadFonts(newDpi);
        ImGui_ImplDX11_InvalidateDeviceObjects();
        return 0;
    }

    case WM_CLOSE: {
        saveWindowRect(g_state.settings, hWnd);
        g_state.settings.splitterPos = g_state.leftPanelWidth;
        g_state.settings.outputFormat = g_state.outputFormat;
        g_state.settings.quality = g_state.quality;
        g_state.settings.overwrite = g_state.overwrite;
        g_state.settings.recursive = g_state.recursive;
        g_state.settings.batchSizeMode = g_state.batchSizeMode;
        g_state.settings.batchWidth = g_state.batchWidth;
        g_state.settings.batchHeight = g_state.batchHeight;
        g_state.settings.batchLockAspect = g_state.batchLockAspect;
        g_state.settings.batchResizeMethod = g_state.batchResizeMethod;
        g_state.settings.lastInputDir = g_state.inputDirBuf;
        g_state.settings.lastOutputDir = g_state.outputDirBuf;
        g_state.settings.save();
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    wc.lpszClassName = L"BLPViewerClass";
    RegisterClassExW(&wc);

    // Load settings
    g_state.settings.load();
    sanitizeWindowRect(g_state.settings);

    // Create window
    HWND hwnd = CreateWindowExW(
        WS_EX_ACCEPTFILES,
        wc.lpszClassName,
        L"\x56fe\x50cf\x5feb\x901f\x5904\x7406\x5de5\x5177 1.4v - \x5c0f\x4e3a",
        WS_OVERLAPPEDWINDOW,
        g_state.settings.windowX, g_state.settings.windowY,
        g_state.settings.windowW, g_state.settings.windowH,
        nullptr, nullptr, hInstance, nullptr);

    // Enable drag-and-drop
    DragAcceptFiles(hwnd, TRUE);

    // Initialize DX11
    if (!CreateDeviceD3D(hwnd, &g_dx)) {
        CleanupDeviceD3D(&g_dx);
        UnregisterClassW(wc.lpszClassName, hInstance);
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // DPI
    float dpi = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
    g_state.dpiScale = dpi;

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // Disable imgui.ini

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_dx.device, g_dx.deviceContext);

    // Style and fonts
    ApplyImGuiStyle(dpi);
    LoadFonts(dpi);

    // Initialize app state
    g_state.hwnd = hwnd;
    g_state.device = g_dx.device;
    g_state.deviceContext = g_dx.deviceContext;
    g_state.leftPanelWidth = g_state.settings.splitterPos;
    g_state.outputFormat = g_state.settings.outputFormat;
    g_state.quality = g_state.settings.quality;
    g_state.overwrite = g_state.settings.overwrite;
    g_state.recursive = g_state.settings.recursive;
    g_state.batchSizeMode = g_state.settings.batchSizeMode;
    g_state.batchWidth = g_state.settings.batchWidth;
    g_state.batchHeight = g_state.settings.batchHeight;
    g_state.batchLockAspect = g_state.settings.batchLockAspect;
    g_state.batchResizeMethod = g_state.settings.batchResizeMethod;
    g_state.batchAspect = static_cast<float>(std::max(1, g_state.batchWidth)) /
                          static_cast<float>(std::max(1, g_state.batchHeight));
    if (!g_state.settings.lastInputDir.empty()) {
        std::strncpy(g_state.inputDirBuf, g_state.settings.lastInputDir.c_str(), sizeof(g_state.inputDirBuf) - 1);
    }
    if (!g_state.settings.lastOutputDir.empty()) {
        std::strncpy(g_state.outputDirBuf, g_state.settings.lastOutputDir.c_str(), sizeof(g_state.outputDirBuf) - 1);
    }

    // Initialize image viewer
    g_state.imageViewer.init(g_dx.device);

    // Check association status
    g_state.blpAssociated = isBlpAssociated();
    g_state.pngAssociated = isPngAssociated();
    g_state.tgaAssociated = isTgaAssociated();
    g_state.thumbnailRegistered = isThumbnailRegistered();

    // Handle command line arguments
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        std::vector<std::string> paths;
        for (int i = 1; i < argc; ++i) {
            std::string path = wideToUtf8(argv[i]);
            if (!path.empty()) paths.push_back(path);
        }
        LocalFree(argv);

        for (const auto& path : paths) {
            namespace fs = std::filesystem;
            try {
                fs::path fsPath = fsPathFromUtf8(path);
                if (!fs::exists(fsPath)) continue;
                if (!isSupportedFile(path)) continue;
                std::string fullPath = fsPathToUtf8(fs::absolute(fsPath));
                if (g_state.fileSet.count(fullPath)) continue;
                g_state.fileList.push_back(fullPath);
                g_state.fileSet.insert(fullPath);
            } catch (...) {}
        }
        if (!g_state.fileList.empty()) {
            g_state.selectedFileIndex = 0;
            // Opened with a single file (e.g. file association) -> 预览; more -> 网格.
            g_state.rightViewMode = (g_state.fileList.size() > 1) ? 0 : 1;
        }
    }

    // Main loop
    const float clearColor[4] = {0.961f, 0.965f, 0.973f, 1.0f};
    bool done = false;

    while (!done) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Render main UI
        renderMainUI(g_state);

        // Rendering
        ImGui::Render();
        g_dx.deviceContext->OMSetRenderTargets(1, &g_dx.renderTargetView, nullptr);
        g_dx.deviceContext->ClearRenderTargetView(g_dx.renderTargetView, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_dx.swapChain->Present(1, 0); // VSync
    }

    // Cleanup
    g_state.thumbCache.clear();
    g_state.imageViewer.release();

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D(&g_dx);
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, hInstance);

    return 0;
}
