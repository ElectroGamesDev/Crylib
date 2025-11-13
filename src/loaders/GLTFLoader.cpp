#include "loaders/GLTFLoader.h"
#include "Maths.h"
#include "Config.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <stb_image.h>
#include "basis universal/basisu_transcoder.h"
#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>
#include <draco/mesh/mesh.h>
#include <draco/attributes/point_attribute.h>
#include <cgltf.h>

namespace cl
{
    static void NormalizeBoneWeights(float* weights, int count)
    {
        float sum = 0.0f;
        for (int i = 0; i < count; ++i)
            sum += weights[i];

        if (sum > 0.0f)
        {
            float invSum = 1.0f / sum;
            for (int i = 0; i < count; ++i)
                weights[i] *= invSum;
        }
    }

    static void ExtractNodeTransform(cgltf_node* node, Vector3& translation, Quaternion& rotation, Vector3& scale)
    {
        if (node->has_matrix)
        {
            Matrix4 mat;
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                    mat.m[col * 4 + row] = node->matrix[row * 4 + col];

            translation = Vector3(mat.m[12], mat.m[13], mat.m[14]);

            Vector3 scaleX(mat.m[0], mat.m[1], mat.m[2]);
            Vector3 scaleY(mat.m[4], mat.m[5], mat.m[6]);
            Vector3 scaleZ(mat.m[8], mat.m[9], mat.m[10]);
            scale = Vector3(scaleX.Length(), scaleY.Length(), scaleZ.Length());

            Matrix4 rotMat = mat;
            if (scale.x != 0.0f) { rotMat.m[0] /= scale.x; rotMat.m[1] /= scale.x; rotMat.m[2] /= scale.x; }
            if (scale.y != 0.0f) { rotMat.m[4] /= scale.y; rotMat.m[5] /= scale.y; rotMat.m[6] /= scale.y; }
            if (scale.z != 0.0f) { rotMat.m[8] /= scale.z; rotMat.m[9] /= scale.z; rotMat.m[10] /= scale.z; }

            rotation = Quaternion::FromMatrix(rotMat);
        }
        else
        {
            translation = node->has_translation ? Vector3(node->translation[0], node->translation[1], node->translation[2]) : Vector3(0.0f, 0.0f, 0.0f);
            rotation = node->has_rotation ? Quaternion(node->rotation[0], node->rotation[1], node->rotation[2], node->rotation[3]) : Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
            scale = node->has_scale ? Vector3(node->scale[0], node->scale[1], node->scale[2]) : Vector3(1.0f, 1.0f, 1.0f);
        }
    }

#ifdef DRACO_SUPPORTED
    static bool DecompressDraco(cgltf_primitive& primitive, cgltf_data* data,
        std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
    {
        cgltf_draco_mesh_compression* dracoCompression = nullptr;
        //for (size_t i = 0; i < primitive.extensions_count; ++i)
        //{
        //    if (strcmp(primitive.extensions[i].name, "KHR_draco_mesh_compression") == 0)
        //    {
        //        dracoCompression = &primitive.extensions[i].data.draco_mesh_compression;
        //        break;
        //    }
        //}
        if (primitive.has_draco_mesh_compression)
            dracoCompression = &primitive.draco_mesh_compression;
        else
            return false;

        if (!dracoCompression || !dracoCompression->buffer_view)
            return false;

        cgltf_buffer_view* view = dracoCompression->buffer_view;
        const uint8_t* buffer_data = static_cast<const uint8_t*>(view->buffer->data);
        const uint8_t* draco_data = buffer_data + view->offset;
        size_t draco_size = view->size;

        draco::DecoderBuffer buffer;
        buffer.Init(reinterpret_cast<const char*>(draco_data), draco_size);

        draco::Decoder decoder;
        auto statusor = decoder.DecodeMeshFromBuffer(&buffer);
        if (!statusor.ok())
        {
            std::cerr << "[ERROR] Draco decode failed: " << statusor.status().error_msg() << std::endl;
            return false;
        }

        std::unique_ptr<draco::Mesh> mesh = std::move(statusor).value();
        vertices.resize(mesh->num_points());

        for (size_t i = 0; i < dracoCompression->attributes_count; ++i)
        {
            // NEW: Correctly match the Draco attribute to the glTF primitive attribute by type + semantic index
            cgltf_attribute_type draco_type = dracoCompression->attributes[i].type;
            int draco_sem_index = dracoCompression->attributes[i].index;

            int attr_index = -1;
            for (size_t k = 0; k < primitive.attributes_count; ++k)
            {
                if (primitive.attributes[k].type == draco_type &&
                    primitive.attributes[k].index == draco_sem_index)
                {
                    attr_index = static_cast<int>(k);
                    break;
                }
            }
            if (attr_index < 0)
                continue;

            cgltf_attribute* gltf_attr = &primitive.attributes[attr_index];
            int semantic_index = gltf_attr->index;
            int draco_attr_id = static_cast<int>(reinterpret_cast<intptr_t>(dracoCompression->attributes[i].data));

            const draco::PointAttribute* attr = mesh->GetAttributeByUniqueId(draco_attr_id);
            if (!attr)
                continue;

            if (gltf_attr->type == cgltf_attribute_type_position && attr->num_components() == 3)
            {
                for (size_t v = 0; v < vertices.size(); ++v)
                {
                    float pos[3];
                    attr->GetValue(draco::AttributeValueIndex(static_cast<uint32_t>(v)), pos);
                    vertices[v].position = Vector3(pos[0], pos[1], pos[2]);
                }
            }
            else if (gltf_attr->type == cgltf_attribute_type_normal && attr->num_components() == 3)
            {
                for (size_t v = 0; v < vertices.size(); ++v)
                {
                    float norm[3];
                    attr->GetValue(draco::AttributeValueIndex(static_cast<uint32_t>(v)), norm);
                    vertices[v].normal = Vector3(norm[0], norm[1], norm[2]).Normalize();
                }
            }
            else if (gltf_attr->type == cgltf_attribute_type_tangent && attr->num_components() == 4)
            {
                for (size_t v = 0; v < vertices.size(); ++v)
                {
                    float tang[4];
                    attr->GetValue(draco::AttributeValueIndex(static_cast<uint32_t>(v)), tang);
                    vertices[v].tangent = Vector4(tang[0], tang[1], tang[2], tang[3]);
                }
            }
            else if (gltf_attr->type == cgltf_attribute_type_texcoord)
            {
                if (semantic_index == 0 && attr->num_components() == 2)
                {
                    for (size_t v = 0; v < vertices.size(); ++v)
                    {
                        float uv[2];
                        attr->GetValue(draco::AttributeValueIndex(static_cast<uint32_t>(v)), uv);
                        vertices[v].texCoord = Vector2(uv[0], uv[1]);
                    }
                }
                else if (semantic_index == 1 && attr->num_components() == 2)
                {
                    for (size_t v = 0; v < vertices.size(); ++v)
                    {
                        float uv[2];
                        attr->GetValue(draco::AttributeValueIndex(static_cast<uint32_t>(v)), uv);
                        vertices[v].texCoord1 = Vector2(uv[0], uv[1]);
                    }
                }
            }
            else if (gltf_attr->type == cgltf_attribute_type_joints && semantic_index == 0)
            {
                for (size_t v = 0; v < vertices.size(); ++v)
                {
                    uint16_t joints[4];
                    attr->GetValue(draco::AttributeValueIndex(static_cast<uint32_t>(v)), joints);
                    for (int c = 0; c < 4; ++c)
                        vertices[v].boneIndices[c] = static_cast<float>(joints[c]);
                }
            }
            else if (gltf_attr->type == cgltf_attribute_type_weights && semantic_index == 0)
            {
                for (size_t v = 0; v < vertices.size(); ++v)
                {
                    float weights[4];
                    attr->GetValue(draco::AttributeValueIndex(static_cast<uint32_t>(v)), weights);
                    for (int c = 0; c < 4; ++c)
                        vertices[v].boneWeights[c] = weights[c];
                }
            }
        }

        // Extract indices
        indices.resize(mesh->num_faces() * 3);
        for (draco::FaceIndex f(0); f < mesh->num_faces(); ++f)
        {
            const draco::Mesh::Face& face = mesh->face(f);
            indices[f.value() * 3 + 0] = face[0].value();
            indices[f.value() * 3 + 1] = face[1].value();
            indices[f.value() * 3 + 2] = face[2].value();
        }

        return true;
    }
#endif

    Model* LoadGLTF(std::string_view filePath, bool mergeMeshes, int sceneIndex)
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
        {
            std::cerr << "[ERROR] Failed to parse GLTF file: " << filePath << std::endl;
            return nullptr;
        }

        result = cgltf_load_buffers(&options, data, filePath.data());
        if (result != cgltf_result_success)
        {
            std::cerr << "[ERROR] Failed to load GLTF buffers: " << filePath << std::endl;
            cgltf_free(data);
            return nullptr;
        }

        result = cgltf_validate(data);
        if (result != cgltf_result_success)
            std::cerr << "[WARNING] GLTF validation failed for: " << filePath << ". Attempting to load anyway..." << std::endl;

        Model* model = new Model();
        std::unordered_map<cgltf_material*, Material*> materialMap;
        std::unordered_map<cgltf_image*, Texture*> textureCache;
        std::filesystem::path path = filePath;

        int targetScene = sceneIndex;
        if (targetScene < 0 || targetScene >= static_cast<int>(data->scenes_count))
            targetScene = data->scene ? static_cast<int>(data->scene - data->scenes) : 0;

        if (targetScene >= static_cast<int>(data->scenes_count))
        {
            std::cerr << "[ERROR] Invalid scene index: " << targetScene << std::endl;
            cgltf_free(data);
            delete model;
            return nullptr;
        }

        // Load skeleton
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

                bone.name = joint->name ? joint->name : ("Bone_" + std::to_string(i));
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

                Vector3 translation, scale;
                Quaternion rotation;
                ExtractNodeTransform(joint, translation, rotation, scale);

                bone.localTransform = Matrix4::Translate(translation) * Matrix4::FromQuaternion(rotation) * Matrix4::Scale(scale);
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
                        for (int col = 0; col < 4; ++col)
                            invBind.m[col * 4 + row] = mat[row * 4 + col];
                }
            }

            model->SetSkeleton(skeleton);
            model->SetSkinned(true);
        }

        // Texture loading lambda
        auto loadAndSetMap = [&](cgltf_texture* tex, MaterialMapType type, cgltf_material* material) -> void
        {
            if (!tex || !tex->image)
                return;

            auto cached = textureCache.find(tex->image);
            if (cached != textureCache.end())
            {
                Material* mat = materialMap[material];
                if (mat)
                    mat->SetMaterialMap(type, cached->second);

                return;
            }

            cgltf_image* image = tex->image;
            uint8_t* imageData = nullptr;
            size_t size = 0;
            std::string mime = image->mime_type ? image->mime_type : "";
            bool isExternal = false;

            if (image->buffer_view && image->buffer_view->buffer->data)
            {
                imageData = static_cast<uint8_t*>(image->buffer_view->buffer->data) + image->buffer_view->offset;
                size = image->buffer_view->size;
            }
            else if (image->uri)
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
                imageData = new uint8_t[size];
                file.read(reinterpret_cast<char*>(imageData), size);
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
                        std::cerr << "[ERROR] Unknown texture extension: " << ext << std::endl;
                        delete[] imageData;
                        return;
                    }
                }
            }
            else
            {
                std::cerr << "[ERROR] No data or URI for texture" << std::endl;
                return;
            }

            if (mime.empty() && imageData && size >= 8)
            {
                if (imageData[0] == 0x89 && imageData[1] == 'P' && imageData[2] == 'N' && imageData[3] == 'G')
                    mime = "image/png";
                else if (imageData[0] == 0xFF && imageData[1] == 0xD8)
                    mime = "image/jpeg";
                else if (imageData[0] == 0xAB && imageData[1] == 'K' && imageData[2] == 'T' && imageData[3] == 'X')
                    mime = "image/ktx2";
            }

            if (mime.empty())
            {
                std::cerr << "[ERROR] Could not determine texture MIME type" << std::endl;
                if (isExternal)
                    delete[] imageData;

                return;
            }

            Texture* texture = new Texture();
            bool success = false;
            bool isColorTexture = (type == MaterialMapType::Albedo || type == MaterialMapType::Emissive);

            if (mime == "image/png" || mime == "image/jpeg")
            {
                int width, height, channels;
                unsigned char* pixels = stbi_load_from_memory(imageData, static_cast<int>(size), &width, &height, &channels, 4);
                if (pixels)
                {
                    success = texture->LoadFromMemory(pixels, width, height, 4, isColorTexture);
                    stbi_image_free(pixels);
                }
                else
                    std::cerr << "[ERROR] Failed to load texture: " << stbi_failure_reason() << std::endl;
            }
            else if (mime == "image/ktx2")
            {
                basist::ktx2_transcoder transcoder;
                if (transcoder.init(imageData, static_cast<uint32_t>(size)))
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
                                success = texture->LoadFromMemory(dst, width, height, 4, isColorTexture);
                            else
                                std::cerr << "[ERROR] BasisU transcode_image_level failed" << std::endl;

                            delete[] dst;
                        }
                        transcoder.clear();
                    }
                }
            }

            if (isExternal)
                delete[] imageData;

            if (success)
            {
                Material* mat = materialMap[material];
                if (mat)
                    mat->SetMaterialMap(type, texture);

                textureCache[tex->image] = texture;
            }
            else
                delete texture;
        };

        // Primitive processing lambda
        auto processPrimitive = [&](cgltf_mesh& meshData, cgltf_primitive& primitive, const Matrix4& worldTransform, bool hasSkin, cgltf_node* node) -> std::shared_ptr<Mesh>
        {
            if (primitive.type != cgltf_primitive_type_triangles)
            {
                std::cerr << "[WARNING] Skipping non-triangle primitive (mode: " << primitive.type << ")" << std::endl;
                return nullptr;
            }

            auto mesh = std::make_shared<Mesh>();
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;

            mesh->SetSkinned(hasSkin);

            // Check for Draco compression
            bool hasDraco = false;
            for (size_t ext_i = 0; ext_i < primitive.extensions_count; ++ext_i)
            {
                if (strcmp(primitive.extensions[ext_i].name, "KHR_draco_mesh_compression") == 0)
                {
                    hasDraco = true;
                    break;
                }
            }

#ifdef DRACO_SUPPORTED
            if (hasDraco)
            {
                if (!DecompressDraco(primitive, data, vertices, indices))
                {
                    std::cerr << "[ERROR] Failed to decompress Draco primitive" << std::endl;
                    return nullptr;
                }

                // Normalize bone weights
                for (auto& vertex : vertices)
                    NormalizeBoneWeights(vertex.boneWeights, 4);

                // Apply world transform if not skinned
                if (!hasSkin)
                {
                    for (auto& vertex : vertices)
                    {
                        vertex.position = worldTransform.TransformPoint(vertex.position);
                        vertex.normal = worldTransform.TransformDirection(vertex.normal).Normalize();
                        Vector3 tangentDir = worldTransform.TransformDirection(Vector3(vertex.tangent.x, vertex.tangent.y, vertex.tangent.z)).Normalize();
                        vertex.tangent = Vector4(tangentDir.x, tangentDir.y, tangentDir.z, vertex.tangent.w);
                    }
                }
            }
            else
#else
            if (hasDraco)
            {
                std::cerr << "[WARNING] Draco compression detected but not supported. You must recompile Crylib with Draco support. Skipping primitive." << std::endl;
                return nullptr;
            }
            else
#endif
            {
                // Load uncompressed attributes
                for (size_t k = 0; k < primitive.attributes_count; ++k)
                {
                    cgltf_attribute& attribute = primitive.attributes[k];
                    cgltf_accessor* accessor = attribute.data;

                    if (vertices.empty())
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

                            Vector3 tangentVec;
                            if (hasSkin)
                                tangentVec = Vector3(tangent[0], tangent[1], tangent[2]).Normalize();
                            else
                                tangentVec = worldTransform.TransformDirection(Vector3(tangent[0], tangent[1], tangent[2])).Normalize();

                            vertices[v].tangent = Vector4(tangentVec.x, tangentVec.y, tangentVec.z, tangent[3]);
                        }
                    }
                    else if (attribute.type == cgltf_attribute_type_texcoord)
                    {
                        if (attribute.index == 0)
                        {
                            for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                            {
                                float uv[2];
                                cgltf_accessor_read_float(accessor, v, uv, 2);
                                vertices[v].texCoord = Vector2(uv[0], uv[1]);
                            }
                        }
                        else if (attribute.index == 1)
                        {
                            for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                            {
                                float uv[2];
                                cgltf_accessor_read_float(accessor, v, uv, 2);
                                vertices[v].texCoord1 = Vector2(uv[0], uv[1]);
                            }
                        }
                    }
                    else if (attribute.type == cgltf_attribute_type_joints && attribute.index == 0)
                    {
                        for (size_t v = 0; v < accessor->count && v < vertices.size(); ++v)
                        {
                            if (accessor->component_type == cgltf_component_type_r_8u || accessor->component_type == cgltf_component_type_r_16u)
                            {
                                cgltf_size offset = accessor->offset + accessor->stride * v;
                                cgltf_buffer_view* view = accessor->buffer_view;
                                uint8_t* ptr = (uint8_t*)view->buffer->data + view->offset + offset;

                                if (accessor->component_type == cgltf_component_type_r_8u)
                                {
                                    for (int c = 0; c < 4; ++c)
                                        vertices[v].boneIndices[c] = static_cast<float>(ptr[c]);
                                }
                                else if (accessor->component_type == cgltf_component_type_r_16u)
                                {
                                    uint16_t* ptr16 = (uint16_t*)ptr;
                                    for (int c = 0; c < 4; ++c)
                                        vertices[v].boneIndices[c] = static_cast<float>(ptr16[c]);
                                }
                            }
                            else
                                std::cerr << "[ERROR] JOINTS attribute has invalid component type" << std::endl;
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

                // Normalize bone weights
                for (auto& v : vertices)
                    NormalizeBoneWeights(v.boneWeights, 4);

                // Load indices
                if (primitive.indices)
                {
                    cgltf_accessor* accessor = primitive.indices;
                    indices.resize(accessor->count);
                    for (size_t idx = 0; idx < accessor->count; ++idx)
                        indices[idx] = static_cast<uint32_t>(cgltf_accessor_read_index(accessor, idx));
                }
                else
                {
                    indices.resize(vertices.size());
                    for (size_t i = 0; i < vertices.size(); ++i)
                        indices[i] = static_cast<uint32_t>(i);
                }
            }

            // Load morph targets
            std::vector<MorphTarget> morphTargets;
            if (primitive.targets_count > 0)
            {
                morphTargets.resize(primitive.targets_count);

                for (size_t t = 0; t < primitive.targets_count; ++t)
                {
                    cgltf_morph_target& target = primitive.targets[t];
                    MorphTarget& morphTarget = morphTargets[t];

                    morphTarget.positionDeltas.resize(vertices.size(), Vector3(0.0f, 0.0f, 0.0f));
                    morphTarget.normalDeltas.resize(vertices.size(), Vector3(0.0f, 0.0f, 0.0f));
                    morphTarget.tangentDeltas.resize(vertices.size(), Vector3(0.0f, 0.0f, 0.0f));

                    for (size_t a = 0; a < target.attributes_count; ++a)
                    {
                        cgltf_attribute& attr = target.attributes[a];
                        cgltf_accessor* accessor = attr.data;

                        if (attr.type == cgltf_attribute_type_position)
                        {
                            for (size_t v = 0; v < accessor->count && v < morphTarget.positionDeltas.size(); ++v)
                            {
                                float delta[3];
                                cgltf_accessor_read_float(accessor, v, delta, 3);
                                morphTarget.positionDeltas[v] = Vector3(delta[0], delta[1], delta[2]);
                            }
                        }
                        else if (attr.type == cgltf_attribute_type_normal)
                        {
                            for (size_t v = 0; v < accessor->count && v < morphTarget.normalDeltas.size(); ++v)
                            {
                                float delta[3];
                                cgltf_accessor_read_float(accessor, v, delta, 3);
                                morphTarget.normalDeltas[v] = Vector3(delta[0], delta[1], delta[2]);
                            }
                        }
                        else if (attr.type == cgltf_attribute_type_tangent)
                        {
                            for (size_t v = 0; v < accessor->count && v < morphTarget.tangentDeltas.size(); ++v)
                            {
                                float delta[3];
                                cgltf_accessor_read_float(accessor, v, delta, 3);
                                morphTarget.tangentDeltas[v] = Vector3(delta[0], delta[1], delta[2]);
                            }
                        }
                    }

                    // Set name if available
                    if (meshData.target_names && t < meshData.target_names_count && meshData.target_names[t])
                        morphTarget.name = meshData.target_names[t];
                    else
                        morphTarget.name = "Target_" + std::to_string(t);
                }

                mesh->SetMorphTargets(morphTargets);

                // Set initial morph weights from node
                if (node && node->weights_count > 0)
                {
                    std::vector<float> weights(node->weights_count);
                    for (size_t w = 0; w < node->weights_count; ++w)
                        weights[w] = node->weights[w];

                    mesh->SetMorphWeights(weights);
                }
                else if (primitive.targets_count > 0)
                {
                    std::vector<float> weights(primitive.targets_count, 0.0f);
                    mesh->SetMorphWeights(weights);
                }
            }

            // Load or create material
            Material* material = new Material();
            material->SetShader(s_defaultShader);

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

                    materialMap[primitive.material] = material;

                    loadAndSetMap(pbr.base_color_texture.texture, MaterialMapType::Albedo, primitive.material);
                    loadAndSetMap(pbr.metallic_roughness_texture.texture, MaterialMapType::MetallicRoughness, primitive.material);
                    loadAndSetMap(primitive.material->normal_texture.texture, MaterialMapType::Normal, primitive.material);
                    loadAndSetMap(primitive.material->occlusion_texture.texture, MaterialMapType::AO, primitive.material);
                    loadAndSetMap(primitive.material->emissive_texture.texture, MaterialMapType::Emissive, primitive.material);

                    // Check which texcoord set is used for AO and Emissive
                    bool aoUsesTexCoord1 = false;
                    bool emissiveUsesTexCoord1 = false;

                    if (primitive.material->occlusion_texture.texture && primitive.material->occlusion_texture.texcoord == 1)
                        aoUsesTexCoord1 = true;

                    if (primitive.material->emissive_texture.texture && primitive.material->emissive_texture.texcoord == 1)
                        emissiveUsesTexCoord1 = true;

                    // Set shader parameters for UV selection if needed
                    if (aoUsesTexCoord1 || emissiveUsesTexCoord1)
                    {
                        material->SetShaderParam("u_MaterialFlags2",
                            { aoUsesTexCoord1 ? 1.0f : 0.0f,
                            emissiveUsesTexCoord1 ? 1.0f : 0.0f,
                            0.0f, 0.0f });
                    }

                }
                else
                {
                    delete material;
                    material = it->second;
                }
            }
            else
                materialMap[nullptr] = material;

            mesh->SetMaterial(material);
            mesh->SetVertices(vertices);
            mesh->SetIndices(indices);

            if (!mergeMeshes)
                mesh->Upload();

            return mesh;
        };

        // Process scene nodes
        std::function<void(cgltf_node*, const Matrix4&)> ProcessNode;
        ProcessNode = [&](cgltf_node* node, const Matrix4& parentTransform)
        {
            Vector3 translation, scale;
            Quaternion rotation;
            ExtractNodeTransform(node, translation, rotation, scale);

            Matrix4 t = Matrix4::Translate(translation);
            Matrix4 r = Matrix4::FromQuaternion(rotation);
            Matrix4 s = Matrix4::Scale(scale);
            Matrix4 local = t * r * s;
            Matrix4 worldTransform = parentTransform * local;

            if (node->mesh)
            {
                bool hasSkin = (node->skin != nullptr);

                for (size_t j = 0; j < node->mesh->primitives_count; ++j)
                {
                    cgltf_primitive& primitive = node->mesh->primitives[j];
                    auto mesh = processPrimitive(*node->mesh, primitive, worldTransform, hasSkin, node);

                    if (mesh)
                        model->AddMesh(mesh);
                }
            }

            for (size_t i = 0; i < node->children_count; ++i)
                ProcessNode(node->children[i], worldTransform);
        };

        // Process the selected scene
        cgltf_scene* scene = &data->scenes[targetScene];
        for (size_t i = 0; i < scene->nodes_count; ++i)
            ProcessNode(scene->nodes[i], Matrix4::Identity());

        // Build node-to-index map for animations
        std::unordered_map<cgltf_node*, int> nodeToIndexMap;
        for (size_t i = 0; i < data->nodes_count; ++i)
            nodeToIndexMap[&data->nodes[i]] = static_cast<int>(i);

        // Load animations
        for (size_t i = 0; i < data->animations_count; ++i)
        {
            cgltf_animation* anim = &data->animations[i];
            AnimationClip* clip = new AnimationClip();

            clip->SetName(anim->name ? anim->name : ("Animation_" + std::to_string(i)));

            float maxTime = 0.0f;
            bool hasSkeletalChannels = false;
            bool hasNodeChannels = false;
            bool hasWeightChannels = false;

            // Determine animation type
            for (size_t j = 0; j < anim->channels_count; ++j)
            {
                cgltf_animation_channel* channel = &anim->channels[j];
                if (!channel->target_node)
                    continue;

                if (channel->target_path == cgltf_animation_path_type_weights)
                    hasWeightChannels = true;
                else if (nodeToJointMap.find(channel->target_node) != nodeToJointMap.end())
                    hasSkeletalChannels = true;
                else
                    hasNodeChannels = true;
            }

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
                clip->SetAnimationType(AnimationType::NodeBased);

            // Load animation channels
            for (size_t j = 0; j < anim->channels_count; ++j)
            {
                cgltf_animation_channel* channel = &anim->channels[j];
                cgltf_animation_sampler* sampler = channel->sampler;

                if (!channel->target_node || !sampler)
                    continue;

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

                cgltf_accessor* timeAccessor = sampler->input;
                std::vector<float> times(timeAccessor->count);
                for (size_t t = 0; t < timeAccessor->count; ++t)
                {
                    cgltf_accessor_read_float(timeAccessor, t, &times[t], 1);
                    if (times[t] > maxTime)
                        maxTime = times[t];
                }

                cgltf_accessor* dataAccessor = sampler->output;
                auto jointIt = nodeToJointMap.find(channel->target_node);
                bool isBoneChannel = (jointIt != nodeToJointMap.end());

                // Handle morph target weight animation
                if (channel->target_path == cgltf_animation_path_type_weights)
                {
                    MorphWeightChannel weightChannel;

                    auto nodeIt = nodeToIndexMap.find(channel->target_node);
                    if (nodeIt != nodeToIndexMap.end())
                        weightChannel.targetNodeIndex = nodeIt->second;
                    else
                    {
                        std::cout << "[WARNING] Node not found for morph weight animation\n";
                        continue;
                    }

                    weightChannel.interpolation = interpType;
                    weightChannel.times = times;

                    // Calculate number of morph targets from the node
                    size_t weightsPerFrame = 0;
                    if (channel->target_node->mesh && channel->target_node->mesh->primitives_count > 0)
                        weightsPerFrame = channel->target_node->mesh->primitives[0].targets_count;

                    if (weightsPerFrame == 0)
                    {
                        std::cout << "[WARNING] Morph weight animation found but no morph targets on node\n";
                        continue;
                    }

                    size_t keyCount = times.size();

                    if (interpType == AnimationInterpolation::CubicSpline)
                    {
                        // For cubic spline, data has 3x values: inTangent, value, outTangent
                        if (dataAccessor->count != keyCount * 3 * weightsPerFrame)
                        {
                            std::cerr << "[ERROR] Cubic spline morph weight data count mismatch\n";
                            continue;
                        }

                        weightChannel.weights.resize(keyCount);
                        for (size_t k = 0; k < keyCount; ++k)
                        {
                            weightChannel.weights[k].resize(weightsPerFrame);

                            // Skip in-tangent, read value, skip out-tangent
                            size_t baseIdx = k * 3 * weightsPerFrame + weightsPerFrame; // +weightsPerFrame to skip in-tangent

                            for (size_t w = 0; w < weightsPerFrame; ++w)
                                cgltf_accessor_read_float(dataAccessor, baseIdx + w, &weightChannel.weights[k][w], 1);
                        }
                    }
                    else
                    {
                        // Linear or Step interpolation
                        if (dataAccessor->count != keyCount * weightsPerFrame)
                        {
                            std::cerr << "[ERROR] Morph weight data count mismatch\n";
                            continue;
                        }

                        weightChannel.weights.resize(keyCount);
                        for (size_t k = 0; k < keyCount; ++k)
                        {
                            weightChannel.weights[k].resize(weightsPerFrame);
                            for (size_t w = 0; w < weightsPerFrame; ++w)
                            {
                                cgltf_accessor_read_float(dataAccessor, k * weightsPerFrame + w,
                                    &weightChannel.weights[k][w], 1);
                            }
                        }
                    }

                    clip->AddMorphWeightChannel(weightChannel);
                    continue;
                }

                // Handle skeletal animation channels
                if (clip->GetAnimationType() == AnimationType::Skeletal && isBoneChannel)
                {
                    AnimationChannel animChannel;
                    animChannel.targetBoneIndex = jointIt->second;
                    animChannel.interpolation = interpType;
                    animChannel.times = times;

                    size_t keyCount = times.size();

                    if (interpType == AnimationInterpolation::CubicSpline)
                    {
                        if (dataAccessor->count != keyCount * 3)
                        {
                            std::cerr << "[ERROR] Cubic spline data count mismatch for bone " << animChannel.targetBoneIndex << std::endl;
                            continue;
                        }

                        if (channel->target_path == cgltf_animation_path_type_translation)
                        {
                            animChannel.translations.resize(keyCount);
                            animChannel.inTangents.resize(keyCount);
                            animChannel.outTangents.resize(keyCount);

                            for (size_t v = 0; v < keyCount; ++v)
                            {
                                float inTan[3], val[3], outTan[3];
                                cgltf_accessor_read_float(dataAccessor, v * 3, inTan, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 1, val, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 2, outTan, 3);

                                animChannel.inTangents[v] = Vector3(inTan[0], inTan[1], inTan[2]);
                                animChannel.translations[v] = Vector3(val[0], val[1], val[2]);
                                animChannel.outTangents[v] = Vector3(outTan[0], outTan[1], outTan[2]);
                            }
                        }
                        else if (channel->target_path == cgltf_animation_path_type_rotation)
                        {
                            animChannel.rotations.resize(keyCount);
                            animChannel.inTangentsQuat.resize(keyCount);
                            animChannel.outTangentsQuat.resize(keyCount);

                            for (size_t v = 0; v < keyCount; ++v)
                            {
                                float inTan[4], val[4], outTan[4];
                                cgltf_accessor_read_float(dataAccessor, v * 3, inTan, 4);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 1, val, 4);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 2, outTan, 4);

                                animChannel.inTangentsQuat[v] = Quaternion(inTan[0], inTan[1], inTan[2], inTan[3]).Normalize();
                                animChannel.rotations[v] = Quaternion(val[0], val[1], val[2], val[3]).Normalize();
                                animChannel.outTangentsQuat[v] = Quaternion(outTan[0], outTan[1], outTan[2], outTan[3]).Normalize();
                            }
                        }
                        else if (channel->target_path == cgltf_animation_path_type_scale)
                        {
                            animChannel.scales.resize(keyCount);
                            animChannel.inTangentsScale.resize(keyCount);
                            animChannel.outTangentsScale.resize(keyCount);

                            for (size_t v = 0; v < keyCount; ++v)
                            {
                                float inTan[3], val[3], outTan[3];
                                cgltf_accessor_read_float(dataAccessor, v * 3, inTan, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 1, val, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 2, outTan, 3);

                                animChannel.inTangentsScale[v] = Vector3(inTan[0], inTan[1], inTan[2]);
                                animChannel.scales[v] = Vector3(val[0], val[1], val[2]);
                                animChannel.outTangentsScale[v] = Vector3(outTan[0], outTan[1], outTan[2]);
                            }
                        }
                    }
                    else
                    {
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
                                animChannel.rotations[v] = Quaternion(rot[0], rot[1], rot[2], rot[3]).Normalize();
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
                    }

                    if (animChannel.translations.empty())
                        animChannel.translations.resize(keyCount, Vector3(0.0f, 0.0f, 0.0f));
                    if (animChannel.rotations.empty())
                        animChannel.rotations.resize(keyCount, Quaternion(0.0f, 0.0f, 0.0f, 1.0f));
                    if (animChannel.scales.empty())
                        animChannel.scales.resize(keyCount, Vector3(1.0f, 1.0f, 1.0f));

                    clip->AddChannel(animChannel);
                }
                // Handle node-based animation channels
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

                    size_t keyCount = times.size();

                    if (interpType == AnimationInterpolation::CubicSpline)
                    {
                        if (dataAccessor->count != keyCount * 3)
                        {
                            std::cerr << "[ERROR] Cubic spline data count mismatch for node " << nodeChannel.targetNodeIndex << std::endl;
                            continue;
                        }

                        if (channel->target_path == cgltf_animation_path_type_translation)
                        {
                            nodeChannel.translations.resize(keyCount);
                            nodeChannel.inTangents.resize(keyCount);
                            nodeChannel.outTangents.resize(keyCount);

                            for (size_t v = 0; v < keyCount; ++v)
                            {
                                float inTan[3], val[3], outTan[3];
                                cgltf_accessor_read_float(dataAccessor, v * 3, inTan, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 1, val, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 2, outTan, 3);

                                nodeChannel.inTangents[v] = Vector3(inTan[0], inTan[1], inTan[2]);
                                nodeChannel.translations[v] = Vector3(val[0], val[1], val[2]);
                                nodeChannel.outTangents[v] = Vector3(outTan[0], outTan[1], outTan[2]);
                            }
                        }
                        else if (channel->target_path == cgltf_animation_path_type_rotation)
                        {
                            nodeChannel.rotations.resize(keyCount);
                            nodeChannel.inTangentsQuat.resize(keyCount);
                            nodeChannel.outTangentsQuat.resize(keyCount);

                            for (size_t v = 0; v < keyCount; ++v)
                            {
                                float inTan[4], val[4], outTan[4];
                                cgltf_accessor_read_float(dataAccessor, v * 3, inTan, 4);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 1, val, 4);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 2, outTan, 4);

                                nodeChannel.inTangentsQuat[v] = Quaternion(inTan[0], inTan[1], inTan[2], inTan[3]).Normalize();
                                nodeChannel.rotations[v] = Quaternion(val[0], val[1], val[2], val[3]).Normalize();
                                nodeChannel.outTangentsQuat[v] = Quaternion(outTan[0], outTan[1], outTan[2], outTan[3]).Normalize();
                            }
                        }
                        else if (channel->target_path == cgltf_animation_path_type_scale)
                        {
                            nodeChannel.scales.resize(keyCount);
                            nodeChannel.inTangentsScale.resize(keyCount);
                            nodeChannel.outTangentsScale.resize(keyCount);

                            for (size_t v = 0; v < keyCount; ++v)
                            {
                                float inTan[3], val[3], outTan[3];
                                cgltf_accessor_read_float(dataAccessor, v * 3, inTan, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 1, val, 3);
                                cgltf_accessor_read_float(dataAccessor, v * 3 + 2, outTan, 3);

                                nodeChannel.inTangentsScale[v] = Vector3(inTan[0], inTan[1], inTan[2]);
                                nodeChannel.scales[v] = Vector3(val[0], val[1], val[2]);
                                nodeChannel.outTangentsScale[v] = Vector3(outTan[0], outTan[1], outTan[2]);
                            }
                        }
                    }
                    else
                    {
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
                                nodeChannel.rotations[v] = Quaternion(rot[0], rot[1], rot[2], rot[3]).Normalize();
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
                    }

                    if (nodeChannel.translations.empty())
                        nodeChannel.translations.resize(keyCount, Vector3(0.0f, 0.0f, 0.0f));
                    if (nodeChannel.rotations.empty())
                        nodeChannel.rotations.resize(keyCount, Quaternion(0.0f, 0.0f, 0.0f, 1.0f));
                    if (nodeChannel.scales.empty())
                        nodeChannel.scales.resize(keyCount, Vector3(1.0f, 1.0f, 1.0f));

                    clip->AddNodeChannel(nodeChannel);
                }
            }

            clip->SetDuration(maxTime);
            model->AddAnimation(clip);
        }

        model->SetNodeCount(static_cast<int>(data->nodes_count));

        // Merge meshes if requested
        if (mergeMeshes)
        {
            if (!model->MergeMeshes())
            {
                for (auto& mesh : model->GetMeshes())
                    mesh->Upload();
            }
        }

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