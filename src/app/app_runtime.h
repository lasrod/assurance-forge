#pragma once

namespace app {

class AppRuntime {
public:
    AppRuntime();
    ~AppRuntime();

    AppRuntime(const AppRuntime&) = delete;
    AppRuntime& operator=(const AppRuntime&) = delete;

    void RenderFrame(bool& done);

private:
    void ScanDirectory();
    void RenderSplitters(float display_w, float display_h, float left_w, float center_w);
    void RenderTreePanel(float left_w, float top_left_h);
    void RenderSacmViewerPanel(float left_w, float top_left_h, float bottom_left_h, bool& done);
    void RenderCenterPanel(float center_x, float center_w, float display_h);
    void RenderElementPropertiesPanel(float center_x, float center_w, float right_w);

    void RebuildDerivedViewsIfNeeded();

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace app
