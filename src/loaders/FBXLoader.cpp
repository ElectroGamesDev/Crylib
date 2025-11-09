#include "loaders/FBXLoader.h"
#include "Maths.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <stb_image.h>
#include "basis universal/basisu_transcoder.h"
#include "ufbx/ufbx.h"
#include <set>

namespace cl
{
    ufbx_anim_prop* FindPropInAnim(ufbx_anim* anim, const ufbx_element* element, const char* prop)
    {
        if (!anim)
            return nullptr;

        for (size_t li = 0; li < anim->layers.count; ++li)
        {
            ufbx_anim_layer* layer = anim->layers.data[li];
            if (!layer)
                continue;

            ufbx_anim_prop* p = ufbx_find_anim_prop(layer, element, prop);
            if (p)
                return p;
        }

        return nullptr;
    }

    void ProcessSkeletalAnimationChannels(ufbx_anim* uanim, const std::unordered_map<ufbx_node*, int>& nodeToJointMap, AnimationClip* clip, float& maxTime)
    {
        for (auto& pair : nodeToJointMap)
        {
            ufbx_node* joint = pair.first;
            int boneIndex = pair.second;

            // Handle translation, rotation, or scale
            auto process_prop = [&](const char* propName,
                auto evalCurveFunc,
                auto defaultValueFunc,
                auto applyValueFunc)
                {
                    ufbx_anim_prop* prop = FindPropInAnim(uanim, &joint->element, propName);
                    if (!prop || !prop->anim_value)
                        return;

                    AnimationChannel channel;
                    channel.targetBoneIndex = boneIndex;
                    channel.interpolation = AnimationInterpolation::Linear;

                    // Collect all keyframe times
                    std::set<float> time_set;
                    for (int c = 0; c < 3; ++c)
                    {
                        ufbx_anim_curve* curve = prop->anim_value->curves[c];
                        if (!curve)
                            continue;

                        for (size_t k = 0; k < curve->keyframes.count; ++k)
                            time_set.insert(static_cast<float>(curve->keyframes.data[k].time));
                    }

                    channel.times.assign(time_set.begin(), time_set.end());
                    applyValueFunc(channel, prop, joint);

                    clip->AddChannel(channel);
                    if (!channel.times.empty())
                        maxTime = std::max(maxTime, channel.times.back());
                };

            // Translation
            process_prop("Lcl Translation",
                [](ufbx_anim_curve* c, float t, float v) { return (float)ufbx_evaluate_curve(c, t, v); },
                [](ufbx_anim_prop* p) { return p->anim_value->default_value; },
                [&](AnimationChannel& channel, ufbx_anim_prop* prop, ufbx_node* joint)
                {
                    channel.translations.resize(channel.times.size());
                    channel.rotations.resize(channel.times.size(), Quaternion(0, 0, 0, 1));
                    channel.scales.resize(channel.times.size(), Vector3(1, 1, 1));

                    for (size_t t = 0; t < channel.times.size(); ++t)
                    {
                        float time = channel.times[t];
                        ufbx_vec3 val = prop->anim_value->default_value;

                        for (int c = 0; c < 3; ++c)
                        {
                            if (prop->anim_value->curves[c])
                                ((float*)&val)[c] = (float)ufbx_evaluate_curve(prop->anim_value->curves[c], time, ((float*)&val)[c]);
                        }

                        channel.translations[t] = Vector3(val.x, val.y, val.z);
                    }
                });

            // Rotation
            process_prop("Lcl Rotation",
                [](ufbx_anim_curve* c, float t, float v) { return (float)ufbx_evaluate_curve(c, t, v); },
                [](ufbx_anim_prop* p) { return p->anim_value->default_value; },
                [&](AnimationChannel& channel, ufbx_anim_prop* prop, ufbx_node* joint)
                {
                    channel.rotations.resize(channel.times.size());
                    channel.translations.resize(channel.times.size(), Vector3(0, 0, 0));
                    channel.scales.resize(channel.times.size(), Vector3(1, 1, 1));

                    for (size_t t = 0; t < channel.times.size(); ++t)
                    {
                        float time = channel.times[t];
                        ufbx_vec3 euler = prop->anim_value->default_value;

                        for (int c = 0; c < 3; ++c)
                        {
                            if (prop->anim_value->curves[c])
                                ((float*)&euler)[c] = (float)ufbx_evaluate_curve(prop->anim_value->curves[c], time, ((float*)&prop->anim_value->default_value)[c]);
                        }

                        Quaternion q;
                        switch (joint->rotation_order)
                        {
                        case UFBX_ROTATION_ORDER_XYZ:
                            q = Quaternion::FromEuler(euler.x, euler.y, euler.z);
                            break;
                        case UFBX_ROTATION_ORDER_XZY:
                            q = Quaternion::FromEuler(euler.x, euler.z, euler.y);
                            break;
                        case UFBX_ROTATION_ORDER_YXZ:
                            q = Quaternion::FromEuler(euler.y, euler.x, euler.z); break;
                        case UFBX_ROTATION_ORDER_YZX: q = Quaternion::FromEuler(euler.y, euler.z, euler.x);
                            break;
                        case UFBX_ROTATION_ORDER_ZXY:
                            q = Quaternion::FromEuler(euler.z, euler.x, euler.y);
                            break;
                        case UFBX_ROTATION_ORDER_ZYX:
                            q = Quaternion::FromEuler(euler.z, euler.y, euler.x);
                            break;
                        default:
                            q = Quaternion::FromEuler(euler.x, euler.y, euler.z);
                            break;
                        }

                        channel.rotations[t] = q;
                    }
                });

            // Scale
            process_prop("Lcl Scaling",
                [](ufbx_anim_curve* c, float t, float v) { return (float)ufbx_evaluate_curve(c, t, v); },
                [](ufbx_anim_prop* p) { return p->anim_value->default_value; },
                [&](AnimationChannel& channel, ufbx_anim_prop* prop, ufbx_node* joint)
                {
                    channel.scales.resize(channel.times.size());
                    channel.translations.resize(channel.times.size(), Vector3(0, 0, 0));
                    channel.rotations.resize(channel.times.size(), Quaternion(0, 0, 0, 1));

                    for (size_t t = 0; t < channel.times.size(); ++t)
                    {
                        float time = channel.times[t];
                        ufbx_vec3 val = prop->anim_value->default_value;

                        for (int c = 0; c < 3; ++c)
                        {
                            if (prop->anim_value->curves[c])
                                ((float*)&val)[c] = (float)ufbx_evaluate_curve(prop->anim_value->curves[c], time, ((float*)&val)[c]);
                        }

                        channel.scales[t] = Vector3(val.x, val.y, val.z);
                    }
                });
        }
    }

