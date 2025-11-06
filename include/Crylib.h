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

    // Window queries
    bool ShouldClose();
    void SetWindowTitle(const char* title);
    void GetWindowSize(int& width, int& height);

    // Direct access to subsystems if needed
    Window* GetWindow();
}