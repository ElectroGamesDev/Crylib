#pragma once

#include "Maths.h"
#include "Model.h"
#include "Texture.h"
#include "Config.h"
#include "Window.h"
#include <bgfx.h>

namespace cl
{
    struct RendererState
    {
        Window* window;
        int width;
        int height;
        bgfx::ProgramHandle defaultProgram; // Todo: Should likely remove this since default shader is in Shader.h
        uint32_t clearColor;
        uint16_t currentViewId = -1;
    };

    bool InitRenderer(Window* window, const Config& config);
    void ShutdownRenderer();

    void BeginFrame();
    void EndFrame();

    void Clear(const Color& color, float depth = 1.0f);
    void SetViewport(int x, int y, int width, int height);
    void SetViewTransform(const Matrix4& view, const Matrix4& projection);

    void DrawMesh(Mesh* mesh, const Matrix4& transform);
    void DrawMesh(Mesh* mesh, const Vector3& position, const Vector3& rotation, const Vector3& scale);
    void DrawModel(Model* model);
    void DrawModel(Model* model, const Vector3& position, const Vector3& rotation, const Vector3& scale);

    void SetCullMode(bool enabled, bool clockwise = true);
    void SetDepthTest(bool enabled);
    void SetWireframe(bool enabled);

    int GetViewWidth();
    int GetViewHeight();

    static bgfx::ProgramHandle CreateDefaultShader();

    extern RendererState* s_renderer;
}