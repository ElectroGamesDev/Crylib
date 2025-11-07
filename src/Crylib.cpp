#include "Crylib.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio/include/miniaudio.h"
#include <chrono>
#include <thread>
#include <iostream>

namespace cl
{
    struct CrylibState
    {
        Window* window;
        Config config;
        bool initialized;

        // Time management
        std::chrono::steady_clock::time_point startTime;
        std::chrono::steady_clock::time_point lastFrameTime;
        std::chrono::steady_clock::time_point currentFrameTime;
        float deltaTime;
        int frameCount;
        int targetFPS;
        double frameTimeAccumulator;
        int fpsCounter;
        int currentFPS;

        // Window state tracking
        bool wasResized;
        int lastWidth;
        int lastHeight;

        CrylibState()
            : window(nullptr)
            , initialized(false)
            , deltaTime(0.0f)
            , frameCount(0)
            , targetFPS(0)
            , frameTimeAccumulator(0.0)
            , fpsCounter(0)
            , currentFPS(0)
            , wasResized(false)
            , lastWidth(0)
            , lastHeight(0)
        {
            startTime = std::chrono::steady_clock::now();
            lastFrameTime = startTime;
            currentFrameTime = startTime;
        }
    };

    static CrylibState* s_crylib = nullptr;

    bool Init(const Config& config)
    {
        if (s_crylib)
            return false;

        s_crylib = new CrylibState();
        s_crylib->config = config;
        s_crylib->initialized = false;

        // Create platform window
        s_crylib->window = Window::Create();
        if (!s_crylib->window)
        {
            delete s_crylib;
            s_crylib = nullptr;

            return false;
        }

        // Initialize window
        if (!s_crylib->window->Init(config))
        {
            delete s_crylib->window;
            delete s_crylib;
            s_crylib = nullptr;

            return false;
        }

        // Store initial window size
        s_crylib->window->GetWindowSize(s_crylib->lastWidth, s_crylib->lastHeight);

        // Initialize input system
        Input::Init();

        // Initialize the audio system
        if (config.audioEnabled)
            cl::InitAudioDevice();

        // Set random seed
        RandomizeSeed();

        // Initialize renderer
        if (!InitRenderer(s_crylib->window, config))
        {
            Input::Shutdown();
            ShutdownAudioDevice();
            s_crylib->window->Shutdown();
            delete s_crylib->window;
            delete s_crylib;
            s_crylib = nullptr;

            return false;
        }

        s_crylib->initialized = true;
        return true;
    }

    void Update()
    {
        if (!s_crylib || !s_crylib->initialized)
            return;

        static auto lastTime = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        float delta = std::chrono::duration<float>(now - lastTime).count();

        // Frame rate limiting
        if (s_crylib->targetFPS > 0)
        {
            float targetFrameTime = 1.0f / s_crylib->targetFPS;
            float sleepThreshold = 0.002f;

            while (delta < targetFrameTime)
            {
                float remaining = targetFrameTime - delta;

                if (remaining > sleepThreshold)
                {
                    auto sleepDur = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::duration<float>(remaining * 0.9f));

                    if (sleepDur.count() > 0)
                    {
#ifdef PLATFORM_WEB
                        emscripten_sleep(static_cast<unsigned int>(sleepDur.count() / 1000));
#else
                        std::this_thread::sleep_for(sleepDur);
#endif                    
                    }
                }
                else
                {
                    std::this_thread::yield();
                }

                now = std::chrono::steady_clock::now();
                delta = std::chrono::duration<float>(now - lastTime).count();
            }
        }

        // Clamp delta to prevent spikes
        constexpr float MAX_DELTA = 0.1f;
        s_crylib->deltaTime = std::min(delta, MAX_DELTA);

        // Update timing
        s_crylib->lastFrameTime = lastTime;
        s_crylib->currentFrameTime = now;
        lastTime = now;

        // FPS Counter
        s_crylib->frameTimeAccumulator += s_crylib->deltaTime;
        s_crylib->fpsCounter++;

