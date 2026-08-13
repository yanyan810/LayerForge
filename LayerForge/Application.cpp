#include "Application.h"

#include "ThirdParty/imgui/backends/imgui_impl_win32.h"

#include <commdlg.h>
#include <filesystem>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

bool Application::Initialize(HINSTANCE instance, int showCommand) {
    const wchar_t* className = L"LayerForgeWindow";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = className;
    if (!RegisterClassExW(&wc)) { error_ = "Could not register the LayerForge window class."; return false; }

    RECT rect{ 0, 0, 1280, 800 };
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window_ = CreateWindowExW(0, className, L"LayerForge", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, instance, this);
    if (!window_) { error_ = "Could not create the LayerForge window."; return false; }

    if (!graphics_.Initialize(window_, 1280, 800, error_)) return false;
    if (!editorUI_.Initialize(window_, graphics_, error_)) return false;
    initialized_ = true;
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

int Application::Run(HINSTANCE instance, int showCommand) {
    HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (!Initialize(instance, showCommand)) {
        MessageBoxA(nullptr, error_.c_str(), "LayerForge initialization error", MB_OK | MB_ICONERROR);
        Shutdown();
        if (SUCCEEDED(comResult)) CoUninitialize();
        return 1;
    }

    MSG message{};
    while (message.message != WM_QUIT) {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessage(&message); continue; }
        if (IsIconic(window_)) { WaitMessage(); continue; }
        editorUI_.BeginFrame();
        editorUI_.Draw(image_.IsValid() ? &image_ : nullptr, texture_.IsValid() ? &texture_ : nullptr, error_, [this] { OpenImage(); });
        graphics_.BeginFrame();
        editorUI_.Render(graphics_.CommandList());
        graphics_.EndFrame();
    }
    Shutdown();
    if (SUCCEEDED(comResult)) CoUninitialize();
    return static_cast<int>(message.wParam);
}

void Application::OpenImage() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{ sizeof(dialog) };
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"Image Files (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0PNG Files (*.png)\0*.png\0JPEG Files (*.jpg;*.jpeg)\0*.jpg;*.jpeg\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrDefExt = L"png";
    if (!GetOpenFileNameW(&dialog)) return;

    ImageData candidate;
    std::string loadError;
    if (!imageLoader_.Load(std::filesystem::path(path), candidate, loadError)) { error_ = std::move(loadError); return; }
    if (!graphics_.CreateTexture(candidate, texture_, loadError)) { error_ = std::move(loadError); return; }
    image_ = std::move(candidate);
    error_.clear();
}

void Application::OnResize(uint32_t width, uint32_t height) { if (initialized_) graphics_.Resize(width, height); }

void Application::Shutdown() {
    if (initialized_) { graphics_.WaitForGpu(); editorUI_.Shutdown(); texture_ = {}; graphics_.Shutdown(); initialized_ = false; }
    if (window_) { DestroyWindow(window_); window_ = nullptr; }
}

LRESULT CALLBACK Application::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam)) return TRUE;
    Application* app = reinterpret_cast<Application*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<Application*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    switch (message) {
    case WM_SIZE: if (app && wParam != SIZE_MINIMIZED) app->OnResize(LOWORD(lParam), HIWORD(lParam)); return 0;
    case WM_GETMINMAXINFO: reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = { 640, 480 }; return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
