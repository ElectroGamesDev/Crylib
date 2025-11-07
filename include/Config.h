#pragma once

#include <string>

#if defined(_WIN32) || defined(_WIN64)
    #define PLATFORM_WINDOWS
#elif defined(__linux__)
    #define PLATFORM_LINUX
#elif defined(__APPLE__)
    #define PLATFORM_MACOS
#endif

namespace cl
{
    enum RenderingAPI
    {
        Null,
        DirectX11,
        DirectX12,
        Metal,
        OpenGLES, // OpenGL ES 2.0+
        OpenGL, // OpenGL 2.1+
        Vulkan,
    };

    struct Config
    {
        std::string windowTitle = "Crylib Application";
        int windowWidth = 800;
        int windowHeight = 600;
        bool windowResizable = true;
        bool windowVSync = true;
        bool windowFullscreen = false;
        RenderingAPI renderingAPI = DirectX11;

        int msaaSamples = 4;
        bool debugRenderer = false;

        bool audioEnabled = true;
    };
}