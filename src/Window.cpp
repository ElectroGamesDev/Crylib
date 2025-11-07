#include "Window.h"
#include "Input.h"
#include <glfw3.h>
#include <iostream>
#include <algorithm>

#ifdef PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#define NOMINMAX
#include <glfw3native.h>
#endif

namespace cl
{
    Window* Window::Create()
    {
#ifdef PLATFORM_WINDOWS
        return new WindowsWindow();
#else
        return nullptr;
#endif
    }

#ifdef PLATFORM_WINDOWS
    static void GLFWErrorCallback(int error, const char* description)
    {
        std::cerr << "GLFW Error [" << error << "]: " << description << std::endl;
    }

    void WindowsWindow::GLFWKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
    {
        if (action == GLFW_PRESS || action == GLFW_RELEASE)
        {
            Input::UpdateKeyState(static_cast<KeyCode>(key), action == GLFW_PRESS);
        }
    }

    void WindowsWindow::GLFWMouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
    {
        if (action == GLFW_PRESS || action == GLFW_RELEASE)
        {
            Input::UpdateMouseButtonState(static_cast<MouseButton>(button), action == GLFW_PRESS);
        }
    }

    void WindowsWindow::GLFWCursorPosCallback(GLFWwindow* window, double xpos, double ypos)
    {
        Input::UpdateMousePosition(static_cast<float>(xpos), static_cast<float>(ypos));
    }

    void WindowsWindow::GLFWScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
    {
        Input::UpdateMouseWheel(static_cast<float>(yoffset));
    }

    WindowsWindow::WindowsWindow()
        : m_window(nullptr)
        , m_width(0)
        , m_height(0)
    {
    }

    WindowsWindow::~WindowsWindow()
    {
        Shutdown();
    }

    bool WindowsWindow::Init(const Config& config)
    {
        glfwSetErrorCallback(GLFWErrorCallback);

        if (!glfwInit())
            return false;

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, config.windowResizable ? GLFW_TRUE : GLFW_FALSE);

        m_width = config.windowWidth;
        m_height = config.windowHeight;

        m_window = glfwCreateWindow(
            m_width,
            m_height,
            config.windowTitle,
            config.windowFullscreen ? glfwGetPrimaryMonitor() : nullptr,
            nullptr
        );

        if (!m_window)
        {
            glfwTerminate();
            return false;
        }

        // Set callbacks
        glfwSetKeyCallback(m_window, GLFWKeyCallback);
        glfwSetMouseButtonCallback(m_window, GLFWMouseButtonCallback);
        glfwSetCursorPosCallback(m_window, GLFWCursorPosCallback);
        glfwSetScrollCallback(m_window, GLFWScrollCallback);

        return true;
    }

    void WindowsWindow::PollEvents()
    {
        glfwPollEvents();
    }

    bool WindowsWindow::ShouldClose() const
    {
        return m_window ? glfwWindowShouldClose(m_window) : true;
    }

    void WindowsWindow::Shutdown()
    {
        if (m_window)
        {
            glfwDestroyWindow(m_window);
            m_window = nullptr;
        }
        glfwTerminate();
    }

    void* WindowsWindow::GetNativeWindowHandle() const
    {
        return glfwGetWin32Window(m_window);
    }

    void WindowsWindow::GetWindowSize(int& width, int& height) const
    {
        if (m_window)
        {
            glfwGetWindowSize(m_window, &width, &height);
        }
        else
        {
            width = m_width;
            height = m_height;
        }
    }

    void WindowsWindow::SetWindowTitle(const char* title)
    {
        if (m_window)
        {
            glfwSetWindowTitle(m_window, title);
        }
    }

    bool WindowsWindow::IsFullscreen() const
    {
        if (!m_window)
            return false;

        return glfwGetWindowMonitor(m_window) != nullptr;
    }

    bool WindowsWindow::IsHidden() const
    {
        if (!m_window)
            return false;

        return !glfwGetWindowAttrib(m_window, GLFW_VISIBLE);
    }

    bool WindowsWindow::IsMinimized() const
    {
        if (!m_window)
            return false;

        return glfwGetWindowAttrib(m_window, GLFW_ICONIFIED);
    }

    bool WindowsWindow::IsMaximized() const
    {
        if (!m_window)
            return false;

        return glfwGetWindowAttrib(m_window, GLFW_MAXIMIZED);
    }

    bool WindowsWindow::IsFocused() const
    {
        if (!m_window)
            return false;

        return glfwGetWindowAttrib(m_window, GLFW_FOCUSED);
    }

    void WindowsWindow::ToggleFullscreen()
    {
        if (!m_window)
            return;

        if (IsFullscreen())
        {
            // Switch to windowed mode
            glfwSetWindowMonitor(m_window, nullptr, 100, 100, m_width, m_height, GLFW_DONT_CARE);
        }
        else
        {
            // Switch to fullscreen mode
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        }
    }

    void WindowsWindow::Maximize()
    {
        if (m_window)
            glfwMaximizeWindow(m_window);
    }

    void WindowsWindow::Minimize()
    {
        if (m_window)
            glfwIconifyWindow(m_window);
    }

    void WindowsWindow::Restore()
    {
        if (m_window)
            glfwRestoreWindow(m_window);
    }

    void WindowsWindow::SetOpacity(float opacity)
    {
        if (m_window)
            glfwSetWindowOpacity(m_window, opacity);
    }

    void WindowsWindow::SetIcon(const char* iconPath)
    {
        if (!m_window)
            return;

        // Todo: Implement this
    }

    int WindowsWindow::GetMonitorCount() const
    {
        int count;
        glfwGetMonitors(&count);
        return count;
    }

    int WindowsWindow::GetCurrentMonitor() const
    {
        if (!m_window)
            return 0;

        int wx, wy, ww, wh;
        glfwGetWindowPos(m_window, &wx, &wy);
        glfwGetWindowSize(m_window, &ww, &wh);

        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);

        int bestoverlap = 0;
        int bestmonitor = 0;

        for (int i = 0; i < count; i++)
        {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            int mx, my;
            glfwGetMonitorPos(monitors[i], &mx, &my);

            int overlap = std::max(0, std::min(wx + ww, mx + mode->width) - std::max(wx, mx)) * std::max(0, std::min(wy + wh, my + mode->height) - std::max(wy, my));

            if (bestoverlap < overlap)
            {
                bestoverlap = overlap;
                bestmonitor = i;
            }
        }

        return bestmonitor;
    }

    void WindowsWindow::GetMonitorSize(int monitor, int& width, int& height) const
    {
        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);

        if (monitor >= 0 && monitor < count)
        {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[monitor]);
            width = mode->width;
            height = mode->height;
        }
        else
        {
            width = 0;
            height = 0;
        }
    }

    int WindowsWindow::GetMonitorRefreshRate(int monitor) const
    {
        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);

        if (monitor >= 0 && monitor < count)
        {
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[monitor]);
            return mode->refreshRate;
        }

        return 0;
    }

    void WindowsWindow::GetMonitorPosition(int monitor, int& x, int& y) const
    {
        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);

        if (monitor >= 0 && monitor < count)
            glfwGetMonitorPos(monitors[monitor], &x, &y);
        else
        {
            x = 0;
            y = 0;
        }
    }

    const char* WindowsWindow::GetMonitorName(int monitor) const
    {
        int count;
        GLFWmonitor** monitors = glfwGetMonitors(&count);

        if (monitor >= 0 && monitor < count)
            return glfwGetMonitorName(monitors[monitor]);

        return "Unknown";
    }
#endif
}