    void ProcessNodeAnimationChannels(ufbx_anim* uanim, const std::unordered_map<ufbx_node*, int>& nodeToIndexMap, AnimationClip* clip, float& maxTime)
    {
        for (auto& pair : nodeToIndexMap)
        {
            ufbx_node* node = pair.first;
            int nodeIndex = pair.second;

            // Handle translation, rotation, or scale
            auto process_prop = [&](const char* propName,
                auto evalCurveFunc,
                auto defaultValueFunc,
                auto applyValueFunc)
                {
                    ufbx_anim_prop* prop = FindPropInAnim(uanim, &node->element, propName);
                    if (!prop || !prop->anim_value)
                        return;

                    NodeAnimationChannel channel;
                    channel.targetNodeIndex = nodeIndex;
                    channel.interpolation = AnimationInterpolation::Linear;

                    // Collect all keyframe times
                    std::set<float> time_set;
                    for (int c = 0; c < 3; ++c)
                    {
                        ufbx_anim_curve* curve = prop->anim_value->curves[c];
                        if (!curve)
                            continue;

                        for (size_t k = 0; k < curve->keyframes.count; ++k)
                            time_set.insert(static_cast<float>(curve->keyframes.data[k].time));
                    }

                    channel.times.assign(time_set.begin(), time_set.end());
                    applyValueFunc(channel, prop, node);

                    clip->AddNodeChannel(channel);
                    if (!channel.times.empty())
                        maxTime = std::max(maxTime, channel.times.back());
                };

            // Translation
            process_prop("Lcl Translation",
                [](ufbx_anim_curve* c, float t, float v) { return (float)ufbx_evaluate_curve(c, t, v); },
                [](ufbx_anim_prop* p) { return p->anim_value->default_value; },
                [&](NodeAnimationChannel& channel, ufbx_anim_prop* prop, ufbx_node* node)
                {
                    channel.translations.resize(channel.times.size());
                    channel.rotations.resize(channel.times.size(), Quaternion(0, 0, 0, 1));
                    channel.scales.resize(channel.times.size(), Vector3(1, 1, 1));

                    for (size_t t = 0; t < channel.times.size(); ++t)
                    {
                        float time = channel.times[t];
                        ufbx_vec3 val = prop->anim_value->default_value;

                        for (int c = 0; c < 3; ++c)
                        {
                            if (prop->anim_value->curves[c])
                                ((float*)&val)[c] = (float)ufbx_evaluate_curve(prop->anim_value->curves[c], time, ((float*)&val)[c]);
                        }

                        channel.translations[t] = Vector3(val.x, val.y, val.z);
                    }
                });

            // Rotation
            process_prop("Lcl Rotation",
                [](ufbx_anim_curve* c, float t, float v) { return (float)ufbx_evaluate_curve(c, t, v); },
                [](ufbx_anim_prop* p) { return p->anim_value->default_value; },
                [&](NodeAnimationChannel& channel, ufbx_anim_prop* prop, ufbx_node* node)
                {
                    channel.rotations.resize(channel.times.size());
                    channel.translations.resize(channel.times.size(), Vector3(0, 0, 0));
                    channel.scales.resize(channel.times.size(), Vector3(1, 1, 1));

                    for (size_t t = 0; t < channel.times.size(); ++t)
                    {
                        float time = channel.times[t];
                        ufbx_vec3 euler = prop->anim_value->default_value;

                        for (int c = 0; c < 3; ++c)
                        {
                            if (prop->anim_value->curves[c])
                                ((float*)&euler)[c] = (float)ufbx_evaluate_curve(prop->anim_value->curves[c], time, ((float*)&prop->anim_value->default_value)[c]);
                        }

                        Quaternion q;
                        switch (node->rotation_order)
                        {
                        case UFBX_ROTATION_ORDER_XYZ:
                            q = Quaternion::FromEuler(euler.x, euler.y, euler.z);
                            break;
                        case UFBX_ROTATION_ORDER_XZY:
                            q = Quaternion::FromEuler(euler.x, euler.z, euler.y);
                            break;
                        case UFBX_ROTATION_ORDER_YXZ:
                            q = Quaternion::FromEuler(euler.y, euler.x, euler.z); break;
                        case UFBX_ROTATION_ORDER_YZX: q = Quaternion::FromEuler(euler.y, euler.z, euler.x);
                            break;
                        case UFBX_ROTATION_ORDER_ZXY:
                            q = Quaternion::FromEuler(euler.z, euler.x, euler.y);
                            break;
                        case UFBX_ROTATION_ORDER_ZYX:
                            q = Quaternion::FromEuler(euler.z, euler.y, euler.x);
                            break;
                        default:
                            q = Quaternion::FromEuler(euler.x, euler.y, euler.z);
                            break;
                        }

                        channel.rotations[t] = q;
                    }
                });

            // Scale
            process_prop("Lcl Scaling",
                [](ufbx_anim_curve* c, float t, float v) { return (float)ufbx_evaluate_curve(c, t, v); },
                [](ufbx_anim_prop* p) { return p->anim_value->default_value; },
                [&](NodeAnimationChannel& channel, ufbx_anim_prop* prop, ufbx_node* node)
                {
                    channel.scales.resize(channel.times.size());
                    channel.translations.resize(channel.times.size(), Vector3(0, 0, 0));
                    channel.rotations.resize(channel.times.size(), Quaternion(0, 0, 0, 1));

                    for (size_t t = 0; t < channel.times.size(); ++t)
                    {
                        float time = channel.times[t];
                        ufbx_vec3 val = prop->anim_value->default_value;

                        for (int c = 0; c < 3; ++c)
                        {
                            if (prop->anim_value->curves[c])
                                ((float*)&val)[c] = (float)ufbx_evaluate_curve(prop->anim_value->curves[c], time, ((float*)&val)[c]);
                        }

                        channel.scales[t] = Vector3(val.x, val.y, val.z);
                    }
                });
        }
    }

