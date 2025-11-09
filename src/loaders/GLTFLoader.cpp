#include "loaders/GLTFLoader.h"
#include "Maths.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <stb_image.h>
#include "basis universal/basisu_transcoder.h"
#include <cgltf.h>

namespace cl
{
    Model* LoadGLTF(std::string_view filePath)
    {
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return nullptr;
        }

        cgltf_options options = {};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, filePath.data(), &data);
        if (result != cgltf_result_success)
            return nullptr;

        result = cgltf_load_buffers(&options, data, filePath.data());

        if (result != cgltf_result_success)
        {
            cgltf_free(data);
            return nullptr;
        }

        Model* model = new Model();
        std::unordered_map<cgltf_material*, Material*> materialMap;
        std::unordered_map<cgltf_image*, Texture*> textureCache;
        std::filesystem::path path = filePath;

        Skeleton* skeleton = nullptr;
        std::unordered_map<cgltf_node*, int> nodeToJointMap;

        if (data->skins_count > 0)
        {
            skeleton = new Skeleton();
            cgltf_skin* skin = &data->skins[0];

            skeleton->bones.resize(skin->joints_count);

            for (size_t i = 0; i < skin->joints_count; ++i)
            {
                cgltf_node* joint = skin->joints[i];
                Bone& bone = skeleton->bones[i];

                if (joint->name)
                    bone.name = joint->name;
                else
                    bone.name = "Bone_" + std::to_string(i);

                nodeToJointMap[joint] = static_cast<int>(i);
                skeleton->boneMap[bone.name] = static_cast<int>(i);
            }

            for (size_t i = 0; i < skin->joints_count; ++i)
            {
                cgltf_node* joint = skin->joints[i];
                Bone& bone = skeleton->bones[i];

                bone.parentIndex = -1;
                if (joint->parent)
                {
                    auto it = nodeToJointMap.find(joint->parent);
                    if (it != nodeToJointMap.end())
                        bone.parentIndex = it->second;
                }

                if (joint->has_matrix)
                {
                    Matrix4 local;
                    for (int row = 0; row < 4; ++row)
                    {
                        for (int col = 0; col < 4; ++col)
                            local.m[col * 4 + row] = joint->matrix[row * 4 + col];
                    }
                    bone.localTransform = local;
                }
                else
                {
                    Matrix4 t = Matrix4::Identity();
                    Matrix4 r = Matrix4::Identity();
                    Matrix4 s = Matrix4::Identity();

                    if (joint->has_translation)
                        t = Matrix4::Translate(Vector3(joint->translation[0], joint->translation[1], joint->translation[2]));
                    if (joint->has_rotation)
                    {
                        Quaternion q(joint->rotation[0], joint->rotation[1], joint->rotation[2], joint->rotation[3]);
                        r = Matrix4::FromQuaternion(q);
                    }
                    if (joint->has_scale)
                        s = Matrix4::Scale(Vector3(joint->scale[0], joint->scale[1], joint->scale[2]));

                    bone.localTransform = t * r * s;
                }
            }

            if (skin->inverse_bind_matrices)
            {
                cgltf_accessor* accessor = skin->inverse_bind_matrices;
                for (size_t i = 0; i < skin->joints_count && i < accessor->count; ++i)
                {
                    float mat[16];
                    cgltf_accessor_read_float(accessor, i, mat, 16);

                    Matrix4& invBind = skeleton->bones[i].inverseBindMatrix;
                    for (int row = 0; row < 4; ++row)
                    {
                        for (int col = 0; col < 4; ++col)
                            invBind.m[col * 4 + row] = mat[row * 4 + col];
                    }
                }
            }

            model->SetSkeleton(skeleton);
            model->SetSkinned(true);
        }

        std::function<void(cgltf_node*, const Matrix4&)> ProcessNode;
        ProcessNode = [&](cgltf_node* node, const Matrix4& parentTransform)
            {
                Matrix4 local = Matrix4::Identity();
                if (node->has_matrix)
                {
                    local = Matrix4::Identity();
                    for (int row = 0; row < 4; ++row)
                    {
                        for (int col = 0; col < 4; ++col)
                            local.m[col * 4 + row] = node->matrix[row * 4 + col];
                    }
                }
                else
                {
                    if (node->has_translation)
                        local = local * Matrix4::Translate(Vector3(node->translation[0], node->translation[1], node->translation[2]));
                    if (node->has_rotation)
                    {
                        Quaternion q(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]);
                        local = local * Matrix4::FromQuaternion(q);
                    }
                    if (node->has_scale)
                        local = local * Matrix4::Scale(Vector3(node->scale[0], node->scale[1], node->scale[2]));
                }

                Matrix4 worldTransform = parentTransform * local;

                if (node->mesh)
                {
                    for (size_t j = 0; j < node->mesh->primitives_count; ++j)
                    {
                        cgltf_primitive& primitive = node->mesh->primitives[j];
                        auto mesh = std::make_shared<Mesh>();
                        std::vector<Vertex> vertices;
                        std::vector<uint32_t> indices;

                        bool hasSkin = (node->skin != nullptr);
                        mesh->SetSkinned(hasSkin);

                        Material* material = new Material();
                        material->SetShader(s_defaultShader);

                        for (size_t k = 0; k < primitive.attributes_count; ++k)
                        {
                            cgltf_attribute& attribute = primitive.attributes[k];
                            cgltf_accessor* accessor = attribute.data;
                            vertices.resize(accessor->count);

                            if (attribute.type == cgltf_attribute_type_position)
                            {
                                for (size_t v = 0; v < accessor->count; ++v)
                                {
                                    float pos[3];
                                    cgltf_accessor_read_float(accessor, v, pos, 3);
                                    if (hasSkin)
                                        vertices[v].position = Vector3(pos[0], pos[1], pos[2]);
                                    else
                                        vertices[v].position = worldTransform.TransformPoint(Vector3(pos[0], pos[1], pos[2]));
                                }
                            }
                            else if (attribute.type == cgltf_attribute_type_normal)
                            {
                                for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                                {
                                    float normal[3];
                                    cgltf_accessor_read_float(accessor, v, normal, 3);
                                    if (hasSkin)
                                        vertices[v].normal = Vector3(normal[0], normal[1], normal[2]).Normalize();
                                    else
                                        vertices[v].normal = worldTransform.TransformDirection(Vector3(normal[0], normal[1], normal[2])).Normalize();
                                }
                            }
                            else if (attribute.type == cgltf_attribute_type_tangent)
                            {
                                for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                                {
                                    float tangent[4];
                                    cgltf_accessor_read_float(accessor, v, tangent, 4);
                                    if (hasSkin)
                                    {
                                        vertices[v].tangent = Vector3(tangent[0], tangent[1], tangent[2]).Normalize();
                                        float sign = tangent[3];
                                        vertices[v].bitangent = Vector3::Cross(vertices[v].normal, vertices[v].tangent).Normalize() * sign;
                                    }
                                    else
                                    {
                                        vertices[v].tangent = worldTransform.TransformDirection(Vector3(tangent[0], tangent[1], tangent[2])).Normalize();
                                        float sign = tangent[3];
                                        vertices[v].bitangent = Vector3::Cross(vertices[v].normal, vertices[v].tangent).Normalize() * sign;
                                    }
                                }
                            }
                            else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0)
                            {
                                for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                                {
                                    float uv[2];
                                    cgltf_accessor_read_float(accessor, v, uv, 2);
                                    vertices[v].texCoord = Vector2(uv[0], uv[1]);
                                }
                            }
                            else if (attribute.type == cgltf_attribute_type_joints && attribute.index == 0)
                            {
                                for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                                {
                                    if (accessor->component_type == cgltf_component_type_r_8u || accessor->component_type == cgltf_component_type_r_16u)
                                    {
                                        uint32_t joints[4] = { 0, 0, 0, 0 };
                                        for (int c = 0; c < 4; ++c)
                                            joints[c] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, v * 4 + c));

                                        for (int c = 0; c < 4; ++c)
                                            vertices[v].boneIndices[c] = static_cast<float>(joints[c]);
                                    }
                                    else
                                    {
                                        float joints[4];
                                        cgltf_accessor_read_float(accessor, v, joints, 4);
                                        for (int c = 0; c < 4; ++c)
                                            vertices[v].boneIndices[c] = joints[c];
                                    }
                                }
                            }
                            else if (attribute.type == cgltf_attribute_type_weights && attribute.index == 0)
                            {
                                for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                                {
                                    float weights[4];
                                    cgltf_accessor_read_float(accessor, v, weights, 4);
                                    for (int c = 0; c < 4; ++c)
                                        vertices[v].boneWeights[c] = weights[c];
                                }
                            }
                        }

                        if (primitive.indices)
                        {
                            cgltf_accessor* accessor = primitive.indices;
                            indices.resize(accessor->count);
                            for (size_t idx = 0; idx < accessor->count; ++idx)
                                indices[idx] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, idx));
                        }

                        if (primitive.material)
                        {
                            auto it = materialMap.find(primitive.material);
                            if (it == materialMap.end())
                            {
                                auto& pbr = primitive.material->pbr_metallic_roughness;
                                material->SetAlbedo(Color(pbr.base_color_factor[0] * 255, pbr.base_color_factor[1] * 255, pbr.base_color_factor[2] * 255, pbr.base_color_factor[3] * 255));
                                material->SetMetallic(pbr.metallic_factor);
                                material->SetRoughness(pbr.roughness_factor);
                                material->SetEmissive(Color(primitive.material->emissive_factor[0] * 255, primitive.material->emissive_factor[1] * 255, primitive.material->emissive_factor[2] * 255, 1.0f));

                                auto loadAndSetMap = [&](cgltf_texture* tex, MaterialMapType type)
                                    {
                                        if (!tex || !tex->image)
                                            return;

                                        auto cached = textureCache.find(tex->image);
                                        if (cached != textureCache.end())
                                        {
                                            material->SetMaterialMap(type, cached->second);
                                            return;
                                        }

                                        cgltf_image* image = tex->image;
                                        uint8_t* data = nullptr;
                                        size_t size = 0;
                                        std::string mime = image->mime_type ? image->mime_type : "";
                                        bool isExternal = false;

                                        if (image && image->buffer_view && image->buffer_view->buffer->data)
                                        {
                                            data = static_cast<uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
                                            size = image->buffer_view->size;
                                        }
                                        else if (image && image->uri)
                                        {
                                            std::string texPath = path.parent_path().string() + "/" + image->uri;
                                            std::ifstream file(texPath, std::ios::binary | std::ios::ate);
                                            if (!file)
                                            {
                                                std::cerr << "[ERROR] Failed to open external texture: " << texPath << std::endl;
                                                return;
                                            }

                                            size = file.tellg();
                                            file.seekg(0);
                                            data = new uint8_t[size];
                                            file.read(reinterpret_cast<char*>(data), size);
                                            file.close();
                                            isExternal = true;
                                            if (mime.empty())
                                            {
                                                std::string ext = texPath.substr(texPath.find_last_of(".") + 1);
                                                if (ext == "png")
                                                    mime = "image/png";
                                                else if (ext == "jpg" || ext == "jpeg")
                                                    mime = "image/jpeg";
                                                else if (ext == "ktx2")
                                                    mime = "image/ktx2";
                                                else
                                                {
                                                    std::cerr << "[ERROR] Unknown extension for " << texPath << std::endl;
                                                    delete[] data;
                                                    return;
                                                }
                                            }
                                        }
                                        else
                                        {
                                            std::cerr << "[ERROR] No data or URI for texture" << std::endl;
                                            return;
                                        }

                                        if (mime.empty() && data && size >= 8)
                                        {
                                            if (data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
                                                mime = "image/png";
                                            else if (data[0] == 0xFF && data[1] == 0xD8)
                                                mime = "image/jpeg";
                                            else if (data[0] == 0xAB && data[1] == 'K' && data[2] == 'T' && data[3] == 'X')
                                                mime = "image/ktx2";
                                            else
                                            {
                                                std::cerr << "[ERROR] Unknown format (magic bytes: " << std::hex << static_cast<int>(data[0]) << " " << static_cast<int>(data[1]) << "...)" << std::endl;
                                                if (isExternal)
                                                    delete[] data;
                                                return;
                                            }
                                        }

                                        if (mime.empty())
                                        {
                                            std::cerr << "[ERROR] No mime type" << std::endl;
                                            if (isExternal)
                                                delete[] data;
                                            return;
                                        }

                                        Texture* texture = new Texture();
                                        bool success = false;

                                        if (mime == "image/png" || mime == "image/jpeg")
                                        {
                                            int width, height, channels;
                                            unsigned char* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width, &height, &channels, 4);
                                            if (pixels)
                                            {
                                                bool isColorTexture = (type == MaterialMapType::Albedo || type == MaterialMapType::Emissive);
                                                success = texture->LoadFromMemory(pixels, width, height, 4, isColorTexture);
                                                stbi_image_free(pixels);
                                            }
                                            else
                                                std::cerr << "[ERROR] Failed to load texture for " << mime << ": " << stbi_failure_reason() << std::endl;
                                        }
                                        else if (mime == "image/ktx2")
                                        {
                                            basist::ktx2_transcoder transcoder;
                                            if (transcoder.init(data, static_cast<uint32_t>(size)))
                                            {
                                                if (transcoder.start_transcoding())
                                                {
                                                    uint32_t width = transcoder.get_width();
                                                    uint32_t height = transcoder.get_height();
                                                    if (width > 0 && height > 0)
                                                    {
                                                        uint32_t dst_size = width * height * 4;
                                                        uint8_t* dst = new uint8_t[dst_size];

                                                        if (transcoder.transcode_image_level(0, 0, 0, dst, width * height, basist::transcoder_texture_format::cTFRGBA32))
                                                        {
                                                            bool isColorTexture = (type == MaterialMapType::Albedo || type == MaterialMapType::Emissive);
                                                            success = texture->LoadFromMemory(dst, width, height, 4, isColorTexture);
                                                        }
                                                        else
                                                            std::cerr << "[ERROR] BasisU transcode_image_level failed." << std::endl;
                                                        delete[] dst;
                                                    }
                                                    else
                                                        std::cerr << "[ERROR] Invalid dimensions from BasisU: " << width << "x" << height << std::endl;
                                                    transcoder.clear();
                                                }
                                                else
                                                    std::cerr << "[ERROR] BasisU start_transcoding failed." << std::endl;
                                            }
                                            else
                                                std::cerr << "[ERROR] BasisU init failed." << std::endl;
                                        }
                                        else
                                            std::cerr << "[ERROR] Unsupported mime type: " << mime << std::endl;

                                        if (isExternal)
                                            delete[] data;

                                        if (success)
                                        {
                                            material->SetMaterialMap(type, texture);
                                            textureCache[tex->image] = texture;
                                        }
                                        else
                                            delete texture;
                                    };

                                loadAndSetMap(pbr.base_color_texture.texture, MaterialMapType::Albedo);
                                loadAndSetMap(pbr.metallic_roughness_texture.texture, MaterialMapType::MetallicRoughness);
                                loadAndSetMap(primitive.material->normal_texture.texture, MaterialMapType::Normal);
                                loadAndSetMap(primitive.material->occlusion_texture.texture, MaterialMapType::AO);
                                loadAndSetMap(primitive.material->emissive_texture.texture, MaterialMapType::Emissive);
                                materialMap[primitive.material] = material;
                            }
                            else
                                material = it->second;
                        }

                        mesh->SetMaterial(material);
                        mesh->SetVertices(vertices);
                        mesh->SetIndices(indices);
                        mesh->Upload();
                        model->AddMesh(mesh);
                    }
                }

                for (size_t i = 0; i < node->children_count; ++i)
                    ProcessNode(node->children[i], worldTransform);
            };

        if (data->scenes_count > 0)
        {
            for (size_t i = 0; i < data->scenes[0].nodes_count; ++i)
                ProcessNode(data->scenes[0].nodes[i], Matrix4::Identity());
        }

        // Build a node-to-index map for all nodes
        std::unordered_map<cgltf_node*, int> nodeToIndexMap;
        for (size_t i = 0; i < data->nodes_count; ++i)
            nodeToIndexMap[&data->nodes[i]] = static_cast<int>(i);

        for (size_t i = 0; i < data->animations_count; ++i)
        {
            cgltf_animation* anim = &data->animations[i];
            AnimationClip* clip = new AnimationClip();

            if (anim->name)
                clip->SetName(anim->name);
            else
                clip->SetName("Animation_" + std::to_string(i));

            float maxTime = 0.0f;
            bool hasSkeletalChannels = false;
            bool hasNodeChannels = false;

            // First pass: determine animation type
            for (size_t j = 0; j < anim->channels_count; ++j)
            {
                cgltf_animation_channel* channel = &anim->channels[j];
                if (!channel->target_node)
                    continue;

                // Check if this node is a joint/bone
                if (nodeToJointMap.find(channel->target_node) != nodeToJointMap.end())
                    hasSkeletalChannels = true;
                else
                    hasNodeChannels = true;
            }

            // Set animation type based on what channels we found
            if (hasSkeletalChannels && !hasNodeChannels)
                clip->SetAnimationType(AnimationType::Skeletal);
            else if (!hasSkeletalChannels && hasNodeChannels)
                clip->SetAnimationType(AnimationType::NodeBased);
            else if (hasSkeletalChannels && hasNodeChannels) // Mixed animation
            {
                // Priortize skeletal animations
                clip->SetAnimationType(AnimationType::Skeletal);
                std::cout << "[WARNING] Animation '" << clip->GetName() << "' has both skeletal and node channels. Using skeletal mode.\n";
            }
            else // No valid channels

                clip->SetAnimationType(AnimationType::NodeBased);

            // Second pass: load animation channels based on type
            for (size_t j = 0; j < anim->channels_count; ++j)
            {
                cgltf_animation_channel* channel = &anim->channels[j];
                cgltf_animation_sampler* sampler = channel->sampler;

                if (!channel->target_node || !sampler)
                    continue;

                // Determine interpolation type
                AnimationInterpolation interpType = AnimationInterpolation::Linear;
                switch (sampler->interpolation)
                {
                case cgltf_interpolation_type_linear:
                    interpType = AnimationInterpolation::Linear;
                    break;
                case cgltf_interpolation_type_step:
                    interpType = AnimationInterpolation::Step;
                    break;
                case cgltf_interpolation_type_cubic_spline:
                    interpType = AnimationInterpolation::CubicSpline;
                    break;
                default:
                    interpType = AnimationInterpolation::Linear;
                    break;
                }

                // Read time data
                cgltf_accessor* timeAccessor = sampler->input;
                std::vector<float> times(timeAccessor->count);
                for (size_t t = 0; t < timeAccessor->count; ++t)
                {
                    cgltf_accessor_read_float(timeAccessor, t, &times[t], 1);
                    if (times[t] > maxTime)
                        maxTime = times[t];
                }

                cgltf_accessor* dataAccessor = sampler->output;

                // Check if this is a skeletal or node animation channel
                auto jointIt = nodeToJointMap.find(channel->target_node);
                bool isBoneChannel = (jointIt != nodeToJointMap.end());

                if (clip->GetAnimationType() == AnimationType::Skeletal && isBoneChannel)
                {
                    AnimationChannel animChannel;
                    animChannel.targetBoneIndex = jointIt->second;
                    animChannel.interpolation = interpType;
                    animChannel.times = times;

                    // Load transform data based on path type
                    if (channel->target_path == cgltf_animation_path_type_translation)
                    {
                        animChannel.translations.resize(dataAccessor->count);
                        for (size_t v = 0; v < dataAccessor->count; ++v)
                        {
                            float trans[3];
                            cgltf_accessor_read_float(dataAccessor, v, trans, 3);
                            animChannel.translations[v] = Vector3(trans[0], trans[1], trans[2]);
                        }
                    }
                    else if (channel->target_path == cgltf_animation_path_type_rotation)
                    {
                        animChannel.rotations.resize(dataAccessor->count);
                        for (size_t v = 0; v < dataAccessor->count; ++v)
                        {
                            float rot[4];
                            cgltf_accessor_read_float(dataAccessor, v, rot, 4);
                            animChannel.rotations[v] = Quaternion(rot[0], rot[1], rot[2], rot[3]);
                        }
                    }
                    else if (channel->target_path == cgltf_animation_path_type_scale)
                    {
                        animChannel.scales.resize(dataAccessor->count);
                        for (size_t v = 0; v < dataAccessor->count; ++v)
                        {
                            float scale[3];
                            cgltf_accessor_read_float(dataAccessor, v, scale, 3);
                            animChannel.scales[v] = Vector3(scale[0], scale[1], scale[2]);
                        }
                    }

                    // Fill in defaults for missing transform components
                    if (animChannel.translations.empty())
                        animChannel.translations.resize(times.size(), Vector3(0.0f, 0.0f, 0.0f));

                    if (animChannel.rotations.empty())
                        animChannel.rotations.resize(times.size(), Quaternion(0.0f, 0.0f, 0.0f, 1.0f));

                    if (animChannel.scales.empty())
                        animChannel.scales.resize(times.size(), Vector3(1.0f, 1.0f, 1.0f));

                    clip->AddChannel(animChannel);
                }
                else if (clip->GetAnimationType() == AnimationType::NodeBased)
                {
                    NodeAnimationChannel nodeChannel;

                    auto nodeIt = nodeToIndexMap.find(channel->target_node);
                    if (nodeIt != nodeToIndexMap.end())
                        nodeChannel.targetNodeIndex = nodeIt->second;
                    else
                    {
                        std::cout << "[WARNING] Node not found in node map for animation channel\n";
                        continue;
                    }

                    nodeChannel.interpolation = interpType;
                    nodeChannel.times = times;

                    // Load transform data based on path type
                    if (channel->target_path == cgltf_animation_path_type_translation)
                    {
                        nodeChannel.translations.resize(dataAccessor->count);
                        for (size_t v = 0; v < dataAccessor->count; ++v)
                        {
                            float trans[3];
                            cgltf_accessor_read_float(dataAccessor, v, trans, 3);
                            nodeChannel.translations[v] = Vector3(trans[0], trans[1], trans[2]);
                        }
                    }
                    else if (channel->target_path == cgltf_animation_path_type_rotation)
                    {
                        nodeChannel.rotations.resize(dataAccessor->count);
                        for (size_t v = 0; v < dataAccessor->count; ++v)
                        {
                            float rot[4];
                            cgltf_accessor_read_float(dataAccessor, v, rot, 4);
                            nodeChannel.rotations[v] = Quaternion(rot[0], rot[1], rot[2], rot[3]);
                        }
                    }
                    else if (channel->target_path == cgltf_animation_path_type_scale)
                    {
                        nodeChannel.scales.resize(dataAccessor->count);
                        for (size_t v = 0; v < dataAccessor->count; ++v)
                        {
                            float scale[3];
                            cgltf_accessor_read_float(dataAccessor, v, scale, 3);
                            nodeChannel.scales[v] = Vector3(scale[0], scale[1], scale[2]);
                        }
                    }

                    // Fill in defaults for missing transform components
                    if (nodeChannel.translations.empty())
                        nodeChannel.translations.resize(times.size(), Vector3(0.0f, 0.0f, 0.0f));

                    if (nodeChannel.rotations.empty())
                        nodeChannel.rotations.resize(times.size(), Quaternion(0.0f, 0.0f, 0.0f, 1.0f));

                    if (nodeChannel.scales.empty())
                        nodeChannel.scales.resize(times.size(), Vector3(1.0f, 1.0f, 1.0f));

                    clip->AddNodeChannel(nodeChannel);
                }
            }

            clip->SetDuration(maxTime);
            model->AddAnimation(clip);
        }

        model->SetNodeCount(static_cast<int>(data->nodes_count));

        cgltf_free(data);
        return model;
    }

    AnimationClip* LoadAnimationFromGLTF(std::string_view filePath, size_t animationIndex)
    {
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return nullptr;
        }

        cgltf_options options = {};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, filePath.data(), &data);
        if (result != cgltf_result_success)
            return nullptr;

        result = cgltf_load_buffers(&options, data, filePath.data());
        if (result != cgltf_result_success)
        {
            cgltf_free(data);
            return nullptr;
        }

        if (animationIndex >= data->animations_count)
        {
            cgltf_free(data);
            return nullptr;
        }

        std::unordered_map<cgltf_node*, int> nodeToJointMap;
        if (data->skins_count > 0)
        {
            cgltf_skin* skin = &data->skins[0];
            for (size_t i = 0; i < skin->joints_count; ++i)
                nodeToJointMap[skin->joints[i]] = static_cast<int>(i);
        }

        cgltf_animation* anim = &data->animations[animationIndex];
        AnimationClip* clip = new AnimationClip();

        if (anim->name)
            clip->SetName(anim->name);
        else
            clip->SetName("Animation_" + std::to_string(animationIndex));

        float maxTime = 0.0f;
        for (size_t j = 0; j < anim->channels_count; ++j)
        {
            cgltf_animation_channel* channel = &anim->channels[j];
            cgltf_animation_sampler* sampler = channel->sampler;
            if (!channel->target_node)
                continue;

            auto it = nodeToJointMap.find(channel->target_node);
            if (it == nodeToJointMap.end())
                continue;

            int boneIndex = it->second;
            AnimationChannel animChannel;
            animChannel.targetBoneIndex = boneIndex;

            switch (sampler->interpolation)
            {
            case cgltf_interpolation_type_linear:
                animChannel.interpolation = AnimationInterpolation::Linear;
                break;
            case cgltf_interpolation_type_step:
                animChannel.interpolation = AnimationInterpolation::Step;
                break;
            case cgltf_interpolation_type_cubic_spline:
                animChannel.interpolation = AnimationInterpolation::CubicSpline;
                break;
            default:
                animChannel.interpolation = AnimationInterpolation::Linear;
                break;
            }
            cgltf_accessor* timeAccessor = sampler->input;
            animChannel.times.resize(timeAccessor->count);

            for (size_t t = 0; t < timeAccessor->count; ++t)
            {
                cgltf_accessor_read_float(timeAccessor, t, &animChannel.times[t], 1);
                if (animChannel.times[t] > maxTime)
                    maxTime = animChannel.times[t];
            }

            cgltf_accessor* dataAccessor = sampler->output;
            if (channel->target_path == cgltf_animation_path_type_translation)
            {
                animChannel.translations.resize(dataAccessor->count);
                for (size_t v = 0; v < dataAccessor->count; ++v)
                {
                    float trans[3];
                    cgltf_accessor_read_float(dataAccessor, v, trans, 3);
                    animChannel.translations[v] = Vector3(trans[0], trans[1], trans[2]);
                }

                if (animChannel.rotations.empty())
                {
                    animChannel.rotations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.rotations.size(); ++v)
                        animChannel.rotations[v] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                }

                if (animChannel.scales.empty())
                {
                    animChannel.scales.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.scales.size(); ++v)
                        animChannel.scales[v] = Vector3(1.0f, 1.0f, 1.0f);
                }
            }
            else if (channel->target_path == cgltf_animation_path_type_rotation)
            {
                animChannel.rotations.resize(dataAccessor->count);
                for (size_t v = 0; v < dataAccessor->count; ++v)
                {
                    float rot[4];
                    cgltf_accessor_read_float(dataAccessor, v, rot, 4);
                    animChannel.rotations[v] = Quaternion(rot[0], rot[1], rot[2], rot[3]);
                }

                if (animChannel.translations.empty())
                {
                    animChannel.translations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.translations.size(); ++v)
                        animChannel.translations[v] = Vector3(0.0f, 0.0f, 0.0f);
                }

                if (animChannel.scales.empty())
                {
                    animChannel.scales.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.scales.size(); ++v)
                        animChannel.scales[v] = Vector3(1.0f, 1.0f, 1.0f);
                }
            }
            else if (channel->target_path == cgltf_animation_path_type_scale)
            {
                animChannel.scales.resize(dataAccessor->count);
                for (size_t v = 0; v < dataAccessor->count; ++v)
                {
                    float scale[3];
                    cgltf_accessor_read_float(dataAccessor, v, scale, 3);
                    animChannel.scales[v] = Vector3(scale[0], scale[1], scale[2]);
                }

                if (animChannel.translations.empty())
                {
                    animChannel.translations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.translations.size(); ++v)
                        animChannel.translations[v] = Vector3(0.0f, 0.0f, 0.0f);
                }

                if (animChannel.rotations.empty())
                {
                    animChannel.rotations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.rotations.size(); ++v)
                        animChannel.rotations[v] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                }
            }
            clip->AddChannel(animChannel);
        }
        clip->SetDuration(maxTime);
        cgltf_free(data);
        return clip;
    }

    AnimationClip* LoadAnimationFromGLTF(std::string_view filePath, std::string_view animationName)
    {
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return nullptr;
        }

        cgltf_options options = {};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, filePath.data(), &data);
        if (result != cgltf_result_success)
            return nullptr;

        result = cgltf_load_buffers(&options, data, filePath.data());
        if (result != cgltf_result_success)
        {
            cgltf_free(data);
            return nullptr;
        }

        std::unordered_map<cgltf_node*, int> nodeToJointMap;
        if (data->skins_count > 0)
        {
            cgltf_skin* skin = &data->skins[0];
            for (size_t i = 0; i < skin->joints_count; ++i)
                nodeToJointMap[skin->joints[i]] = static_cast<int>(i);
        }

        cgltf_animation* anim = nullptr;
        for (size_t i = 0; i < data->animations_count; ++i)
        {
            if (data->animations[i].name && std::string(data->animations[i].name) == animationName)
            {
                anim = &data->animations[i];
                break;
            }
        }

        if (!anim)
        {
            cgltf_free(data);
            return nullptr;
        }

        AnimationClip* clip = new AnimationClip();
        clip->SetName(animationName.data());
        float maxTime = 0.0f;

        for (size_t j = 0; j < anim->channels_count; ++j)
        {
            cgltf_animation_channel* channel = &anim->channels[j];
            cgltf_animation_sampler* sampler = channel->sampler;
            if (!channel->target_node)
                continue;

            auto it = nodeToJointMap.find(channel->target_node);
            if (it == nodeToJointMap.end())
                continue;

            int boneIndex = it->second;
            AnimationChannel animChannel;
            animChannel.targetBoneIndex = boneIndex;

            switch (sampler->interpolation)
            {
            case cgltf_interpolation_type_linear:
                animChannel.interpolation = AnimationInterpolation::Linear;
                break;
            case cgltf_interpolation_type_step:
                animChannel.interpolation = AnimationInterpolation::Step;
                break;
            case cgltf_interpolation_type_cubic_spline:
                animChannel.interpolation = AnimationInterpolation::CubicSpline;
                break;
            default:
                animChannel.interpolation = AnimationInterpolation::Linear;
                break;
            }

            cgltf_accessor* timeAccessor = sampler->input;
            animChannel.times.resize(timeAccessor->count);
            for (size_t t = 0; t < timeAccessor->count; ++t)
            {
                cgltf_accessor_read_float(timeAccessor, t, &animChannel.times[t], 1);
                if (animChannel.times[t] > maxTime)
                    maxTime = animChannel.times[t];
            }

            cgltf_accessor* dataAccessor = sampler->output;
            if (channel->target_path == cgltf_animation_path_type_translation)
            {
                animChannel.translations.resize(dataAccessor->count);
                for (size_t v = 0; v < dataAccessor->count; ++v)
                {
                    float trans[3];
                    cgltf_accessor_read_float(dataAccessor, v, trans, 3);
                    animChannel.translations[v] = Vector3(trans[0], trans[1], trans[2]);
                }

                if (animChannel.rotations.empty())
                {
                    animChannel.rotations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.rotations.size(); ++v)
                        animChannel.rotations[v] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                }

                if (animChannel.scales.empty())
                {
                    animChannel.scales.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.scales.size(); ++v)
                        animChannel.scales[v] = Vector3(1.0f, 1.0f, 1.0f);
                }
            }
            else if (channel->target_path == cgltf_animation_path_type_rotation)
            {
                animChannel.rotations.resize(dataAccessor->count);
                for (size_t v = 0; v < dataAccessor->count; ++v)
                {
                    float rot[4];
                    cgltf_accessor_read_float(dataAccessor, v, rot, 4);
                    animChannel.rotations[v] = Quaternion(rot[0], rot[1], rot[2], rot[3]);
                }

                if (animChannel.translations.empty())
                {
                    animChannel.translations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.translations.size(); ++v)
                        animChannel.translations[v] = Vector3(0.0f, 0.0f, 0.0f);
                }

                if (animChannel.scales.empty())
                {
                    animChannel.scales.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.scales.size(); ++v)
                        animChannel.scales[v] = Vector3(1.0f, 1.0f, 1.0f);
                }
            }
            else if (channel->target_path == cgltf_animation_path_type_scale)
            {
                animChannel.scales.resize(dataAccessor->count);
                for (size_t v = 0; v < dataAccessor->count; ++v)
                {
                    float scale[3];
                    cgltf_accessor_read_float(dataAccessor, v, scale, 3);
                    animChannel.scales[v] = Vector3(scale[0], scale[1], scale[2]);
                }

                if (animChannel.translations.empty())
                {
                    animChannel.translations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.translations.size(); ++v)
                        animChannel.translations[v] = Vector3(0.0f, 0.0f, 0.0f);
                }

                if (animChannel.rotations.empty())
                {
                    animChannel.rotations.resize(animChannel.times.size());
                    for (size_t v = 0; v < animChannel.rotations.size(); ++v)
                        animChannel.rotations[v] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                }
            }
            clip->AddChannel(animChannel);
        }
        clip->SetDuration(maxTime);
        cgltf_free(data);
        return clip;
    }

    std::vector<AnimationClip*> LoadAnimationsFromGLTF(std::string_view filePath)
    {
        std::vector<AnimationClip*> clips;
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return clips;
        }

        cgltf_options options = {};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, filePath.data(), &data);

        if (result != cgltf_result_success)
            return clips;

        result = cgltf_load_buffers(&options, data, filePath.data());
        if (result != cgltf_result_success)
        {
            cgltf_free(data);
            return clips;
        }

        std::unordered_map<cgltf_node*, int> nodeToJointMap;
        if (data->skins_count > 0)
        {
            cgltf_skin* skin = &data->skins[0];
            for (size_t i = 0; i < skin->joints_count; ++i)
                nodeToJointMap[skin->joints[i]] = static_cast<int>(i);
        }

        for (size_t i = 0; i < data->animations_count; ++i)
        {
            cgltf_animation* anim = &data->animations[i];
            AnimationClip* clip = new AnimationClip();

            if (anim->name)
                clip->SetName(anim->name);
            else
                clip->SetName("Animation_" + std::to_string(i));
            float maxTime = 0.0f;

            for (size_t j = 0; j < anim->channels_count; ++j)
            {
                cgltf_animation_channel* channel = &anim->channels[j];
                cgltf_animation_sampler* sampler = channel->sampler;
                if (!channel->target_node)
                    continue;

                auto it = nodeToJointMap.find(channel->target_node);
                if (it == nodeToJointMap.end())
                    continue;

                int boneIndex = it->second;
                AnimationChannel animChannel;
                animChannel.targetBoneIndex = boneIndex;
                switch (sampler->interpolation)
                {
                case cgltf_interpolation_type_linear:
                    animChannel.interpolation = AnimationInterpolation::Linear;
                    break;
                case cgltf_interpolation_type_step:
                    animChannel.interpolation = AnimationInterpolation::Step;
                    break;
                case cgltf_interpolation_type_cubic_spline:
                    animChannel.interpolation = AnimationInterpolation::CubicSpline;
                    break;
                default:
                    animChannel.interpolation = AnimationInterpolation::Linear;
                    break;
                }

                cgltf_accessor* timeAccessor = sampler->input;
                animChannel.times.resize(timeAccessor->count);

                for (size_t t = 0; t < timeAccessor->count; ++t)
                {
                    cgltf_accessor_read_float(timeAccessor, t, &animChannel.times[t], 1);
                    if (animChannel.times[t] > maxTime)
                        maxTime = animChannel.times[t];
                }

                cgltf_accessor* dataAccessor = sampler->output;
                if (channel->target_path == cgltf_animation_path_type_translation)
                {
                    animChannel.translations.resize(dataAccessor->count);
                    for (size_t v = 0; v < dataAccessor->count; ++v)
                    {
                        float trans[3];
                        cgltf_accessor_read_float(dataAccessor, v, trans, 3);
                        animChannel.translations[v] = Vector3(trans[0], trans[1], trans[2]);
                    }

                    if (animChannel.rotations.empty())
                    {
                        animChannel.rotations.resize(animChannel.times.size());
                        for (size_t v = 0; v < animChannel.rotations.size(); ++v)
                            animChannel.rotations[v] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                    }

                    if (animChannel.scales.empty())
                    {
                        animChannel.scales.resize(animChannel.times.size());
                        for (size_t v = 0; v < animChannel.scales.size(); ++v)
                            animChannel.scales[v] = Vector3(1.0f, 1.0f, 1.0f);
                    }
                }
                else if (channel->target_path == cgltf_animation_path_type_rotation)
                {
                    animChannel.rotations.resize(dataAccessor->count);
                    for (size_t v = 0; v < dataAccessor->count; ++v)
                    {
                        float rot[4];
                        cgltf_accessor_read_float(dataAccessor, v, rot, 4);
                        animChannel.rotations[v] = Quaternion(rot[0], rot[1], rot[2], rot[3]);
                    }

                    if (animChannel.translations.empty())
                    {
                        animChannel.translations.resize(animChannel.times.size());
                        for (size_t v = 0; v < animChannel.translations.size(); ++v)
                            animChannel.translations[v] = Vector3(0.0f, 0.0f, 0.0f);
                    }

                    if (animChannel.scales.empty())
                    {
                        animChannel.scales.resize(animChannel.times.size());
                        for (size_t v = 0; v < animChannel.scales.size(); ++v)
                            animChannel.scales[v] = Vector3(1.0f, 1.0f, 1.0f);
                    }
                }
                else if (channel->target_path == cgltf_animation_path_type_scale)
                {
                    animChannel.scales.resize(dataAccessor->count);
                    for (size_t v = 0; v < dataAccessor->count; ++v)
                    {
                        float scale[3];
                        cgltf_accessor_read_float(dataAccessor, v, scale, 3);
                        animChannel.scales[v] = Vector3(scale[0], scale[1], scale[2]);
                    }

                    if (animChannel.translations.empty())
                    {
                        animChannel.translations.resize(animChannel.times.size());
                        for (size_t v = 0; v < animChannel.translations.size(); ++v)
                            animChannel.translations[v] = Vector3(0.0f, 0.0f, 0.0f);
                    }

                    if (animChannel.rotations.empty())
                    {
                        animChannel.rotations.resize(animChannel.times.size());
                        for (size_t v = 0; v < animChannel.rotations.size(); ++v)
                            animChannel.rotations[v] = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
                    }
                }
                clip->AddChannel(animChannel);
            }
            clip->SetDuration(maxTime);
            clips.push_back(clip);
        }
        cgltf_free(data);
        return clips;
    }
}