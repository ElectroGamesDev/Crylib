#pragma once

#include <bgfx.h>
#include <vector>
#include <string_view>

namespace cl
{
    class Texture
    {
    public:
        static std::vector<Texture*> s_textures; // Used for cl::Shutdown()

        Texture();
        ~Texture();

        bool LoadFromFile(std::string_view filepath, bool isColorTexture = true);
        bool LoadFromMemory(const void* data, int width, int height, int channels, bool isColorTexture = true);

        void Destroy();

        bgfx::TextureHandle GetHandle() const { return m_handle; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }
        bool IsValid() const { return bgfx::isValid(m_handle); }

    private:
        bgfx::TextureHandle m_handle;
        int m_width;
        int m_height;
    };
}