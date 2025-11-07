#pragma once

#include <string>
#include <unordered_map>
#include <variant>
#include <memory>
#include "Maths.h"
#include "Shader.h"
#include "Texture.h"
#include <array>

namespace cl
{
    using MaterialParam = std::variant<float, int, Vector2, Vector3, Vector4, Matrix4>;

    // PBR Material Map Types
    enum class MaterialMapType
    {
        Albedo = 0, // Base color/diffuse map
        Normal = 1, // Normal map for surface detail
        Metallic = 2, // Metallic map (white = metal, black = dielectric)
        Roughness = 3, // Roughness map (white = rough, black = smooth)
        AO = 4, // Ambient Occlusion map
        Emissive = 5, // Emissive/glow map
        Height = 6, // Height/displacement map
        MetallicRoughness = 7, // Combined metallic (B) + roughness (G) in one texture
        Opacity = 8, // Opacity/transparency map
        Count = 9
    };

    class Material // Todo: Consider adding a default Normal map
    {
    public:
        Material() = default;
        ~Material() = default;
        Material(const Material&) = default;
        Material& operator=(const Material&) = default;
        Material(Material&&) noexcept = default;
        Material& operator=(Material&&) noexcept = default;

        // Shader management
        void SetShader(Shader* shader);
        Shader* GetShader() const;

        // PBR Material Map management
        void SetMaterialMap(MaterialMapType type, Texture* texture);
        Texture* GetMaterialMap(MaterialMapType type) const;
        bool HasMaterialMap(MaterialMapType type) const;
        void RemoveMaterialMap(MaterialMapType type);
        void ClearMaterialMaps();

        // PBR Properties
        void SetAlbedo(const Color& color);
        void SetMetallic(float value);
        void SetRoughness(float value);
        void SetEmissive(const Color& color);
        void SetAO(float value);

        Color GetAlbedo() const { return m_albedo; }
        float GetMetallic() const { return m_metallic; }
        float GetRoughness() const { return m_roughness; }
        Color GetEmissive() const { return m_emissive; }
        float GetAO() const { return m_ao; }

        // User parameter management
        /// Stores parameters for your own usage. Does not affect materials or shaders.
        void SetUserParam(std::string_view name, const MaterialParam& param);
        const MaterialParam* GetUserParam(std::string_view name) const;
        bool HasUserParam(std::string_view name) const;
        void RemoveUserParam(std::string_view name);
        void ClearUserParams();

        // Shader parameter management
        /// Sets shader float uniforms only for this material. Use SetUniform() for global shader uniforms.
        void SetShaderParam(std::string_view name, const float v);
        /// Sets shader int uniforms only for this material. Use SetUniform() for global shader uniforms.
        void SetShaderParam(std::string_view name, const int v);
        /// Sets shader vector2 (float[2]) uniforms only for this material. Use SetUniform() for global shader uniforms.
        void SetShaderParam(std::string_view name, const float(&v2)[2]);
        /// Sets shader vector3 (float[3]) uniforms only for this material. Use SetUniform() for global shader uniforms.
        void SetShaderParam(std::string_view name, const float(&v3)[3]);
        /// Sets shader vector4/quaternion (float[4]) uniforms only for this material. Use SetUniform() for global shader uniforms.
        void SetShaderParam(std::string_view name, const float(&v4)[4]);
        /// Sets shader matrix (float[16]) uniforms only for this material. Use SetUniform() for global shader uniforms.
        void SetShaderParam(std::string_view name, const float(&m4)[16]);
        /// Sets shader texture uniforms only for this material. Use SetUniform() for global shader uniforms.
        void SetShaderParam(std::string_view name, Texture* texture);

        /// Applies the material shader parameters to the shader uniforms. WARNING: This should be only used internally.
        void ApplyShaderUniforms();

        /// Applies PBR material maps and properties to shader uniforms. WARNING: This should be only used internally.
        void ApplyPBRUniforms();

        // Clear all material data
        void Clear();

    private:
        Shader* m_shader = nullptr;

        // PBR Material maps
        std::array<Texture*, static_cast<size_t>(MaterialMapType::Count)> m_materialMaps = {};

        // PBR Properties
        Color m_albedo = Color::White();
        float m_metallic = 0.0f;
        float m_roughness = 0.5f;
        Color m_emissive = Color::White();
        float m_ao = 1.0f;

        std::unordered_map<std::string, MaterialParam> m_UserParams;
        std::vector<ShaderUniform> m_ShaderParams;
    };
}