#include "Texture.h"
#include <bimg.h>
#include <decode.h>
#include <bx/bx.h>
#include <bx/allocator.h>
#include <bx/file.h>
#include <iostream>

namespace cl
{
    std::vector<Texture*> Texture::s_textures;
    static bx::DefaultAllocator s_allocator;

    Texture::Texture()
        : m_handle(BGFX_INVALID_HANDLE)
        , m_width(0)
        , m_height(0)
    {
        s_textures.push_back(this);
    }

    Texture::~Texture()
    {
        Destroy();

        for (size_t i = 0; i < s_textures.size(); ++i)
        {
            if (s_textures[i] == this)
            {
                if (i != s_textures.size() - 1)
                    std::swap(s_textures[i], s_textures.back());

                s_textures.pop_back();
                return;
            }
        }
    }

    bool Texture::LoadFromFile(std::string_view filepath, bool isColorTexture)
    {
        bx::FileReaderI* reader = BX_NEW(&s_allocator, bx::FileReader);

        if (!bx::open(reader, filepath.data()))
        {
            bx::deleteObject(&s_allocator, reader);
            return false;
        }

        uint32_t size = (uint32_t)bx::getSize(reader);
        void* data = bx::alloc(&s_allocator, size);
        bx::read(reader, data, size, bx::ErrorAssert{});
        bx::close(reader);
        bx::deleteObject(&s_allocator, reader);

        bimg::ImageContainer* imageContainer = bimg::imageParse(&s_allocator, data, size);

        if (!imageContainer)
        {
            bx::free(&s_allocator, data);
            return false;
        }


        uint64_t flags = 0;
        if (isColorTexture)
            flags |= BGFX_TEXTURE_SRGB;

        const bgfx::Memory* mem = bgfx::makeRef(imageContainer->m_data, imageContainer->m_size);

        // Todo: Add sampler flag presets, and customs that the user can modify. For example, default, sharp, anisotropic, pixel art, etc.
        m_handle = bgfx::createTexture2D(
            uint16_t(imageContainer->m_width),
            uint16_t(imageContainer->m_height),
            imageContainer->m_numMips > 1,
            imageContainer->m_numLayers,
            bgfx::TextureFormat::Enum(imageContainer->m_format),
            flags,
            mem
        );

        m_width = imageContainer->m_width;
        m_height = imageContainer->m_height;

        bimg::imageFree(imageContainer);
        bx::free(&s_allocator, data);

        return bgfx::isValid(m_handle);
    }

    bool Texture::LoadFromMemory(const void* data, int width, int height, int channels, bool isColorTexture)
    {
        if (!data || width <= 0 || height <= 0)
        {
            std::cerr << "[ERROR] Failed to load texture from memory." << std::endl;
            return false;
        }

        // Todo: RGB8 may not be supported on some platforms like mobile or WebGL
        bgfx::TextureFormat::Enum format = bgfx::TextureFormat::RGBA8;
        if (channels == 2)
            format = bgfx::TextureFormat::RG8;
        else if (channels == 1)
            format = bgfx::TextureFormat::R8;

        uint64_t flags = 0;
        if (isColorTexture)
            flags |= BGFX_TEXTURE_SRGB;

        const bgfx::Memory* mem = bgfx::copy(data, static_cast<uint32_t>(width * height * channels));

        // Todo: Add sampler flag presets, and customs that the user can modify. For example, default, sharp, anisotropic, pixel art, etc.
        m_handle = bgfx::createTexture2D(static_cast<uint16_t>(width), static_cast<uint16_t>(height), false, 1, format, flags, mem);

        m_width = width;
        m_height = height;

        bool valid = bgfx::isValid(m_handle);
        if (!valid)
            std::cerr << "[ERROR] Failed to load texture from memory." << std::endl;

        return valid;
    }

    void Texture::Destroy()
    {
        if (bgfx::isValid(m_handle))
        {
            bgfx::destroy(m_handle);
            m_handle = BGFX_INVALID_HANDLE;
        }

        m_width = 0;
        m_height = 0;
    }
}