#pragma once

#include "Config.h"
#include <string>

struct GLFWwindow;

namespace cl
{
    class Window
    {
    public:
        virtual ~Window() = default;

        virtual bool Init(const Config& config) = 0;
        virtual void PollEvents() = 0;
        virtual bool ShouldClose() const = 0;
        virtual void Shutdown() = 0;

        virtual void* GetNativeWindowHandle() const = 0;
        virtual void GetWindowSize(int& width, int& height) const = 0;
        virtual void SetWindowTitle(std::string_view title) = 0;
        virtual bool IsFullscreen() const = 0;
        virtual bool IsHidden() const = 0;
        virtual bool IsMinimized() const = 0;
        virtual bool IsMaximized() const = 0;
        virtual bool IsFocused() const = 0;
        virtual void ToggleFullscreen() = 0;
        virtual void Maximize() = 0;
        virtual void Minimize() = 0;
        virtual void Restore() = 0;
        virtual void SetOpacity(float opacity) = 0;
        virtual void SetIcon(std::string_view iconPath) = 0;
        virtual int GetMonitorCount() const = 0;
        virtual int GetCurrentMonitor() const = 0;
        virtual void GetMonitorSize(int monitor, int& width, int& height) const = 0;
        virtual int GetMonitorRefreshRate(int monitor) const = 0;
        virtual void GetMonitorPosition(int monitor, int& x, int& y) const = 0;
        virtual std::string GetMonitorName(int monitor) const = 0;

        static Window* Create();
    };

#ifdef PLATFORM_WINDOWS
    class WindowsWindow : public Window
    {
    public:
        WindowsWindow();
        ~WindowsWindow() override;

        bool Init(const Config& config) override;
        void PollEvents() override;
        bool ShouldClose() const override;
        void Shutdown() override;

        void* GetNativeWindowHandle() const override;
        void GetWindowSize(int& width, int& height) const override;
        void SetWindowTitle(std::string_view title) override;
        bool IsFullscreen() const override;
        bool IsHidden() const override;
        bool IsMinimized() const override;
        bool IsMaximized() const override;
        bool IsFocused() const override;
        void ToggleFullscreen() override;
        void Maximize() override;
        void Minimize() override;
        void Restore() override;
        void SetOpacity(float opacity) override;
        void SetIcon(std::string_view iconPath) override;
        int GetMonitorCount() const override;
        int GetCurrentMonitor() const override;
        void GetMonitorSize(int monitor, int& width, int& height) const override;
        int GetMonitorRefreshRate(int monitor) const override;
        void GetMonitorPosition(int monitor, int& x, int& y) const override;
        std::string GetMonitorName(int monitor) const override;

    private:
        static void GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
        static void GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
        static void GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos);
        static void GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset);

        GLFWwindow* m_window;
        int m_width;
        int m_height;
    };
#endif
}