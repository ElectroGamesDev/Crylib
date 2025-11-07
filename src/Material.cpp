#include "Material.h"
#include "Shader.h"
#include "Texture.h"
#include "Maths.h"
#include <iostream>
#include <algorithm>

namespace cl
{
    void Material::SetShader(Shader* shader)
    {
        m_shader = shader;
    }

    Shader* Material::GetShader() const
    {
        return m_shader;
    }

    // PBR Material Map management
    void Material::SetMaterialMap(MaterialMapType type, Texture* texture)
    {
        m_materialMaps[static_cast<size_t>(type)] = texture;
    }

    Texture* Material::GetMaterialMap(MaterialMapType type) const
    {
        return m_materialMaps[static_cast<size_t>(type)];
    }

    bool Material::HasMaterialMap(MaterialMapType type) const
    {
        return m_materialMaps[static_cast<size_t>(type)] != nullptr;
    }

    void Material::RemoveMaterialMap(MaterialMapType type)
    {
        m_materialMaps[static_cast<size_t>(type)] = nullptr;
    }

    void Material::ClearMaterialMaps()
    {
        m_materialMaps.fill(nullptr);
    }

    // PBR Properties
    void Material::SetAlbedo(const Color& color)
    {
        m_albedo = color;
    }

    void Material::SetMetallic(float value)
    {
        m_metallic = std::clamp(value, 0.0f, 1.0f);
    }

    void Material::SetRoughness(float value)
    {
        m_roughness = std::clamp(value, 0.0f, 1.0f);
    }

    void Material::SetEmissive(const Color& color)
    {
        m_emissive = color;
    }

    void Material::SetAO(float value)
    {
        m_ao = std::clamp(value, 0.0f, 1.0f);
    }

    // User parameters
    void Material::SetUserParam(std::string_view name, const MaterialParam& param)
    {
        m_UserParams[name.data()] = param;
    }

    const MaterialParam* Material::GetUserParam(std::string_view name) const
    {
        auto it = m_UserParams.find(name.data());
        if (it == m_UserParams.end())
            return nullptr;
        return &it->second;
    }

    bool Material::HasUserParam(std::string_view name) const
    {
        return m_UserParams.find(name.data()) != m_UserParams.end();
    }

    void Material::RemoveUserParam(std::string_view name)
    {
        m_UserParams.erase(name.data());
    }

    void Material::ClearUserParams()
    {
        m_UserParams.clear();
    }

    // Shader parameters
    void Material::SetShaderParam(std::string_view name, const float v)
    {
        m_ShaderParams.push_back({ name.data(), UniformType::Vec4, v });
    }

    void Material::SetShaderParam(std::string_view name, const int v)
    {
        m_ShaderParams.push_back({ name.data(), UniformType::Vec4, v });
    }

    void Material::SetShaderParam(std::string_view name, const float(&v2)[2])
    {
        m_ShaderParams.push_back({ name.data(), UniformType::Vec4, std::array<float, 2>{ v2[0], v2[1] } });
    }

    void Material::SetShaderParam(std::string_view name, const float(&v3)[3])
    {
        m_ShaderParams.push_back({ name.data(), UniformType::Vec4, std::array<float, 3>{ v3[0], v3[1], v3[2] } });
    }

    void Material::SetShaderParam(std::string_view name, const float(&v4)[4])
    {
        m_ShaderParams.push_back({ name.data(), UniformType::Vec4, std::array<float, 4>{ v4[0], v4[1], v4[2], v4[3] } });
    }

    void Material::SetShaderParam(std::string_view name, const float(&m4)[16])
    {
        std::array<float, 16> arr;
        std::copy(std::begin(m4), std::end(m4), arr.begin());
        m_ShaderParams.push_back({ name.data(), UniformType::Mat4, arr });
    }

    void Material::SetShaderParam(std::string_view name, Texture* texture)
    {
        m_ShaderParams.push_back({ name.data(), UniformType::Sampler, texture });
    }

