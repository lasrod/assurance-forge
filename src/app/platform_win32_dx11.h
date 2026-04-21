#pragma once

#include <windows.h>
#include <d3d11.h>

namespace app::platform {

struct Win32Dx11Context {
    HWND hwnd = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* main_render_target_view = nullptr;
};

bool Initialize(HINSTANCE h_instance, int n_cmd_show, Win32Dx11Context& out_ctx);
void Shutdown(Win32Dx11Context& ctx);

bool InitializeImGuiBackends(const Win32Dx11Context& ctx);
void ShutdownImGuiBackends();

bool PollEvents(bool& done);
void BeginFrame();
void RenderFrame(Win32Dx11Context& ctx, const float clear_color[4]);

}  // namespace app::platform