        if (s_crylib->frameTimeAccumulator >= 1.0f)
        {
            s_crylib->currentFPS = s_crylib->fpsCounter;
            s_crylib->fpsCounter = 0;
            s_crylib->frameTimeAccumulator = 0.0f;
        }

        s_crylib->frameCount++;

        // Window handling
        int currentWidth, currentHeight;
        s_crylib->window->GetWindowSize(currentWidth, currentHeight);
        s_crylib->wasResized = (currentWidth != s_crylib->lastWidth || currentHeight != s_crylib->lastHeight);
        s_crylib->lastWidth = currentWidth;
        s_crylib->lastHeight = currentHeight;

        // Events and input
        s_crylib->window->PollEvents();
        Input::Update();
    }

    void Shutdown()
    {
        if (!s_crylib)
            return;

        // Todo: Add an option in config.h to not automatically handle deleting resources like Shaders, Meshes, etc. since this does cause overhead
        // We can't free the memory here due to not being able to know if its created in the heap or stack. We could techincally make a Create() function which returns a pointer, but
        // it's not that important as the platform should clean up when the program is closed and in most cases, the user should be cleaning up anyway

        // Todo: Use unordered_map instead of Vector, it will be faster
        Model::s_models.clear();
        for (int i = static_cast<int>(Model::s_models.size()) - 1; i >= 0; --i)
            Model::s_models[i]->Destroy();

        Model::s_models.clear();

        for (int i = static_cast<int>(Mesh::s_meshes.size()) - 1; i >= 0; --i)
            Mesh::s_meshes[i]->Destroy();
        Mesh::s_meshes.clear();

        for (int i = static_cast<int>(Shader::s_shaders.size()) - 1; i >= 0; --i)
            Shader::s_shaders[i]->Destroy();
        Shader::s_shaders.clear();

        for (int i = static_cast<int>(Texture::s_textures.size()) - 1; i >= 0; --i)
            Texture::s_textures[i]->Destroy();
        Texture::s_textures.clear();

        // Todo: Add Animation, AnimationClip, Skeletal, etc.

        // Todo: Fix this. Sounds are returned as copies, which therefore the pointers in s_sounds are invalid
        //for (int i = static_cast<int>(s_sounds.size()) - 1; i >= 0; --i)
        //    UnloadSound(*s_sounds[i]);
        //s_sounds.clear();

        //for (int i = static_cast<int>(s_musics.size()) - 1; i >= 0; --i)
        //    UnloadMusicStream(*s_musics[i]);
        //s_musics.clear();

        //for (int i = static_cast<int>(s_audioStreams.size()) - 1; i >= 0; --i)
        //    UnloadAudioStream(*s_audioStreams[i]);
        //s_audioStreams.clear();

        ShutdownRenderer();
        Input::Shutdown();

        if (s_crylib->config.audioEnabled)
            cl::ShutdownAudioDevice();

        if (s_crylib->window)
        {
            s_crylib->window->Shutdown();
            delete s_crylib->window;
            s_crylib->window = nullptr;
        }

        delete s_crylib;
        s_crylib = nullptr;
    }

    // Window State and Properties

    bool ShouldClose()
    {
        if (!s_crylib || !s_crylib->window)
            return true;

        return s_crylib->window->ShouldClose();
    }

    void SetWindowTitle(const char* title)
    {
        if (s_crylib && s_crylib->window)
        {
            s_crylib->window->SetWindowTitle(title);
        }
    }

    void GetWindowSize(int& width, int& height)
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->GetWindowSize(width, height);
        else
        {
            width = 0;
            height = 0;
        }
    }

    bool IsWindowReady()
    {
        return s_crylib && s_crylib->window && s_crylib->initialized;
    }

    bool IsWindowFullscreen()
    {
        if (!s_crylib || !s_crylib->window)
            return false;
        return s_crylib->window->IsFullscreen();
    }

    bool IsWindowHidden()
    {
        if (!s_crylib || !s_crylib->window)
            return false;
        return s_crylib->window->IsHidden();
    }

    bool IsWindowMinimized()
    {
        if (!s_crylib || !s_crylib->window)
            return false;
        return s_crylib->window->IsMinimized();
    }

    bool IsWindowMaximized()
    {
        if (!s_crylib || !s_crylib->window)
            return false;
        return s_crylib->window->IsMaximized();
    }

    bool IsWindowFocused()
    {
        if (!s_crylib || !s_crylib->window)
            return false;
        return s_crylib->window->IsFocused();
    }

    bool IsWindowResized()
    {
        return s_crylib ? s_crylib->wasResized : false;
    }

    void ToggleFullscreen()
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->ToggleFullscreen();
    }

    void MaximizeWindow()
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->Maximize();
    }

    void MinimizeWindow()
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->Minimize();
    }

    void RestoreWindow()
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->Restore();
    }

    void SetWindowOpacity(float opacity)
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->SetOpacity(opacity);
    }

    void SetWindowIcon(const char* iconPath)
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->SetIcon(iconPath);
    }

    int GetMonitorCount()
    {
        if (!s_crylib || !s_crylib->window)
            return 0;
        return s_crylib->window->GetMonitorCount();
    }

    int GetCurrentMonitor()
    {
        if (!s_crylib || !s_crylib->window)
            return 0;
        return s_crylib->window->GetCurrentMonitor();
    }

    void GetMonitorSize(int monitor, int& width, int& height)
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->GetMonitorSize(monitor, width, height);
        else
        {
            width = 0;
            height = 0;
        }
    }

    int GetMonitorRefreshRate(int monitor)
    {
        if (!s_crylib || !s_crylib->window)
            return 0;
        return s_crylib->window->GetMonitorRefreshRate(monitor);
    }

    void GetMonitorPosition(int monitor, int& x, int& y)
    {
        if (s_crylib && s_crylib->window)
            s_crylib->window->GetMonitorPosition(monitor, x, y);
        else
        {
            x = 0;
            y = 0;
        }
    }

    const char* GetMonitorName(int monitor)
    {
        if (!s_crylib || !s_crylib->window)
            return "Unknown";
        return s_crylib->window->GetMonitorName(monitor);
    }

    // Time and FPS
    float GetFrameTime()
    {
        return s_crylib ? s_crylib->deltaTime : 0.0f;
    }

    float GetDeltaTime()
    {
        return GetFrameTime();
    }

    double GetTime()
    {
        if (!s_crylib)
            return 0.0;

        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = now - s_crylib->startTime;
        return elapsed.count();
    }

    int GetFrameCount()
    {
        return s_crylib ? s_crylib->frameCount : 0;
    }

    void SetTargetFPS(int fps)
    {
        if (s_crylib)
            s_crylib->targetFPS = fps;
        else
            std::cout << "[WARNING] SetTargetFrame() must be called after Init()" << std::endl;
    }

    int GetFPS()
    {
        return s_crylib ? s_crylib->currentFPS : 0;
    }

    // System Info
    const char* GetPlatformName()
    {
#ifdef PLATFORM_WINDOWS
        return "Windows";
#elif defined(PLATFORM_LINUX)
        return "Linux";
#elif defined(PLATFORM_MACOS)
        return "macOS";
#elif defined(PLATFORM_IOS)
        return "IOS";
#elif defined(PLATFORM_ANDROID)
        return "Android";
#elif defined(PLATFORM_WEB)
        return "Web";
#else
        return "Unknown";
#endif
    }

    int GetCPUCoreCount()
    {
        return static_cast<int>(std::thread::hardware_concurrency());
    }

    // Misc
    Window* GetWindow()
    {
        return s_crylib ? s_crylib->window : nullptr;
    }

    const Config& GetConfig()
    {
        static Config emptyConfig;
        return s_crylib ? s_crylib->config : emptyConfig;
    }
}