    void Material::ApplyShaderUniforms()
    {
        for (ShaderUniform& param : m_ShaderParams)
        {
            std::visit([this, param](auto&& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, float>) {
                    m_shader->SetUniformInternal(param.name, arg);
                }
                else if constexpr (std::is_same_v<T, int>) {
                    m_shader->SetUniformInternal(param.name, arg);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 2>>) {
                    const float(&v2)[2] = *reinterpret_cast<const float(*)[2]>(arg.data());
                    m_shader->SetUniformInternal(param.name, v2);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 3>>) {
                    const float(&v3)[3] = *reinterpret_cast<const float(*)[3]>(arg.data());
                    m_shader->SetUniformInternal(param.name, v3);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 4>>) {
                    const float(&v4)[4] = *reinterpret_cast<const float(*)[4]>(arg.data());
                    m_shader->SetUniformInternal(param.name, v4);
                }
                else if constexpr (std::is_same_v<T, std::array<float, 16>>) {
                    const float(&m4)[16] = *reinterpret_cast<const float(*)[16]>(arg.data());
                    m_shader->SetUniformInternal(param.name, m4);
                }
                else if constexpr (std::is_same_v<T, Texture*>) {
                    m_shader->SetUniformInternal(param.name, arg);
                }
                }, param.value);
        }
    }

    void Material::ApplyPBRUniforms()
    {
        // Todo: Is calling this function each frame the best solution? The issue is, if two materials use the same shader, then it will need to call it each frame for each material 

        if (!m_shader)
            return;

        //m_shader->m_updateUniforms = true; // This shouldn't be needed since PBR global shader uniforms should not be set

        // Build material flags as vec4 uniforms to match shader expectations
        float flags0[4] = {
            HasMaterialMap(MaterialMapType::Albedo) ? 1.0f : 0.0f,     // x = hasAlbedo
            HasMaterialMap(MaterialMapType::Normal) ? 1.0f : 0.0f,     // y = hasNormal
            HasMaterialMap(MaterialMapType::Metallic) ? 1.0f : 0.0f,   // z = hasMetallic
            HasMaterialMap(MaterialMapType::Roughness) ? 1.0f : 0.0f   // w = hasRoughness
        };
        m_shader->SetUniformInternal("u_MaterialFlags0", flags0);

        float flags1[4] = {
            HasMaterialMap(MaterialMapType::MetallicRoughness) ? 1.0f : 0.0f, // x = hasMetallicRoughness
            HasMaterialMap(MaterialMapType::AO) ? 1.0f : 0.0f,                // y = hasAO
            HasMaterialMap(MaterialMapType::Emissive) ? 1.0f : 0.0f,          // z = hasEmissive
            HasMaterialMap(MaterialMapType::Opacity) ? 1.0f : 0.0f            // w = hasOpacity
        };
        m_shader->SetUniformInternal("u_MaterialFlags1", flags1);

        // Set texture samplers
        if (HasMaterialMap(MaterialMapType::Albedo))
            m_shader->SetUniformInternal("u_AlbedoMap", GetMaterialMap(MaterialMapType::Albedo));

        if (HasMaterialMap(MaterialMapType::Normal))
            m_shader->SetUniformInternal("u_NormalMap", GetMaterialMap(MaterialMapType::Normal));

        if (HasMaterialMap(MaterialMapType::Metallic))
            m_shader->SetUniformInternal("u_MetallicMap", GetMaterialMap(MaterialMapType::Metallic));

        if (HasMaterialMap(MaterialMapType::Roughness))
            m_shader->SetUniformInternal("u_RoughnessMap", GetMaterialMap(MaterialMapType::Roughness));

        if (HasMaterialMap(MaterialMapType::MetallicRoughness))
            m_shader->SetUniformInternal("u_MetallicRoughnessMap", GetMaterialMap(MaterialMapType::MetallicRoughness));

        if (HasMaterialMap(MaterialMapType::AO))
            m_shader->SetUniformInternal("u_AOMap", GetMaterialMap(MaterialMapType::AO));

        if (HasMaterialMap(MaterialMapType::Emissive))
            m_shader->SetUniformInternal("u_EmissiveMap", GetMaterialMap(MaterialMapType::Emissive));

        if (HasMaterialMap(MaterialMapType::Height))
            m_shader->SetUniformInternal("u_HeightMap", GetMaterialMap(MaterialMapType::Height));

        if (HasMaterialMap(MaterialMapType::Opacity))
            m_shader->SetUniformInternal("u_OpacityMap", GetMaterialMap(MaterialMapType::Opacity));

        // Set material properties (tints/fallback values)
        // Convert Color (0-255) to float (0.0-1.0)
        // u_Albedo expects vec4 (RGB albedo, A for base opacity)
        float albedo[4] = {
            m_albedo.r / 255.0f,
            m_albedo.g / 255.0f,
            m_albedo.b / 255.0f,
            m_albedo.a / 255.0f
        };
        m_shader->SetUniformInternal("u_Albedo", albedo);

        // u_EmissiveParams expects vec4 (RGB emissive, A unused)
        float emissiveParams[4] = {
            m_emissive.r / 255.0f,
            m_emissive.g / 255.0f,
            m_emissive.b / 255.0f,
            0.0f  // Alpha unused for emissive
        };
        m_shader->SetUniformInternal("u_EmissiveParams", emissiveParams);

        // u_MaterialProps expects vec4 (x=metallic, y=roughness, z=ao, w=unused)
        float materialProps[4] = { m_metallic, m_roughness, m_ao, 0.0f };
        m_shader->SetUniformInternal("u_MaterialProps", materialProps);
    }

    void Material::Clear()
    {
        m_shader = nullptr;
        m_materialMaps.fill(nullptr);
        m_UserParams.clear();
        m_ShaderParams.clear();

        m_albedo = Color::White();
        m_metallic = 0.0f;
        m_roughness = 0.5f;
        m_emissive = Color::Black();
        m_ao = 1.0f;
    }
}