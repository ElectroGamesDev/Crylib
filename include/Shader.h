#pragma once
#include <string>
#include <unordered_map>
#include <cstdint>
#include "Texture.h"
#include <variant>
#include <array>
#include <bgfx.h>

namespace cl
{
    struct ShaderImpl;

    enum class UniformType
    {
        Vec4, // Used for float, int, vec2, vec3, and vec4
        Mat3,
        Mat4,
        Sampler
    };

    struct ShaderUniform
    {
        std::string name;
        UniformType type;
        std::variant<
            float,
            int,
            std::array<float, 2>,
            std::array<float, 3>,
            std::array<float, 4>,
            std::array<float, 16>,
            Texture*> value;
    };

    class Shader
    {
        friend class Material;
    public:
        static std::vector<Shader*> s_shaders; // Used for cl::Shutdown()

        Shader();
        ~Shader();

        Shader(const Shader&) = delete;
        Shader& operator=(const Shader&) = delete;

        Shader(Shader&& other) noexcept;
        Shader& operator=(Shader&& other) noexcept;

        bool Load(std::string_view vertexPath, std::string_view fragmentPath);
        void Destroy();
        bool IsValid() const;

        // Uniform setters
        void SetUniform(std::string_view name, const float v);
        void SetUniform(std::string_view name, const int v);
        void SetUniform(std::string_view name, const float(&v2)[2]);
        void SetUniform(std::string_view name, const float(&v3)[3]);
        void SetUniform(std::string_view name, const float(&v4)[4]);
        void SetUniform(std::string_view name, const float(&m4)[16]);
        void SetUniform(std::string_view name, Texture* texture);

        bool HasUniform(std::string_view name) const;

        bgfx::ProgramHandle GetHandle() const;

        /// <summary>
        /// Applys the shader uniforms. WARNING: This should be only used internally.
        /// </summary>
        void ApplyUniforms();

    private:
        ShaderImpl* m_impl;
        std::vector<ShaderUniform> m_Uniforms;

        void* LoadShaderFile(std::string_view path) const;

        bgfx::UniformHandle GetOrCreateUniform(std::string_view name, UniformType type, uint16_t num) const;
        void SetUniformInternal(std::string_view name, const float v) const;
        void SetUniformInternal(std::string_view name, const int v) const;
        void SetUniformInternal(std::string_view name, const float(&v2)[2]) const;
        void SetUniformInternal(std::string_view name, const float(&v3)[3]) const;
        void SetUniformInternal(std::string_view name, const float(&v4)[4]) const;
        void SetUniformInternal(std::string_view name, const float(&m4)[16]) const;
        void SetUniformInternal(std::string_view name, const Texture* texture) const;
    };

    extern Shader* s_defaultShader;
    Shader* LoadDefaultShader(std::string_view vertexPath, std::string_view fragmentPath);
    Shader* GetDefaultShader();
}