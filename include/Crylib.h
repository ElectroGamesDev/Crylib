#pragma once

// Todo: Some of these includes can likely be removed
#include "Config.h"
#include "Maths.h"
#include "Window.h"
#include "Input.h"
#include "Renderer.h"
#include "Model.h";
#include "Texture.h"
#include "loaders/ModelLoader.h"
#include "Shader.h"
#include "Audio.h"
#include "Camera.h"
#include "Camera2D.h"

namespace cl
{
    // Framework lifetime
    bool Init(const Config& config = Config());
    void Update();
    void Shutdown();

    // Window State and Properties
    bool ShouldClose();
    void SetWindowTitle(std::string_view title);
    void GetWindowSize(int& width, int& height);
    bool IsWindowReady();
    bool IsWindowFullscreen();
    bool IsWindowHidden();
    bool IsWindowMinimized();
    bool IsWindowMaximized();
    bool IsWindowFocused();
    bool IsWindowResized();
    void ToggleFullscreen();
    void MaximizeWindow();
    void MinimizeWindow();
    void RestoreWindow();
    void SetWindowOpacity(float opacity);
    void SetWindowIcon(std::string_view iconPath);
    int GetMonitorCount();
    int GetCurrentMonitor();
    void GetMonitorSize(int monitor, int& width, int& height);
    int GetMonitorRefreshRate(int monitor);
    void GetMonitorPosition(int monitor, int& x, int& y);
    std::string GetMonitorName(int monitor);

    // Time and FPS
    float GetFrameTime();
    /// An alias for GetFrameTime()
    float GetDeltaTime();
    double GetTime();
    int GetFrameCount();
    void SetTargetFPS(int fps);
    int GetFPS();

    // System Info
    const char* GetPlatformName();
    int GetCPUCoreCount();

    // Misc
    const Config& GetConfig();
    Window* GetWindow();
}