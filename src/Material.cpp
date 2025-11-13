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
            std::visit([this, &param](auto&& arg)
            {
                using T = std::decay_t<decltype(arg)>;

                if (!bgfx::isValid(param.cachedUniform))
                {
                    if constexpr (std::is_same_v<T, Texture*>)
                    {
                        param.cachedUniform = m_shader->GetOrCreateSamplerUniform(param.name);
                        param.cachedStage = m_shader->GetSamplerStage(param.name);
                    }
                    else
                    {
                        UniformType utype;

                        if constexpr (std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, std::array<float, 2>> || std::is_same_v<T, std::array<float, 3>> || std::is_same_v<T, std::array<float, 4>>)
                            utype = UniformType::Vec4;
                        else if constexpr (std::is_same_v<T, std::array<float, 16>>)
                            utype = UniformType::Mat4;

                        param.cachedUniform = m_shader->GetOrCreateUniform(param.name, utype, 1);
                    }
                }

                if constexpr (std::is_same_v<T, Texture*>)
                {
                    if (arg && bgfx::isValid(param.cachedUniform) && param.cachedStage != 255)
                    {
                        bgfx::TextureHandle handle = arg->GetHandle();
                        if (bgfx::isValid(handle))
                            bgfx::setTexture(param.cachedStage, param.cachedUniform, handle);
                    }
                }
                else if (bgfx::isValid(param.cachedUniform))
                {
                    if constexpr (std::is_same_v<T, float>)
                    {
                        float tmp[4] = { arg, 0.0f, 0.0f, 0.0f };
                        bgfx::setUniform(param.cachedUniform, tmp);
                    }
                    else if constexpr (std::is_same_v<T, int>)
                    {
                        float tmp[4] = { static_cast<float>(arg), 0.0f, 0.0f, 0.0f };
                        bgfx::setUniform(param.cachedUniform, tmp);
                    }
                    else if constexpr (std::is_same_v<T, std::array<float, 2>>)
                    {
                        float tmp[4] = { arg[0], arg[1], 0.0f, 0.0f };
                        bgfx::setUniform(param.cachedUniform, tmp);
                    }
                    else if constexpr (std::is_same_v<T, std::array<float, 3>>)
                    {
                        float tmp[4] = { arg[0], arg[1], arg[2], 0.0f };
                        bgfx::setUniform(param.cachedUniform, tmp);
                    }
                    else if constexpr (std::is_same_v<T, std::array<float, 4>> || std::is_same_v<T, std::array<float, 16>>)
                        bgfx::setUniform(param.cachedUniform, arg.data());
                }
            }, param.value);
        }
    }

    void Material::ApplyPBRUniforms()
    {
        if (!m_shader)
            return;

        // We're not using SetUniformInternal() here because unordered_map lookups can get expensive when doing it tens of thousands of times each frame.
        // Todo: "u_MaterialFlags2" is set currently being set as a SetShaderParam() which should be changed.

        // Todo: This map code needs to be optimized

        // Build material flags as vec4 uniforms to match shader expectations
        float flags0[4] = {
            HasMaterialMap(MaterialMapType::Albedo) ? 1.0f : 0.0f,     // x = hasAlbedo
            HasMaterialMap(MaterialMapType::Normal) ? 1.0f : 0.0f,     // y = hasNormal
            HasMaterialMap(MaterialMapType::Metallic) ? 1.0f : 0.0f,   // z = hasMetallic
            HasMaterialMap(MaterialMapType::Roughness) ? 1.0f : 0.0f   // w = hasRoughness
        };

        if (!bgfx::isValid(m_hMaterialFlags0))
            m_hMaterialFlags0 = m_shader->GetOrCreateUniform("u_MaterialFlags0", UniformType::Vec4, 1);

        if (bgfx::isValid(m_hMaterialFlags0))
            bgfx::setUniform(m_hMaterialFlags0, flags0);

        float flags1[4] = {
            HasMaterialMap(MaterialMapType::MetallicRoughness) ? 1.0f : 0.0f, // x = hasMetallicRoughness
            HasMaterialMap(MaterialMapType::AO) ? 1.0f : 0.0f,                // y = hasAO
            HasMaterialMap(MaterialMapType::Emissive) ? 1.0f : 0.0f,          // z = hasEmissive
            HasMaterialMap(MaterialMapType::Opacity) ? 1.0f : 0.0f            // w = hasOpacity
        };

        if (!bgfx::isValid(m_hMaterialFlags1))
            m_hMaterialFlags1 = m_shader->GetOrCreateUniform("u_MaterialFlags1", UniformType::Vec4, 1);

        if (bgfx::isValid(m_hMaterialFlags1))
            bgfx::setUniform(m_hMaterialFlags1, flags1);

        // Texture samplers
        auto bindTexture = [&](MaterialMapType type, const char* uniformName, bgfx::UniformHandle& hSampler, uint8_t& stage)
            {
                if (HasMaterialMap(type))
                {
                    if (!bgfx::isValid(hSampler))
                    {
                        hSampler = m_shader->GetOrCreateSamplerUniform(uniformName);
                        stage = m_shader->GetSamplerStage(uniformName);
                    }

                    if (bgfx::isValid(hSampler) && stage != 255)
                        bgfx::setTexture(stage, hSampler, GetMaterialMap(type)->GetHandle());
                }
            };

        bindTexture(MaterialMapType::Albedo, "u_AlbedoMap", m_hSamplerAlbedo, m_stageAlbedo);
        bindTexture(MaterialMapType::Normal, "u_NormalMap", m_hSamplerNormal, m_stageNormal);
        bindTexture(MaterialMapType::Metallic, "u_MetallicMap", m_hSamplerMetallic, m_stageMetallic);
        bindTexture(MaterialMapType::Roughness, "u_RoughnessMap", m_hSamplerRoughness, m_stageRoughness);
        bindTexture(MaterialMapType::MetallicRoughness, "u_MetallicRoughnessMap", m_hSamplerMetallicRoughness, m_stageMetallicRoughness);
        bindTexture(MaterialMapType::AO, "u_AOMap", m_hSamplerAO, m_stageAO);
        bindTexture(MaterialMapType::Emissive, "u_EmissiveMap", m_hSamplerEmissive, m_stageEmissive);
        bindTexture(MaterialMapType::Height, "u_HeightMap", m_hSamplerHeight, m_stageHeight);
        bindTexture(MaterialMapType::Opacity, "u_OpacityMap", m_hSamplerOpacity, m_stageOpacity);

        // Set material properties
        float albedo[4] = {
            m_albedo.r / 255.0f,
            m_albedo.g / 255.0f,
            m_albedo.b / 255.0f,
            m_albedo.a / 255.0f
        };

        if (!bgfx::isValid(m_hAlbedo))
            m_hAlbedo = m_shader->GetOrCreateUniform("u_Albedo", UniformType::Vec4, 1);

        if (bgfx::isValid(m_hAlbedo))
            bgfx::setUniform(m_hAlbedo, albedo);

        float emissiveParams[4] = {
            m_emissive.r / 255.0f,
            m_emissive.g / 255.0f,
            m_emissive.b / 255.0f,
            0.0f
        };

        if (!bgfx::isValid(m_hEmissiveParams))
            m_hEmissiveParams = m_shader->GetOrCreateUniform("u_EmissiveParams", UniformType::Vec4, 1);

        if (bgfx::isValid(m_hEmissiveParams))
            bgfx::setUniform(m_hEmissiveParams, emissiveParams);

        // u_MaterialProps expects vec4 (x=metallic, y=roughness, z=ao, w=unused)
        float materialProps[4] = { m_metallic, m_roughness, m_ao, 0.0f };

        if (!bgfx::isValid(m_hMaterialProps))
            m_hMaterialProps = m_shader->GetOrCreateUniform("u_MaterialProps", UniformType::Vec4, 1);

        if (bgfx::isValid(m_hMaterialProps))
            bgfx::setUniform(m_hMaterialProps, materialProps);
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