#include "app/platform_win32_dx11.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace app::platform {
namespace {

static Win32Dx11Context* g_ctx = nullptr;

void CleanupRenderTarget(Win32Dx11Context& ctx) {
    if (ctx.main_render_target_view) {
        ctx.main_render_target_view->Release();
        ctx.main_render_target_view = nullptr;
    }
}

void CreateRenderTarget(Win32Dx11Context& ctx) {
    ID3D11Texture2D* back_buffer = nullptr;
    ctx.swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer) {
        ctx.device->CreateRenderTargetView(back_buffer, nullptr, &ctx.main_render_target_view);
        back_buffer->Release();
    }
}

bool CreateDeviceD3D(HWND hwnd, Win32Dx11Context& ctx) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT create_device_flags = 0;
    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL feature_level_array[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_device_flags,
        feature_level_array,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &ctx.swap_chain,
        &ctx.device,
        &feature_level,
        &ctx.device_context);

    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            create_device_flags,
            feature_level_array,
            2,
            D3D11_SDK_VERSION,
            &sd,
            &ctx.swap_chain,
            &ctx.device,
            &feature_level,
            &ctx.device_context);
    }

    if (res != S_OK) {
        return false;
    }

    CreateRenderTarget(ctx);
    return true;
}

void CleanupDeviceD3D(Win32Dx11Context& ctx) {
    CleanupRenderTarget(ctx);
    if (ctx.swap_chain) { ctx.swap_chain->Release(); ctx.swap_chain = nullptr; }
    if (ctx.device_context) { ctx.device_context->Release(); ctx.device_context = nullptr; }
    if (ctx.device) { ctx.device->Release(); ctx.device = nullptr; }
}

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_SIZE:
        if (g_ctx && g_ctx->device != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget(*g_ctx);
            g_ctx->swap_chain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget(*g_ctx);
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

}  // namespace

bool Initialize(HINSTANCE h_instance, int n_cmd_show, Win32Dx11Context& out_ctx) {
    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        h_instance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"AssuranceForgeClass",
        nullptr
    };

    RegisterClassExW(&wc);

    out_ctx.hwnd = CreateWindowW(
        wc.lpszClassName,
        L"Assurance Forge",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1280, 720,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!out_ctx.hwnd) {
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    if (!CreateDeviceD3D(out_ctx.hwnd, out_ctx)) {
        CleanupDeviceD3D(out_ctx);
        DestroyWindow(out_ctx.hwnd);
        out_ctx.hwnd = nullptr;
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return false;
    }

    g_ctx = &out_ctx;

    ShowWindow(out_ctx.hwnd, n_cmd_show);
    UpdateWindow(out_ctx.hwnd);

    return true;
}

void Shutdown(Win32Dx11Context& ctx) {
    CleanupDeviceD3D(ctx);

    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (ctx.hwnd) {
        DestroyWindow(ctx.hwnd);
        ctx.hwnd = nullptr;
    }
    UnregisterClassW(L"AssuranceForgeClass", instance);

    g_ctx = nullptr;
}

bool InitializeImGuiBackends(const Win32Dx11Context& ctx) {
    return ImGui_ImplWin32_Init(ctx.hwnd) && ImGui_ImplDX11_Init(ctx.device, ctx.device_context);
}

void ShutdownImGuiBackends() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
}

bool PollEvents(bool& done) {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) {
            done = true;
        }
    }
    return !done;
}

void BeginFrame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void RenderFrame(Win32Dx11Context& ctx, const float clear_color[4]) {
    ImGui::Render();
    ctx.device_context->OMSetRenderTargets(1, &ctx.main_render_target_view, nullptr);
    ctx.device_context->ClearRenderTargetView(ctx.main_render_target_view, clear_color);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx.swap_chain->Present(1, 0);
}

}  // namespace app::platform
