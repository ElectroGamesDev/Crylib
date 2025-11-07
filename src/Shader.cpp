#include "Shader.h"
#include "Texture.h"
#include <fstream>
#include <vector>
#include <iostream>
#include <bgfx.h>

namespace cl
{
    std::vector<Shader*> Shader::s_shaders;
    Shader* s_defaultShader = nullptr;

    struct ShaderImpl
    {
        bgfx::ShaderHandle vertex = BGFX_INVALID_HANDLE;
        bgfx::ShaderHandle fragment = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;
        mutable std::unordered_map<std::string, bgfx::UniformHandle> uniforms;
        mutable std::unordered_map<std::string, bgfx::UniformHandle> samplerUniforms;
        mutable std::unordered_map<std::string, uint8_t> samplerStages;
        mutable uint8_t nextSamplerStage = 0;
    };

    Shader* LoadDefaultShader(std::string_view vertexPath, std::string_view fragmentPath)
    {
        if (s_defaultShader)
            delete s_defaultShader;

        s_defaultShader = new Shader();
        s_defaultShader->Load(vertexPath, fragmentPath);

        return s_defaultShader;
    }
    Shader* GetDefaultShader()
    {
        return s_defaultShader;
    }

    std::vector<uint8_t> ReadFileBytes(std::string_view path)
    {
        std::vector<uint8_t> data;
        std::ifstream in(path.data(), std::ios::binary | std::ios::ate);
        if (!in)
            return data;

        std::streamsize size = in.tellg();
        in.seekg(0, std::ios::beg);
        data.resize(size);
        if (!in.read(reinterpret_cast<char*>(data.data()), size))
            data.clear();

        return data;
    }

    // Convert UniformType to bgfx type
    static bgfx::UniformType::Enum ToBgfxUniformType(UniformType type)
    {
        switch (type)
        {
        case UniformType::Vec4:
            return bgfx::UniformType::Vec4;
        case UniformType::Mat3:
            return bgfx::UniformType::Mat3;
        case UniformType::Mat4:
            return bgfx::UniformType::Mat4;
        case UniformType::Sampler:
            return bgfx::UniformType::Sampler;
        default:
            return bgfx::UniformType::Vec4;
        }
    }

    Shader::Shader()
        : m_impl(new ShaderImpl())
    {
        s_shaders.push_back(this);
    }

    Shader::~Shader()
    {
        Destroy();
        delete m_impl;

        for (size_t i = 0; i < s_shaders.size(); ++i)
        {
            if (s_shaders[i] == this)
            {
                if (i != s_shaders.size() - 1)
                    std::swap(s_shaders[i], s_shaders.back());

                s_shaders.pop_back();
                return;
            }
        }
    }

    Shader::Shader(Shader&& other) noexcept
        : m_impl(other.m_impl)
    {
        other.m_impl = nullptr;
        s_shaders.push_back(this);
    }

    Shader& Shader::operator=(Shader&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            delete m_impl;
            m_impl = other.m_impl;
            other.m_impl = nullptr;
        }

