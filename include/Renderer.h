#pragma once

#include "Maths.h"
#include "Model.h"
#include "Texture.h"
#include "Config.h"
#include "Window.h"
#include <bgfx.h>
#include <chrono>

namespace cl
{
    enum class BlendMode
    {
        None,                 // No blending
        Alpha,                // Standard alpha blending
        Additive,             // Add colors (brightens)
        Multiplied,           // Multiply source and destination
        Subtract,             // Subtract source from destination
        Screen,               // Screen blend (inverse multiply)
        Darken,               // Darken only
        Lighten,              // Lighten only
        LinearBurn,           // Linear burn
        LinearDodge,          // Linear dodge (similar to additive)
        PremultipliedAlpha    // For pre-multiplied alpha textures
    };

    struct DrawStats
    {
        int drawCalls = 0;
        int triangles = 0;
        int vertices = 0;
        int indicies = 0;
        int textureBinds = 0;
        int shaderSwitches = 0;
        float cpuTime = 0.0f;
        float gpuTime = 0.0f;
        int textureMemoryUsed = 0.0f;
        int gpuMemoryUsed = 0.0f;
    };

    struct ProfileMarker
    {
        std::string name = nullptr;
        float cpuTime = 0.0f;
        float gpuTime = 0.0f;
    };

    struct RendererState
    {
        Window* window;
        int width;
        int height;
        bgfx::ProgramHandle defaultProgram; // Todo: Should likely remove this since default shader is in Shader.h
        uint16_t currentViewId = 0;
        uint32_t clearColor = 0x000000ff;
        float clearDepth = 1.0f;

        // Blend mode
        BlendMode currentBlendMode = BlendMode::None;

        // Statistics
        DrawStats drawStats;
        std::chrono::steady_clock::time_point frameStartTime;
        std::chrono::steady_clock::time_point frameEndTime;

        // Profiling
        bool profilerEnabled;
        std::vector<ProfileMarker> profileMarkers;
        std::chrono::steady_clock::time_point currentMarkerStart;
        std::string currentMarkerName;
        Shader* lastShader;
    };
    extern RendererState* s_renderer;

    bool InitRenderer(Window* window, const Config& config);
    void ShutdownRenderer();

    void BeginFrame();
    void EndFrame();

    void Clear(const Color& color, float depth = 1.0f);
    void SetViewport(int x, int y, int width, int height);
    void SetViewTransform(const Matrix4& view, const Matrix4& projection);

    void DrawMesh(Mesh* mesh, const Matrix4& transform);
    void DrawMesh(Mesh* mesh, const Vector3& position, const Vector3& rotation, const Vector3& scale);
    void DrawModel(Model* model, const Vector3& position, const Vector3& rotation, const Vector3& scale);
    void DrawModel(Model* model);

    void SetCullMode(bool enabled, bool clockwise = true);
    void SetDepthTest(bool enabled);
    void SetWireframe(bool enabled);

    int GetViewWidth();
    int GetViewHeight();

    static bgfx::ProgramHandle CreateDefaultShader();

    // Blend Mode
    void SetBlendMode(BlendMode mode);
    BlendMode GetBlendMode();

    // Performance Stats
    void ResetDrawStats();
    int GetDrawCallCount();
    int GetTriangleCount();
    int GetVertexCount();
    int GetIndexCount();
    int GetTextureBindCount();
    int GetShaderSwitchCount();
    float GetCPUFrameTime();
    float GetGPUFrameTime();
    const DrawStats& GetDrawStats();
    int GetGPUMemoryUsage();
    int GetTextureMemoryUsage();

    // Renderer Info
    std::string GetRendererName();
    std::string GetGPUVendor();
    int GetMaxTextureSize();

    // Profiling and Markers
    void BeginProfileMarker(std::string_view name);
    void EndProfileMarker();
    void SetProfilerEnabled(bool enabled);
    const std::vector<ProfileMarker>& GetProfileMarkers();
    void SetDebugMarker(std::string_view marker);
}