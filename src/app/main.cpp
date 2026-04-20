// Assurance Forge - Win32 + DirectX11 Application
// Minimal ImGui window with SACM XML parsing capability

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>

#include "ui/gsn_canvas.h"
#include "ui/gsn_adapter.h"
#include "ui/tree_view.h"
#include "ui/element_panel.h"
#include "ui/ui_state.h"

#include "core/app_state.h"
#include <cstdio>   // for FILE, fopen, fclose (file-exists check)

// DirectX 11 globals
static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

// Forward declarations
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Create application window
    WNDCLASSEXW wc = {
        sizeof(wc),
        CS_CLASSDC,
        WndProc,
        0L,
        0L,
        hInstance,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        L"AssuranceForgeClass",
        nullptr
    };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(
        wc.lpszClassName,
        L"Assurance Forge",
        WS_OVERLAPPEDWINDOW,
        100, 100, 1280, 720,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // Disable imgui.ini — we manage layout manually
    // Docking requires a docking-enabled Dear ImGui build. If you update
    // the bundled ImGui to a version with docking support, re-enable
    // the following line:
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Load fonts: normal and bold variants of Segoe UI (Windows system font)
    const char* font_path = "C:\\Windows\\Fonts\\segoeui.ttf";
    const char* bold_path = "C:\\Windows\\Fonts\\segoeuib.ttf";
    io.Fonts->AddFontFromFileTTF(font_path, 15.0f);  // default font
    ui::g_BoldFont = io.Fonts->AddFontFromFileTTF(bold_path, 15.0f);
    if (!ui::g_BoldFont) {
        // Fallback: use default font for bold too
        ui::g_BoldFont = io.Fonts->Fonts[0];
    }

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Application state
    core::AppState app_state;
    static char file_path_buf[512] = "data/open-autonomy-safety-case.sacm.xml";
    bool tree_needs_rebuild = false;
    core::AssuranceTree current_tree;
    bool show_overwrite_confirm = false;

    // Fixed panel window flags (no moving, no resizing, no collapsing)
    const ImGuiWindowFlags kPanelFlags = ImGuiWindowFlags_NoMove
                                       | ImGuiWindowFlags_NoResize
                                       | ImGuiWindowFlags_NoCollapse
                                       | ImGuiWindowFlags_NoBringToFrontOnFocus;

    // Panel layout ratios (persist across frames, adjustable via splitter drag)
    float left_ratio  = 0.20f;   // left column width as fraction of display
    float right_ratio = 0.20f;   // right column width as fraction of display
    const float kMinPanelRatio = 0.10f;  // minimum 10% of display width
    const float kMaxPanelRatio = 0.40f;  // maximum 40% of display width
    const float kSplitterThickness = 6.0f;  // hit area for splitter drag

    // Main loop
    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) break;

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Compute panel layout from current display size
        ImVec2 display = ImGui::GetIO().DisplaySize;
        float left_w   = display.x * left_ratio;
        float right_w  = display.x * right_ratio;
        float center_w = display.x - left_w - right_w - kSplitterThickness * 2;
        float top_left_h    = display.y * 0.50f;
        float bottom_left_h = display.y - top_left_h;

        // ---- Left splitter (between left column and center) ----
        {
            float splitter_x = left_w;
            ImGui::SetNextWindowPos(ImVec2(splitter_x, 0));
            ImGui::SetNextWindowSize(ImVec2(kSplitterThickness, display.y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::Begin("##left_splitter", nullptr,
                         kPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
            ImGui::InvisibleButton("##left_splitter_btn", ImVec2(kSplitterThickness, display.y));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                float delta = ImGui::GetIO().MouseDelta.x;
                left_ratio += delta / display.x;
                if (left_ratio < kMinPanelRatio) left_ratio = kMinPanelRatio;
                if (left_ratio > kMaxPanelRatio) left_ratio = kMaxPanelRatio;
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        float center_x = left_w + kSplitterThickness;

        // ---- Right splitter (between center and right column) ----
        {
            float splitter_x = center_x + center_w;
            ImGui::SetNextWindowPos(ImVec2(splitter_x, 0));
            ImGui::SetNextWindowSize(ImVec2(kSplitterThickness, display.y));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
            ImGui::Begin("##right_splitter", nullptr,
                         kPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
            ImGui::InvisibleButton("##right_splitter_btn", ImVec2(kSplitterThickness, display.y));
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
                float delta = ImGui::GetIO().MouseDelta.x;
                right_ratio -= delta / display.x;
                if (right_ratio < kMinPanelRatio) right_ratio = kMinPanelRatio;
                if (right_ratio > kMaxPanelRatio) right_ratio = kMaxPanelRatio;
            }
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
        }

        // ---- Top-Left: Tree View ----
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(left_w, top_left_h));
        ImGui::Begin("Safety Case Tree", nullptr, kPanelFlags);
        ui::ShowTreeViewPanel(current_tree.root ? &current_tree : nullptr);
        ImGui::End();

        // ---- Bottom-Left: SACM Viewer ----
        ImGui::SetNextWindowPos(ImVec2(0, top_left_h));
        ImGui::SetNextWindowSize(ImVec2(left_w, bottom_left_h));
        ImGui::Begin("SACM Viewer", nullptr, kPanelFlags | ImGuiWindowFlags_MenuBar);

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Exit")) {
                    done = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // File loading section
        ImGui::Text("XML File:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##filepath", file_path_buf, sizeof(file_path_buf));
        if (ImGui::Button("Load")) {
            app_state.load_file(file_path_buf);
            tree_needs_rebuild = true;
        }
        ImGui::SameLine();
        {
            bool can_save = app_state.sacm_package.has_value();
            if (!can_save) ImGui::BeginDisabled();
            if (ImGui::Button("Save")) {
                // Check if file exists before overwriting
                FILE* f = fopen(file_path_buf, "r");
                if (f) {
                    fclose(f);
                    show_overwrite_confirm = true;
                } else {
                    app_state.save_file(file_path_buf);
                }
            }
            if (!can_save) ImGui::EndDisabled();
        }

        // Overwrite confirmation popup
        if (show_overwrite_confirm) {
            ImGui::OpenPopup("Overwrite File?");
            show_overwrite_confirm = false;
        }
        if (ImGui::BeginPopupModal("Overwrite File?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("File already exists:\n%s", file_path_buf);
            ImGui::Separator();
            ImGui::Text("Are you sure you want to overwrite it?");
            ImGui::Spacing();
            if (ImGui::Button("Yes, Overwrite", ImVec2(130, 0))) {
                app_state.save_file(file_path_buf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(130, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Status display
        if (!app_state.status_message.empty()) {
            ImGui::TextWrapped("%s", app_state.status_message.c_str());
        }

        ImGui::Separator();

        // Display loaded data
        if (app_state.loaded_case.has_value()) {
            const auto& ac = app_state.loaded_case.value();

            // Build tree once on load, not every frame
            if (tree_needs_rebuild) {
                current_tree = ui::BuildAssuranceTree(ac);
                ui::SetCanvasTree(current_tree);
                tree_needs_rebuild = false;
            }

            ImGui::Text("Assurance Case: %s", ac.name.c_str());

            ImGui::Separator();

            // Element list
            if (ImGui::BeginChild("ElementList", ImVec2(0, 0), true)) {
                for (const auto& elem : ac.elements) {
                    ImGui::PushID(elem.id.c_str());

                    // Color-code by type
                    ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
                    if (elem.type == "claim") {
                        color = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
                    } else if (elem.type == "argumentreasoning") {
                        color = ImVec4(0.4f, 0.6f, 0.9f, 1.0f);
                    } else if (elem.type == "artifact" || elem.type == "artifactreference") {
                        color = ImVec4(0.9f, 0.7f, 0.3f, 1.0f);
                    }

                    ImGui::TextColored(color, "[%s]", elem.type.c_str());
                    ImGui::SameLine();
                    ImGui::Text("%s: %s", elem.id.c_str(), elem.name.c_str());

                    if (!elem.content.empty()) {
                        ImGui::TextWrapped("  Content: %s", elem.content.c_str());
                    }

                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
        }

        ImGui::End();

        // ---- Center: GSN Canvas ----
        ImGui::SetNextWindowPos(ImVec2(center_x, 0));
        ImGui::SetNextWindowSize(ImVec2(center_w, display.y));
        ui::ShowGsnCanvasWindow();

        // ---- Right: Element Properties ----
        float right_x = center_x + center_w + kSplitterThickness;
        ImGui::SetNextWindowPos(ImVec2(right_x, 0));
        ImGui::SetNextWindowSize(ImVec2(right_w, display.y));
        ImGui::Begin("Element Properties", nullptr, kPanelFlags);
        ui::ShowElementPanel(app_state.loaded_case.has_value() ? &app_state.loaded_case.value() : nullptr);
        ImGui::End();

        // Rendering
        ImGui::Render();
        const float clear_color[4] = { 0.1f, 0.1f, 0.1f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);  // VSync on
    }

    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };
    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createDeviceFlags,
        featureLevelArray,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &featureLevel,
        &g_pd3dDeviceContext
    );
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_WARP,
            nullptr,
            createDeviceFlags,
            featureLevelArray,
            2,
            D3D11_SDK_VERSION,
            &sd,
            &g_pSwapChain,
            &g_pd3dDevice,
            &featureLevel,
            &g_pd3dDeviceContext
        );
    }
    if (res != S_OK) {
        return false;
    }

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
        return true;
    }

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
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