        return *this;
    }

    void* Shader::LoadShaderFile(std::string_view path) const
    {
        auto bytes = ReadFileBytes(path.data());
        if (bytes.empty())
        {
            std::cerr << "[ERROR] Shader - failed to load \"" << path << "\". The file is either empty or not found." << std::endl;
            return nullptr;
        }

        const bgfx::Memory* mem = bgfx::copy(bytes.data(), static_cast<uint32_t>(bytes.size()));
        bgfx::ShaderHandle sh = bgfx::createShader(mem);
        if (!bgfx::isValid(sh))
        {
            std::cerr << "[ERROR] Shader - failed to load \"" << path << "\"" << std::endl;
            return nullptr;
        }

        // Return as void* to hide bgfx type
        bgfx::ShaderHandle* handle = new bgfx::ShaderHandle(sh);
        return handle;
    }

    bool Shader::Load(std::string_view vertexPath, std::string_view fragmentPath)
    {
        if (!m_impl)
            return false;

        Destroy();

        void* vPtr = LoadShaderFile(vertexPath);
        void* fPtr = LoadShaderFile(fragmentPath);

        if (!vPtr || !fPtr)
        {
            if (vPtr) delete static_cast<bgfx::ShaderHandle*>(vPtr);
            if (fPtr) delete static_cast<bgfx::ShaderHandle*>(fPtr);
            return false;
        }

        m_impl->vertex = *static_cast<bgfx::ShaderHandle*>(vPtr);
        m_impl->fragment = *static_cast<bgfx::ShaderHandle*>(fPtr);
        delete static_cast<bgfx::ShaderHandle*>(vPtr);
        delete static_cast<bgfx::ShaderHandle*>(fPtr);

        if (!bgfx::isValid(m_impl->vertex) || !bgfx::isValid(m_impl->fragment))
        {
            Destroy();
            return false;
        }

        m_impl->program = bgfx::createProgram(m_impl->vertex, m_impl->fragment, true);
        if (!bgfx::isValid(m_impl->program))
        {
            std::cerr << "Shader::Load: failed to create program\n";
            Destroy();
            return false;
        }

        // Reset caches
        m_impl->uniforms.clear();
        m_impl->samplerUniforms.clear();
        m_impl->samplerStages.clear();
        m_impl->nextSamplerStage = 0;
        return true;
    }

    void Shader::Destroy()
    {
        if (!m_impl)
            return;

        if (bgfx::isValid(m_impl->program))
        {
            bgfx::destroy(m_impl->program);
            m_impl->program = BGFX_INVALID_HANDLE;
        }

        // If program didn't own shaders, destroy individually
        if (bgfx::isValid(m_impl->vertex))
        {
            bgfx::destroy(m_impl->vertex);
            m_impl->vertex = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_impl->fragment))
        {
            bgfx::destroy(m_impl->fragment);
            m_impl->fragment = BGFX_INVALID_HANDLE;
        }

        // Destroy uniforms and samplers
        for (auto& p : m_impl->uniforms)
        {
            if (bgfx::isValid(p.second))
                bgfx::destroy(p.second);
        }
        m_impl->uniforms.clear();

        for (auto& p : m_impl->samplerUniforms)
        {
            if (bgfx::isValid(p.second))
                bgfx::destroy(p.second);
        }
        m_impl->samplerUniforms.clear();

        m_impl->samplerStages.clear();
        m_impl->nextSamplerStage = 0;
    }

    bool Shader::IsValid() const
    {
        return m_impl && bgfx::isValid(m_impl->program);
    }

    // Uniform functions

    bool Shader::HasUniform(std::string_view name) const
    {
        if (!m_impl)
            return false;

        // Todo: Should this check from m_Uniforms instead?
        return m_impl->uniforms.find(name.data()) != m_impl->uniforms.end() || m_impl->samplerUniforms.find(name.data()) != m_impl->samplerUniforms.end();
    }

    void Shader::SetUniform(std::string_view name, float v)
    {
        m_Uniforms.push_back({ name.data(), UniformType::Vec4, v });
    }


    void Shader::SetUniform(std::string_view name, const int v)
    {
        m_Uniforms.push_back({ name.data(), UniformType::Vec4, v });
    }

    void Shader::SetUniform(std::string_view name, const float(&v2)[2])
    {
        m_Uniforms.push_back({ name.data(), UniformType::Vec4, std::array<float, 2>{ v2[0], v2[1] } });
    }

    void Shader::SetUniform(std::string_view name, const float(&v3)[3])
    {
        m_Uniforms.push_back({ name.data(), UniformType::Vec4, std::array<float, 3>{ v3[0], v3[1], v3[2] } });
    }

    void Shader::SetUniform(std::string_view name, const float(&v4)[4])
    {
        m_Uniforms.push_back({ name.data(), UniformType::Vec4, std::array<float, 4>{ v4[0], v4[1], v4[2], v4[3] } });
    }

    void Shader::SetUniform(std::string_view name, const float(&m4)[16])
    {
        std::array<float, 16> arr;
        std::copy(std::begin(m4), std::end(m4), arr.begin());
        m_Uniforms.push_back({ name.data(), UniformType::Mat4, arr });
    }

    void Shader::SetUniform(std::string_view name, Texture* texture)
    {
        m_Uniforms.push_back({ name.data(), UniformType::Sampler, texture });
    }

    // Internal uniform functions

    void* Shader::GetOrCreateUniform(std::string_view name, UniformType type, uint16_t num) const
    {
        if (!m_impl)
            return nullptr;

        auto it = m_impl->uniforms.find(name.data());
        if (it != m_impl->uniforms.end())
        {
            bgfx::UniformHandle* handle = new bgfx::UniformHandle(it->second);
            return handle;
        }

        bgfx::UniformHandle h = bgfx::createUniform(name.data(), ToBgfxUniformType(type), num);
        if (!bgfx::isValid(h))
        {
            std::cerr << "Shader::getOrCreateUniform: failed to create uniform " << name << std::endl;
            return nullptr;
        }
        m_impl->uniforms[name.data()] = h;

        bgfx::UniformHandle* handle = new bgfx::UniformHandle(h);
        return handle;
    }

    void Shader::SetUniformInternal(std::string_view name, float v) const
    {
        if (!m_impl)
            return;

        float tmp[4] = { v, 0.0f, 0.0f, 0.0f };
        void* hPtr = GetOrCreateUniform(name.data(), UniformType::Vec4, 1);
        if (hPtr)
        {
            bgfx::UniformHandle h = *static_cast<bgfx::UniformHandle*>(hPtr);
            if (bgfx::isValid(h))
                bgfx::setUniform(h, tmp);
            delete static_cast<bgfx::UniformHandle*>(hPtr);
        }
    }

    void Shader::SetUniformInternal(std::string_view name, int v) const
    {
        if (!m_impl)
            return;

        // Pack int as float in a Vec4 uniform
        // In shader, you can cast back: int myInt = int(u_intUniform.x);
        float tmp[4] = { static_cast<float>(v), 0.0f, 0.0f, 0.0f };
        void* hPtr = GetOrCreateUniform(name.data(), UniformType::Vec4, 1);
        if (hPtr)
        {
            bgfx::UniformHandle h = *static_cast<bgfx::UniformHandle*>(hPtr);
            if (bgfx::isValid(h))
                bgfx::setUniform(h, tmp);
            delete static_cast<bgfx::UniformHandle*>(hPtr);
        }
    }

    void Shader::SetUniformInternal(std::string_view name, const float(&v2)[2]) const
    {
        if (!m_impl)
            return;
        float tmp[4] = { v2[0], v2[1], 0.0f, 0.0f };
        void* hPtr = GetOrCreateUniform(name.data(), UniformType::Vec4, 1);
        if (hPtr)
        {
            bgfx::UniformHandle h = *static_cast<bgfx::UniformHandle*>(hPtr);
            if (bgfx::isValid(h))
                bgfx::setUniform(h, tmp);
            delete static_cast<bgfx::UniformHandle*>(hPtr);
        }
    }

    void Shader::SetUniformInternal(std::string_view name, const float(&v3)[3]) const
    {
        if (!m_impl)
            return;
        float tmp[4] = { v3[0], v3[1], v3[2], 0.0f };
        void* hPtr = GetOrCreateUniform(name.data(), UniformType::Vec4, 1);
        if (hPtr)
        {
            bgfx::UniformHandle h = *static_cast<bgfx::UniformHandle*>(hPtr);
            if (bgfx::isValid(h))
                bgfx::setUniform(h, tmp);
            delete static_cast<bgfx::UniformHandle*>(hPtr);
        }
    }

    void Shader::SetUniformInternal(std::string_view name, const float(&v4)[4]) const
    {
        if (!m_impl)
            return;
        void* hPtr = GetOrCreateUniform(name.data(), UniformType::Vec4, 1);
        if (hPtr)
        {
            bgfx::UniformHandle h = *static_cast<bgfx::UniformHandle*>(hPtr);
            if (bgfx::isValid(h))
                bgfx::setUniform(h, v4);
            delete static_cast<bgfx::UniformHandle*>(hPtr);
        }
    }

    void Shader::SetUniformInternal(std::string_view name, const float(&m4)[16]) const
    {
        if (!m_impl)
            return;
        void* hPtr = GetOrCreateUniform(name.data(), UniformType::Mat4, 1);
        if (hPtr)
        {
            bgfx::UniformHandle h = *static_cast<bgfx::UniformHandle*>(hPtr);
            if (bgfx::isValid(h))
                bgfx::setUniform(h, m4);
            delete static_cast<bgfx::UniformHandle*>(hPtr);
        }
    }

    void Shader::SetUniformInternal(std::string_view name, const Texture* texture) const
    {
        if (!m_impl || !texture)
            return;
        bgfx::TextureHandle handle = texture->GetHandle();
        if (!bgfx::isValid(handle))
            return;
        // Get or create sampler uniform
        auto itSampler = m_impl->samplerUniforms.find(name.data());
        bgfx::UniformHandle sampler = BGFX_INVALID_HANDLE;
        if (itSampler == m_impl->samplerUniforms.end())
        {
            sampler = bgfx::createUniform(name.data(), bgfx::UniformType::Sampler);
            if (!bgfx::isValid(sampler))
            {
                std::cerr << "Shader::SetUniform: failed to create sampler uniform " << name << std::endl;
                return;
            }
            m_impl->samplerUniforms[name.data()] = sampler;
        }
        else
            sampler = itSampler->second;

        uint8_t stage = 255;
        if (name == "u_AlbedoMap")
            stage = 0;
        else if (name == "u_NormalMap")
            stage = 1;
        else if (name == "u_MetallicMap")
            stage = 2;
        else if (name == "u_RoughnessMap")
            stage = 3;
        else if (name == "u_MetallicRoughnessMap")
            stage = 4;
        else if (name == "u_AOMap")
            stage = 5;
        else if (name == "u_EmissiveMap")
            stage = 6;
        else if (name == "u_HeightMap")
            stage = 7;
        else if (name == "u_OpacityMap")
            stage = 8;
        else
        {
            std::cerr << "[Error] Shader::SetUniform - Unknown sampler name " << name << std::endl;
            return;
        }

        // Cache the fixed stage if not already set
        auto itStage = m_impl->samplerStages.find(name.data());
        if (itStage == m_impl->samplerStages.end())
            m_impl->samplerStages[name.data()] = stage;
        else if (itStage->second != stage)
        {
            std::cerr << "[Error] Shader::SetUniform - Stage mismatch for " << name << std::endl;
            return;
        }

        bgfx::setTexture(stage, sampler, handle);
    }

    bgfx::ProgramHandle Shader::GetHandle() const
    {
        if (!m_impl || !bgfx::isValid(m_impl->program))
            return BGFX_INVALID_HANDLE;

        return m_impl->program;
    }

    void Shader::ApplyUniforms()
    {
        for (ShaderUniform& param : m_Uniforms)
        {
            std::visit([this, param](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>) {
                    SetUniformInternal(param.name, arg);
                }
                else if constexpr (std::is_same_v<T, int>) {
                    SetUniformInternal(param.name, arg);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 2>>) {
                    const float(&v2)[2] = *reinterpret_cast<const float(*)[2]>(arg.data());
                    SetUniformInternal(param.name, v2);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
                    const float(&v3)[3] = *reinterpret_cast<const float(*)[3]>(arg.data());
                    SetUniformInternal(param.name, v3);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
                    const float(&v4)[4] = *reinterpret_cast<const float(*)[4]>(arg.data());
                    SetUniformInternal(param.name, v4);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 16>>) {
                    const float(&m4)[16] = *reinterpret_cast<const float(*)[16]>(arg.data());
                    SetUniformInternal(param.name, m4);
                }
                else if constexpr (std::is_same_v<T, Texture*>) {
                    SetUniformInternal(param.name, arg);
                }
            }, param.value);
        }
    }
}