    Model* LoadFBX(std::string_view filePath)
    {
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return nullptr;
        }

        ufbx_load_opts options = {};
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0f;
        options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
        options.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_MODIFY_GEOMETRY;
        options.inherit_mode_handling = UFBX_INHERIT_MODE_HANDLING_COMPENSATE;
        options.target_camera_axes = ufbx_axes_right_handed_y_up;
        options.target_light_axes = ufbx_axes_right_handed_y_up;
        options.generate_missing_normals = true;

        ufbx_error error;
        ufbx_scene* data = ufbx_load_file(filePath.data(), &options, &error);
        if (!data)
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\": " << error.description.data << std::endl;
            return nullptr;
        }

        Model* model = new Model();
        std::unordered_map<ufbx_material*, Material*> materialMap;
        std::unordered_map<ufbx_texture*, Texture*> textureCache;
        std::filesystem::path path = filePath;
        Skeleton* skeleton = nullptr;
        std::unordered_map<ufbx_node*, int> nodeToJointMap;
        ufbx_skin_deformer* skin = nullptr;

        for (size_t i = 0; i < data->meshes.count; ++i)
        {
            if (data->meshes[i]->skin_deformers.count > 0)
            {
                skin = data->meshes[i]->skin_deformers.data[0];
                break;
            }
        }
        if (skin)
        {
            skeleton = new Skeleton();
            skeleton->bones.resize(skin->clusters.count);

            for (size_t i = 0; i < skin->clusters.count; ++i)
            {
                ufbx_skin_cluster* cluster = skin->clusters.data[i];
                ufbx_node* joint = cluster->bone_node;

                if (!joint)
                    continue;

                Bone& bone = skeleton->bones[i];
                if (joint->name.data)
                    bone.name = joint->name.data;
                else
                    bone.name = "Bone_" + std::to_string(i);

                nodeToJointMap[joint] = static_cast<int>(i);
                skeleton->boneMap[bone.name] = static_cast<int>(i);
            }

            for (size_t i = 0; i < skin->clusters.count; ++i)
            {
                ufbx_skin_cluster* cluster = skin->clusters.data[i];
                ufbx_node* joint = cluster->bone_node;

                if (!joint)
                    continue;

                Bone& bone = skeleton->bones[i];
                bone.parentIndex = -1;

                if (joint->parent)
                {
                    auto it = nodeToJointMap.find(joint->parent);
                    if (it != nodeToJointMap.end())
                        bone.parentIndex = it->second;
                }

                Matrix4 translation = Matrix4::Translate(Vector3(joint->local_transform.translation.x, joint->local_transform.translation.y, joint->local_transform.translation.z));
                Matrix4 rotation = Matrix4::FromQuaternion(Quaternion(joint->local_transform.rotation.x, joint->local_transform.rotation.y, joint->local_transform.rotation.z, joint->local_transform.rotation.w));
                Matrix4 scale = Matrix4::Scale(Vector3(joint->local_transform.scale.x, joint->local_transform.scale.y, joint->local_transform.scale.z));
                
                bone.localTransform = translation * rotation * scale;
                Matrix4& invBind = skeleton->bones[i].inverseBindMatrix;
                ufbx_matrix mat = cluster->geometry_to_bone;

                // ufbx_matrix is column-major 3x4 (affine, no last row)
                for (int col = 0; col < 4; ++col)
                {
                    for (int row = 0; row < 3; ++row)
                        invBind.m[col * 4 + row] = mat.cols[col].v[row];

                    invBind.m[col * 4 + 3] = (col == 3 ? 1.0f : 0.0f);
                }
            }

            model->SetSkeleton(skeleton);
            model->SetSkinned(true);
        }
        std::function<void(ufbx_node*, const Matrix4&)> ProcessNode;
        ProcessNode = [&](ufbx_node* node, const Matrix4& parentTransform)
            {
                Matrix4 local = Matrix4::Identity();
                Matrix4 translation = Matrix4::Translate(Vector3(node->local_transform.translation.x, node->local_transform.translation.y, node->local_transform.translation.z));
                Matrix4 rotation = Matrix4::FromQuaternion(Quaternion(node->local_transform.rotation.x, node->local_transform.rotation.y, node->local_transform.rotation.z, node->local_transform.rotation.w));
                Matrix4 scale = Matrix4::Scale(Vector3(node->local_transform.scale.x, node->local_transform.scale.y, node->local_transform.scale.z));

                local = translation * rotation * scale;
                Matrix4 worldTransform = parentTransform * local;

                if (node->mesh)
                {
                    ufbx_mesh* primitive = node->mesh;
                    bool hasSkin = (primitive->skin_deformers.count > 0);

                    // Process each material part as a separate sub-mesh to match GLTF primitives
                    for (size_t j = 0; j < primitive->material_parts.count; ++j)
                    {
                        ufbx_mesh_part& part = primitive->material_parts.data[j];
                        auto mesh = std::make_shared<Mesh>();
                        mesh->SetSkinned(hasSkin);

                        std::vector<Vertex> vertices;
                        std::vector<uint32_t> indices;
                        std::vector<uint32_t> tri_buf(primitive->max_face_triangles * 3);

                        uint32_t ix = 0;
                        for (size_t fi = 0; fi < part.face_indices.count; ++fi)
                        {
                            ufbx_face face = primitive->faces.data[part.face_indices.data[fi]];
                            size_t num_tris = ufbx_triangulate_face(tri_buf.data(), tri_buf.size(), primitive, face);

                            for (size_t t = 0; t < num_tris; ++t)
                            {
                                // Flip triangle winding order (CW to CCW)
                                std::swap(tri_buf[t * 3 + 0], tri_buf[t * 3 + 2]);

                                for (size_t vtx = 0; vtx < 3; ++vtx)
                                {
                                    uint32_t index = tri_buf[t * 3 + vtx];
                                    Vertex v;

                                    // Position
                                    ufbx_vec3 p = ufbx_get_vertex_vec3(&primitive->vertex_position, index);
                                    if (hasSkin)
                                        v.position = Vector3(p.x, p.y, p.z);
                                    else
                                        v.position = worldTransform.TransformPoint(Vector3(p.x, p.y, p.z));

                                    // Normal
                                    ufbx_vec3 n = ufbx_get_vertex_vec3(&primitive->vertex_normal, index);
                                    if (hasSkin)
                                        v.normal = Vector3(n.x, n.y, n.z).Normalize();
                                    else
                                        v.normal = worldTransform.TransformDirection(Vector3(n.x, n.y, n.z)).Normalize();

                                    // Tangent / Bitangent
                                    if (primitive->vertex_tangent.exists)
                                    {
                                        ufbx_vec3 tan = ufbx_get_vertex_vec3(&primitive->vertex_tangent, index);
                                        ufbx_vec3 bit = ufbx_get_vertex_vec3(&primitive->vertex_bitangent, index);
                                        if (hasSkin)
                                        {
                                            v.tangent = Vector3(tan.x, tan.y, tan.z).Normalize();
                                            v.bitangent = Vector3(bit.x, bit.y, bit.z).Normalize();
                                        }
                                        else
                                        {
                                            v.tangent = worldTransform.TransformDirection(Vector3(tan.x, tan.y, tan.z)).Normalize();
                                            v.bitangent = worldTransform.TransformDirection(Vector3(bit.x, bit.y, bit.z)).Normalize();
                                        }
                                    }

                                    // UV
                                    if (primitive->vertex_uv.exists)
                                    {
                                        ufbx_vec2 uv = ufbx_get_vertex_vec2(&primitive->vertex_uv, index);
                                        v.texCoord = Vector2(uv.x, uv.y);
                                    }

                                    // Skinning
                                    if (hasSkin && skin)
                                    {
                                        // Determine the vertex index into skin->vertices array
                                        uint32_t vertexIndex = 0;
                                        if (primitive->vertex_indices.data)
                                        {
                                            // Ensure index is within vertex_indices.count
                                            if (index >= primitive->vertex_indices.count)
                                                vertexIndex = UINT32_MAX;
                                            else
                                                vertexIndex = primitive->vertex_indices.data[index];
                                        }
                                        else
                                        {
                                            vertexIndex = index;
                                        }

                                        // Validate vertexIndex and existence in skin->vertices
                                        if (vertexIndex != UINT32_MAX && vertexIndex < skin->vertices.count)
                                        {
                                            const ufbx_skin_vertex& sv = skin->vertices.data[vertexIndex];

                                            // Clamp number of weights
                                            int numWeights = static_cast<int>(sv.num_weights);
                                            if (numWeights > 4)
                                                numWeights = 4;

                                            if (numWeights < 0)
                                                numWeights = 0;

                                            float totalWeight = 0.0f;

                                            for (int c = 0; c < 4; ++c)
                                            {
                                                v.boneIndices[c] = 0.0f;
                                                v.boneWeights[c] = 0.0f;
                                            }

                                            for (int c = 0; c < numWeights; ++c)
                                            {
                                                uint32_t weightIndex = static_cast<uint32_t>(sv.weight_begin) + static_cast<uint32_t>(c);

                                                // Bounds-check the weightIndex
                                                if (weightIndex >= skin->weights.count)
                                                    break;  // Malformed data. Skip remaining weights

                                                const ufbx_skin_weight& sw = skin->weights.data[weightIndex];

                                                // cluster_index should refer to a cluster/bone index
                                                // Map FBX cluster indicies to skeleton bone indicies
                                                int clusterIndex = static_cast<int>(sw.cluster_index);
                                                if (clusterIndex < 0)
                                                    continue;

                                                v.boneIndices[c] = static_cast<float>(clusterIndex);
                                                v.boneWeights[c] = static_cast<float>(sw.weight);
                                                totalWeight += v.boneWeights[c];
                                            }

                                            // Normalize weights to sum to 1.0 if total > 0
                                            if (totalWeight > 0.0f)
                                            {
                                                for (int c = 0; c < 4; ++c)
                                                    v.boneWeights[c] /= totalWeight;
                                            }
                                        }
                                        else
                                        {
                                            // If there's no valid skin vertex entry then leave boneIndices/weights at 0
                                            for (int c = 0; c < 4; ++c)
                                            {
                                                v.boneIndices[c] = 0.0f;
                                                v.boneWeights[c] = 0.0f;
                                            }
                                        }
                                    }

                                    vertices.push_back(v);
                                    indices.push_back(static_cast<uint32_t>(ix++));
                                }
                            }
                        }

                        Material* material = new Material();
                        material->SetShader(s_defaultShader);
                        ufbx_material* pmat = (part.index != UFBX_NO_INDEX) ? primitive->materials.data[part.index] : nullptr;

                        if (pmat)
                        {
                            auto it = materialMap.find(pmat);
                            if (it == materialMap.end())
                            {
                                ufbx_vec4 base = pmat->pbr.base_color.value_vec4;
                                material->SetAlbedo(Color(base.x * 255, base.y * 255, base.z * 255, base.w * 255));
                                material->SetMetallic(pmat->pbr.metalness.value_real);
                                material->SetRoughness(pmat->pbr.roughness.value_real);
                                ufbx_vec4  emiss = pmat->pbr.emission_color.value_vec4;
                                material->SetEmissive(Color(emiss.x * 255, emiss.y * 255, emiss.z * 255, emiss.w * 255));
                                auto loadAndSetMap = [&](ufbx_texture* tex, MaterialMapType type)
                                    {
                                        if (!tex)
                                            return;

                                        auto cached = textureCache.find(tex);
                                        if (cached != textureCache.end())
                                        {
                                            material->SetMaterialMap(type, cached->second);
                                            return;
                                        }

                                        uint8_t* data = nullptr;
                                        size_t size = 0;
                                        std::string mime = "";
                                        bool isExternal = false;

                                        if (tex->content.size > 0)
                                        {
                                            data = reinterpret_cast<uint8_t*>(const_cast<void*>(tex->content.data));
                                            size = tex->content.size;
                                        }
                                        else if (tex->relative_filename.data)
                                        {
                                            std::string texPath = path.parent_path().string() + "/" + tex->relative_filename.data;

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
                                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
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
                                            textureCache[tex] = texture;
                                        }
                                        else
                                            delete texture;
                                    };
                                loadAndSetMap(pmat->pbr.base_color.texture, MaterialMapType::Albedo);
                                loadAndSetMap(pmat->pbr.metalness.texture, MaterialMapType::MetallicRoughness);
                                loadAndSetMap(pmat->pbr.roughness.texture, MaterialMapType::MetallicRoughness); // Todo: Need to add individual map types for roughness and metallic, then support them in the shader
                                loadAndSetMap(pmat->pbr.normal_map.texture, MaterialMapType::Normal);
                                loadAndSetMap(pmat->pbr.ambient_occlusion.texture, MaterialMapType::AO);
                                loadAndSetMap(pmat->pbr.emission_color.texture, MaterialMapType::Emissive);
                                materialMap[pmat] = material;
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
                for (size_t i = 0; i < node->children.count; ++i)
                    ProcessNode(node->children.data[i], worldTransform);
        };

        ProcessNode(data->root_node, Matrix4::Identity());

        // Build node-to-index map for all nodes
        std::unordered_map<ufbx_node*, int> nodeToIndexMap;
        for (size_t i = 0; i < data->nodes.count; ++i)
            nodeToIndexMap[data->nodes.data[i]] = static_cast<int>(i);

        for (size_t i = 0; i < data->anim_stacks.count; ++i)
        {
            ufbx_anim_stack* anim = data->anim_stacks.data[i];
            AnimationClip* clip = new AnimationClip();

            if (anim->name.data)
                clip->SetName(anim->name.data);
            else
                clip->SetName("Animation_" + std::to_string(i));

            ufbx_anim* uanim = anim->anim;
            float maxTime = 0.0f;

            // Determine animation type by checking which nodes are animated
            bool hasSkeletalChannels = false;
            bool hasNodeChannels = false;

            if (uanim)
            {
                for (size_t li = 0; li < uanim->layers.count; ++li)
                {
                    ufbx_anim_layer* layer = uanim->layers.data[li];
                    if (!layer)
                        continue;

                    for (size_t pi = 0; pi < layer->anim_props.count; ++pi)
                    {
                        ufbx_anim_prop* prop = &layer->anim_props.data[pi];
                        if (!prop || !prop->element)
                            continue;

                        // Check if this is a node element
                        if (prop->element->type == UFBX_ELEMENT_NODE)
                        {
                            ufbx_node* node = (ufbx_node*)prop->element;

                            // Check if it's a joint/bone
                            if (nodeToJointMap.find(node) != nodeToJointMap.end())
                            {
                                hasSkeletalChannels = true;
                            }
                            else
                            {
                                hasNodeChannels = true;
                            }
                        }
                    }
                }
            }

            // Set animation type
            if (hasSkeletalChannels && !hasNodeChannels)
                clip->SetAnimationType(AnimationType::Skeletal);
            else if (!hasSkeletalChannels && hasNodeChannels)
                clip->SetAnimationType(AnimationType::NodeBased);
            else if (hasSkeletalChannels && hasNodeChannels)
            {
                // Mixed animation. Priortize skeleton
                clip->SetAnimationType(AnimationType::Skeletal);
                std::cout << "[WARNING] Animation '" << clip->GetName() << "' has both skeletal and node channels. Using skeletal mode.\n";
            }
            else // No valid channels, default to skeletal
                clip->SetAnimationType(AnimationType::Skeletal);

            // Process channels based on type
            if (clip->GetAnimationType() == AnimationType::Skeletal)
            {
                ProcessSkeletalAnimationChannels(uanim, nodeToJointMap, clip, maxTime);
            }
            else // NodeBased
                ProcessNodeAnimationChannels(uanim, nodeToIndexMap, clip, maxTime);

            clip->SetDuration(maxTime);
            model->AddAnimation(clip);
        }

        model->SetNodeCount(static_cast<int>(data->nodes.count));

        ufbx_free_scene(data);
        return model;
    }

    AnimationClip* LoadAnimationFromFBX(std::string_view filePath, size_t animationIndex)
    {
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return nullptr;
        }

        ufbx_load_opts options{};
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0f;
        options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(filePath.data(), &options, &error);
        if (!scene) return nullptr;

        if (animationIndex >= scene->anim_stacks.count)
        {
            ufbx_free_scene(scene);
            return nullptr;
        }

        // Map joints to bone indices
        std::unordered_map<ufbx_node*, int> nodeToJointMap;
        ufbx_skin_deformer* skin = nullptr;
        for (size_t i = 0; i < scene->meshes.count; ++i)
        {
            if (scene->meshes[i]->skin_deformers.count > 0)
            {
                skin = scene->meshes[i]->skin_deformers.data[0];
                break;
            }
        }

        if (skin)
        {
            for (size_t i = 0; i < skin->clusters.count; ++i)
            {
                ufbx_skin_cluster* cluster = skin->clusters.data[i];
                ufbx_node* joint = cluster->bone_node;
                if (joint) nodeToJointMap[joint] = static_cast<int>(i);
            }
        }

        // Build node-to-index map for all nodes
        std::unordered_map<ufbx_node*, int> nodeToIndexMap;
        for (size_t i = 0; i < scene->nodes.count; ++i)
            nodeToIndexMap[scene->nodes.data[i]] = static_cast<int>(i);

        ufbx_anim_stack* animStack = scene->anim_stacks.data[animationIndex];
        AnimationClip* clip = new AnimationClip();
        clip->SetName(animStack->name.data ? animStack->name.data : "Animation_" + std::to_string(animationIndex));
        ufbx_anim* uanim = animStack->anim;

        float maxTime = 0.0f;

        // Determine animation type
        bool hasSkeletalChannels = false;
        bool hasNodeChannels = false;

        if (uanim)
        {
            for (size_t li = 0; li < uanim->layers.count; ++li)
            {
                ufbx_anim_layer* layer = uanim->layers.data[li];
                if (!layer)
                    continue;

                for (size_t pi = 0; pi < layer->anim_props.count; ++pi)
                {
                    ufbx_anim_prop* prop = &layer->anim_props.data[pi];
                    if (!prop || !prop->element)
                        continue;

                    if (prop->element->type == UFBX_ELEMENT_NODE)
                    {
                        ufbx_node* node = (ufbx_node*)prop->element;

                        if (nodeToJointMap.find(node) != nodeToJointMap.end())
                            hasSkeletalChannels = true;
                        else
                            hasNodeChannels = true;
                    }
                }
            }
        }

        // Set animation type
        if (hasSkeletalChannels && !hasNodeChannels)
            clip->SetAnimationType(AnimationType::Skeletal);
        else if (!hasSkeletalChannels && hasNodeChannels)
            clip->SetAnimationType(AnimationType::NodeBased);
        else if (hasSkeletalChannels && hasNodeChannels)
        {
            clip->SetAnimationType(AnimationType::Skeletal);
            std::cout << "[WARNING] Animation '" << clip->GetName() << "' has both skeletal and node channels. Using skeletal mode.\n";
        }
        else
            clip->SetAnimationType(AnimationType::Skeletal);

        // Process channels based on type
        if (clip->GetAnimationType() == AnimationType::Skeletal)
            ProcessSkeletalAnimationChannels(uanim, nodeToJointMap, clip, maxTime);
        else
            ProcessNodeAnimationChannels(uanim, nodeToIndexMap, clip, maxTime);

        clip->SetDuration(maxTime);
        ufbx_free_scene(scene);
        return clip;
    }

    AnimationClip* LoadAnimationFromFBX(std::string_view filePath, std::string_view animationName)
    {
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return nullptr;
        }

        ufbx_load_opts options{};
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0f;
        options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(filePath.data(), &options, &error);

        if (!scene)
            return nullptr;

        // Map joints to bone indices
        std::unordered_map<ufbx_node*, int> nodeToJointMap;
        ufbx_skin_deformer* skin = nullptr;
        for (size_t i = 0; i < scene->meshes.count; ++i)
        {
            if (scene->meshes[i]->skin_deformers.count > 0)
            {
                skin = scene->meshes[i]->skin_deformers.data[0];
                break;
            }
        }

        if (skin)
        {
            for (size_t i = 0; i < skin->clusters.count; ++i)
            {
                ufbx_skin_cluster* cluster = skin->clusters.data[i];
                ufbx_node* joint = cluster->bone_node;
                if (joint) nodeToJointMap[joint] = static_cast<int>(i);
            }
        }

        // Build node-to-index map for all nodes
        std::unordered_map<ufbx_node*, int> nodeToIndexMap;
        for (size_t i = 0; i < scene->nodes.count; ++i)
            nodeToIndexMap[scene->nodes.data[i]] = static_cast<int>(i);

        // Find the animation by name
        ufbx_anim_stack* animStack = nullptr;
        for (size_t i = 0; i < scene->anim_stacks.count; ++i)
        {
            if (scene->anim_stacks.data[i]->name.data && animationName == scene->anim_stacks.data[i]->name.data)
            {
                animStack = scene->anim_stacks.data[i];
                break;
            }
        }

        if (!animStack)
        {
            ufbx_free_scene(scene);
            return nullptr;
        }

        AnimationClip* clip = new AnimationClip();
        clip->SetName(animationName.data());

        ufbx_anim* uanim = animStack->anim;
        float maxTime = 0.0f;

        // Determine animation type
        bool hasSkeletalChannels = false;
        bool hasNodeChannels = false;

        if (uanim)
        {
            for (size_t li = 0; li < uanim->layers.count; ++li)
            {
                ufbx_anim_layer* layer = uanim->layers.data[li];
                if (!layer)
                    continue;

                for (size_t pi = 0; pi < layer->anim_props.count; ++pi)
                {
                    ufbx_anim_prop* prop = &layer->anim_props.data[pi];
                    if (!prop || !prop->element)
                        continue;

                    if (prop->element->type == UFBX_ELEMENT_NODE)
                    {
                        ufbx_node* node = (ufbx_node*)prop->element;

                        if (nodeToJointMap.find(node) != nodeToJointMap.end())
                            hasSkeletalChannels = true;
                        else
                            hasNodeChannels = true;
                    }
                }
            }
        }

        // Set animation type
        if (hasSkeletalChannels && !hasNodeChannels)
            clip->SetAnimationType(AnimationType::Skeletal);
        else if (!hasSkeletalChannels && hasNodeChannels)
            clip->SetAnimationType(AnimationType::NodeBased);
        else if (hasSkeletalChannels && hasNodeChannels)
        {
            clip->SetAnimationType(AnimationType::Skeletal);
            std::cout << "[WARNING] Animation '" << clip->GetName() << "' has both skeletal and node channels. Using skeletal mode.\n";
        }
        else
            clip->SetAnimationType(AnimationType::Skeletal);

        // Process channels based on type
        if (clip->GetAnimationType() == AnimationType::Skeletal)
            ProcessSkeletalAnimationChannels(uanim, nodeToJointMap, clip, maxTime);
        else
            ProcessNodeAnimationChannels(uanim, nodeToIndexMap, clip, maxTime);

        clip->SetDuration(maxTime);
        ufbx_free_scene(scene);
        return clip;
    }

    std::vector<AnimationClip*> LoadAnimationsFromFBX(std::string_view filePath)
    {
        std::vector<AnimationClip*> clips;
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return clips;
        }

        ufbx_load_opts options{};
        options.target_axes = ufbx_axes_right_handed_y_up;
        options.target_unit_meters = 1.0f;
        options.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;

        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(filePath.data(), &options, &error);
        if (!scene) return clips;

        // Map joints to bone indices
        std::unordered_map<ufbx_node*, int> nodeToJointMap;
        ufbx_skin_deformer* skin = nullptr;
        for (size_t i = 0; i < scene->meshes.count; ++i)
        {
            if (scene->meshes[i]->skin_deformers.count > 0)
            {
                skin = scene->meshes[i]->skin_deformers.data[0];
                break;
            }
        }

        if (skin)
        {
            for (size_t i = 0; i < skin->clusters.count; ++i)
            {
                ufbx_skin_cluster* cluster = skin->clusters.data[i];
                ufbx_node* joint = cluster->bone_node;

                if (joint)
                    nodeToJointMap[joint] = static_cast<int>(i);
            }
        }

        // Build node-to-index map for all nodes
        std::unordered_map<ufbx_node*, int> nodeToIndexMap;
        for (size_t i = 0; i < scene->nodes.count; ++i)
            nodeToIndexMap[scene->nodes.data[i]] = static_cast<int>(i);

        // Load each anim stack
        for (size_t i = 0; i < scene->anim_stacks.count; ++i)
        {
            ufbx_anim_stack* animStack = scene->anim_stacks.data[i];
            AnimationClip* clip = new AnimationClip();
            clip->SetName(animStack->name.data ? animStack->name.data : "Animation_" + std::to_string(i));

            ufbx_anim* uanim = animStack->anim;
            float maxTime = 0.0f;

            // Determine animation type
            bool hasSkeletalChannels = false;
            bool hasNodeChannels = false;

            if (uanim)
            {
                for (size_t li = 0; li < uanim->layers.count; ++li)
                {
                    ufbx_anim_layer* layer = uanim->layers.data[li];
                    if (!layer)
                        continue;

                    for (size_t pi = 0; pi < layer->anim_props.count; ++pi)
                    {
                        ufbx_anim_prop* prop = &layer->anim_props.data[pi];
                        if (!prop || !prop->element)
                            continue;

                        if (prop->element->type == UFBX_ELEMENT_NODE)
                        {
                            ufbx_node* node = (ufbx_node*)prop->element;

                            if (nodeToJointMap.find(node) != nodeToJointMap.end())
                                hasSkeletalChannels = true;
                            else
                                hasNodeChannels = true;
                        }
                    }
                }
            }

            // Set animation type
            if (hasSkeletalChannels && !hasNodeChannels)
                clip->SetAnimationType(AnimationType::Skeletal);
            else if (!hasSkeletalChannels && hasNodeChannels)
                clip->SetAnimationType(AnimationType::NodeBased);
            else if (hasSkeletalChannels && hasNodeChannels)
            {
                clip->SetAnimationType(AnimationType::Skeletal);
                std::cout << "[WARNING] Animation '" << clip->GetName() << "' has both skeletal and node channels. Using skeletal mode.\n";
            }
            else
                clip->SetAnimationType(AnimationType::Skeletal);

            // Process channels based on type
            if (clip->GetAnimationType() == AnimationType::Skeletal)
                ProcessSkeletalAnimationChannels(uanim, nodeToJointMap, clip, maxTime);
            else
                ProcessNodeAnimationChannels(uanim, nodeToIndexMap, clip, maxTime);

            clip->SetDuration(maxTime);
            clips.push_back(clip);
        }

        ufbx_free_scene(scene);
        return clips;
    }
}