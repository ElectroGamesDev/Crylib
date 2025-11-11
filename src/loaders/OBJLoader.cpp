#include "loaders/OBJLoader.h"
#include "Maths.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <stb_image.h>
#include <rapidobj/rapidobj.hpp>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <future>
#include <optional>
#include <algorithm>
#include <memory>
#include <limits>
#include <execution>

namespace cl
{
    struct VertexKey
    {
        int pos;
        int tex;
        int norm;

        bool operator==(VertexKey const& o) const noexcept
        {
            return pos == o.pos && tex == o.tex && norm == o.norm;
        }
    };

    struct VertexKeyHash
    {
        size_t operator()(VertexKey const& k) const noexcept
        {
            size_t h1 = std::hash<int>()(k.pos);
            size_t h2 = std::hash<int>()(k.tex);
            size_t h3 = std::hash<int>()(k.norm);
            size_t seed = h1;

            seed ^= h2 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            seed ^= h3 + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);

            return seed;
        }
    };

    struct TextureCache
    {
        TextureCache() = default;

        Texture* GetIfExists(std::string_view path)
        {
            std::shared_lock lock(mutex_);
            auto it = cache_.find(path.data());
            return (it != cache_.end()) ? it->second : nullptr;
        }

        void Insert(std::string_view path, Texture* tex)
        {
            std::unique_lock lock(mutex_);
            cache_[path.data()] = tex;
        }

        std::unordered_map<std::string, Texture*> cache_;
        mutable std::shared_mutex mutex_;
    };

    static size_t GetThreadCount()
    {
        unsigned int hwThreads = std::thread::hardware_concurrency();

        // Fallback if unknown
        if (hwThreads == 0)
            hwThreads = 4;

        return static_cast<size_t>(hwThreads >= 2 ? hwThreads - 1 : 1);
    }

    Model* LoadOBJ(std::string_view filePath, bool mergeMeshes)
    {
        if (!std::filesystem::exists(filePath))
        {
            std::cerr << "[ERROR] Failed to load \"" << filePath << "\". File does not exist." << std::endl;
            return nullptr;
        }

        // Parse OBJ
        rapidobj::Result result = rapidobj::ParseFile(filePath, rapidobj::MaterialLibrary::Default(rapidobj::Load::Optional));
        if (result.error)
        {
            std::cerr << "[ERROR] Failed to parse OBJ file: " << result.error.code.message() << std::endl;
            return nullptr;
        }

        // Triangulate
        if (!rapidobj::Triangulate(result))
        {
            std::cerr << "[ERROR] Failed to triangulate OBJ mesh: " << result.error.code.message() << std::endl;
            return nullptr;
        }

        Model* model = new Model();

        //Prepare materials asynchronously (but lightweight)
        const std::filesystem::path objPath = filePath;
        const std::filesystem::path objDir = objPath.parent_path();
        TextureCache textureCache;

        // Helper to synchronously load an image from disk
        auto loadTextureFromFile = [&](const std::filesystem::path& fullPath, bool isColorTexture) -> std::optional<Texture*>
            {
                if (!std::filesystem::exists(fullPath))
                    return std::nullopt;

                int width = 0, height = 0, channels = 0;
                // prefer 4 channels for color textures
                int desired = isColorTexture ? 4 : 0;
                unsigned char* pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, desired);
                if (!pixels)
                {
                    // second attempt to force RGBA if we got 3 and need 4 on some platforms
                    if (desired == 0 && channels == 3)
                    {
                        pixels = stbi_load(fullPath.string().c_str(), &width, &height, &channels, 4);
                        channels = 4;
                    }
                    if (!pixels)
                    {
                        std::cerr << "[WARNING] Failed to load texture: " << fullPath << " - " << stbi_failure_reason() << std::endl;
                        return std::nullopt;
                    }
                }

                // Create Texture
                Texture* tex = new Texture();
                bool ok = tex->LoadFromMemory(pixels, width, height, (desired == 0 ? channels : desired), isColorTexture);

                stbi_image_free(pixels);

                if (!ok)
                {
                    delete tex;
                    return std::nullopt;
                }

                return tex;
            };

        auto loadTextureWithCache = [&](std::string_view texPath, bool isColorTexture) -> Texture*
            {
                if (texPath.empty())
                    return nullptr;

                // Fast read path
                if (Texture* cached = textureCache.GetIfExists(texPath))
                    return cached;

                // Build full path
                std::filesystem::path fullPath = objDir / texPath;

                // Load image then insert into cache.
                auto loaded = loadTextureFromFile(fullPath, isColorTexture);

                if (!loaded.has_value())
                    return nullptr;

                Texture* tex = loaded.value();
                textureCache.Insert(texPath, tex);

                return tex;
            };

        // Pre-create Material objects in a vector
        std::vector<Material*> materials;
        materials.resize(result.materials.size(), nullptr);

        // Creating materials with threading
        const size_t materialCount = result.materials.size();
        size_t threadCount = std::min(materialCount == 0 ? size_t(1) : materialCount, GetThreadCount());
        std::atomic<size_t> materialIndex(0);
        std::vector<std::thread> materialWorkers;
        materialWorkers.reserve(threadCount);

        for (size_t t = 0; t < threadCount; ++t)
        {
            materialWorkers.emplace_back([&]() {
                size_t i;

                while ((i = materialIndex.fetch_add(1)) < materialCount)
                {
                    const rapidobj::Material& objMat = result.materials[i];
                    Material* material = new Material();
                    material->SetShader(s_defaultShader);

                    // Set basic PBR properties
                    Color albedoColor(
                        static_cast<unsigned char>(std::clamp(objMat.diffuse[0] * 255.0f, 0.0f, 255.0f)),
                        static_cast<unsigned char>(std::clamp(objMat.diffuse[1] * 255.0f, 0.0f, 255.0f)),
                        static_cast<unsigned char>(std::clamp(objMat.diffuse[2] * 255.0f, 0.0f, 255.0f)),
                        static_cast<unsigned char>(std::clamp((1.0f - objMat.dissolve) * 255.0f, 0.0f, 255.0f))
                    );
                    material->SetAlbedo(albedoColor);

                    // Roughness/metallic
                    float roughness = (objMat.roughness >= 0.0f) ? objMat.roughness : (1.0f - (objMat.shininess / 1000.0f));
                    float metallic = (objMat.metallic >= 0.0f) ? objMat.metallic : 0.0f;
                    material->SetRoughness(roughness);
                    material->SetMetallic(metallic);

                    Color emissiveColor(
                        static_cast<unsigned char>(std::clamp(objMat.emission[0] * 255.0f, 0.0f, 255.0f)),
                        static_cast<unsigned char>(std::clamp(objMat.emission[1] * 255.0f, 0.0f, 255.0f)),
                        static_cast<unsigned char>(std::clamp(objMat.emission[2] * 255.0f, 0.0f, 255.0f)),
                        255
                    );
                    material->SetEmissive(emissiveColor);

                    // Texture loading
                    if (!objMat.diffuse_texname.empty())
                    {
                        if (Texture* t = loadTextureWithCache(objMat.diffuse_texname, true))
                            material->SetMaterialMap(MaterialMapType::Albedo, t);
                    }

                    // Metallic-roughness // Todo: Need to separate these 
                    if (!objMat.roughness_texname.empty())
                    {
                        if (Texture* t = loadTextureWithCache(objMat.roughness_texname, false))
                            material->SetMaterialMap(MaterialMapType::MetallicRoughness, t);
                    }

                    if (!objMat.specular_texname.empty())
                    {
                        if (Texture* t = loadTextureWithCache(objMat.specular_texname, false))
                            material->SetMaterialMap(MaterialMapType::MetallicRoughness, t);
                    }

                    // Normal map
                    if (!objMat.normal_texname.empty())
                    {
                        if (Texture* t = loadTextureWithCache(objMat.normal_texname, false))
                            material->SetMaterialMap(MaterialMapType::Normal, t);
                    }
                    else if (!objMat.bump_texname.empty())
                    {
                        if (Texture* t = loadTextureWithCache(objMat.bump_texname, false))
                            material->SetMaterialMap(MaterialMapType::Normal, t);
                    }

                    if (!objMat.ambient_texname.empty())
                    {
                        if (Texture* t = loadTextureWithCache(objMat.ambient_texname, false))
                            material->SetMaterialMap(MaterialMapType::AO, t);
                    }

                    if (!objMat.emissive_texname.empty())
                    {
                        if (Texture* t = loadTextureWithCache(objMat.emissive_texname, true))
                            material->SetMaterialMap(MaterialMapType::Emissive, t);
                    }

                    materials[i] = material;
                }
                });
        }

        for (auto& w : materialWorkers)
            w.join();

        // Create default material
        Material* defaultMaterial = new Material();
        defaultMaterial->SetShader(s_defaultShader);
        defaultMaterial->SetAlbedo(Color(200, 200, 200, 255));
        defaultMaterial->SetRoughness(0.5f);
        defaultMaterial->SetMetallic(0.0f);

        size_t totalShapes = result.shapes.size();
        std::vector<std::shared_ptr<Mesh>> allMeshes;
        allMeshes.reserve(totalShapes * 2);
        std::mutex allMeshesMutex;

        size_t shapeThreadCount = std::min<size_t>(totalShapes == 0 ? 1 : totalShapes, GetThreadCount());
        std::atomic<size_t> shapeIndex(0);
        std::vector<std::thread> shapeWorkers;
        shapeWorkers.reserve(shapeThreadCount);

        const auto& positions = result.attributes.positions;
        const auto& normals = result.attributes.normals;
        const auto& texcoords = result.attributes.texcoords;
        const auto& colors = result.attributes.colors;

        for (size_t t = 0; t < shapeThreadCount; ++t)
        {
            shapeWorkers.emplace_back([&, t]() {
                size_t s;
                while ((s = shapeIndex.fetch_add(1)) < result.shapes.size())
                {
                    const auto& shape = result.shapes[s];
                    const auto& mesh = shape.mesh;

                    if (mesh.num_face_vertices.empty())
                        continue;

                    bool hasNormals = !normals.empty();

                    std::vector<Vector3> smoothNormals;
                    if (!hasNormals)
                    {
                        size_t posCount = positions.size() / 3;
                        smoothNormals.assign(posCount, { 0,0,0 });
                        std::vector<int> normalCounts(posCount, 0);

                        size_t idxOffset = 0;
                        for (size_t f = 0; f < mesh.num_face_vertices.size(); ++f)
                        {
                            int numVerts = mesh.num_face_vertices[f];
                            uint32_t smoothGroup = mesh.smoothing_group_ids.empty() ? 1u : mesh.smoothing_group_ids[f];

                            int i0 = mesh.indices[idxOffset].position_index;
                            int i1 = mesh.indices[idxOffset + 1].position_index;
                            int i2 = mesh.indices[idxOffset + 2].position_index;

                            Vector3 v0{ positions[i0 * 3 + 0], positions[i0 * 3 + 1], positions[i0 * 3 + 2] };
                            Vector3 v1{ positions[i1 * 3 + 0], positions[i1 * 3 + 1], positions[i1 * 3 + 2] };
                            Vector3 v2{ positions[i2 * 3 + 0], positions[i2 * 3 + 1], positions[i2 * 3 + 2] };

                            Vector3 faceNormal = Vector3::Cross(v1 - v0, v2 - v0).Normalize();

                            for (int vi = 0; vi < numVerts; ++vi)
                            {
                                int posIdx = mesh.indices[idxOffset + vi].position_index;
                                if (smoothGroup != 0)
                                {
                                    smoothNormals[posIdx] = smoothNormals[posIdx] + faceNormal;
                                    normalCounts[posIdx]++;
                                }
                                else
                                {
                                    smoothNormals[posIdx] = faceNormal;
                                    normalCounts[posIdx] = 1;
                                }
                            }
                            idxOffset += numVerts;
                        }

                        // Normalize if needed. Use parallel only for big arrays
                        static const int NormalizeThreshold = [] {
                            const int threads = static_cast<int>(GetThreadCount());
                            const int baseThreshold = 50000;
                            const int minThreshold = (threads > 8) ? 10000 : 20000;
                            return std::max(minThreshold, baseThreshold / std::max(1, threads));
                        }();

                        if (smoothNormals.size() > NormalizeThreshold)
                        {
                            std::for_each(std::execution::par, smoothNormals.begin(), smoothNormals.end(), [&](Vector3& n) {
                                size_t i = &n - smoothNormals.data();
                                if (normalCounts[i] > 1)
                                    n = n / static_cast<float>(normalCounts[i]);
                                if (normalCounts[i] > 0)
                                    n = n.Normalize();
                                });
                        }
                        else
                        {
                            for (size_t i = 0; i < smoothNormals.size(); ++i)
                            {
                                if (normalCounts[i] > 1)
                                    smoothNormals[i] = smoothNormals[i] / static_cast<float>(normalCounts[i]);

                                if (normalCounts[i] > 0)
                                    smoothNormals[i] = smoothNormals[i].Normalize();
                            }
                        }
                    }

                    // Group faces by material id for batching
                    std::unordered_map<int, std::vector<size_t>> facesByMaterial;
                    facesByMaterial.reserve(mesh.material_ids.size() ? mesh.material_ids.size() : 4);
                    for (size_t fi = 0; fi < mesh.num_face_vertices.size(); ++fi)
                    {
                        int matId = mesh.material_ids.empty() ? -1 : mesh.material_ids[fi];
                        facesByMaterial[matId].push_back(fi);
                    }

                    // For each material group produce a mesh
                    for (auto const& pair : facesByMaterial)
                    {
                        int matId = pair.first;
                        const std::vector<size_t>& faces = pair.second;

                        std::vector<char> faceMask(mesh.num_face_vertices.size(), 0);
                        for (size_t f : faces)
                            faceMask[f] = 1;

                        // Reserve approximate sizes
                        size_t estimatedVerts = faces.size() * 3;
                        std::vector<Vertex> vertices;
                        std::vector<uint32_t> indices;
                        vertices.reserve(estimatedVerts);
                        indices.reserve(estimatedVerts);

                        // Use unordered_map with VertexKey struct for deduplication
                        std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;
                        vertexMap.reserve(estimatedVerts * 2);

                        // Iterate over faces once, adding those marked in faceMask
                        size_t idxOffset = 0;
                        for (size_t f = 0; f < mesh.num_face_vertices.size(); ++f)
                        {
                            int numVerts = mesh.num_face_vertices[f];
                            if (!faceMask[f])
                            {
                                idxOffset += numVerts;
                                continue;
                            }

                            // Each face is triangle (triangulated). Create vertices
                            for (int vi = 0; vi < numVerts; ++vi)
                            {
                                const rapidobj::Index& idx = mesh.indices[idxOffset + vi];
                                VertexKey key{
                                    idx.position_index,
                                    (idx.texcoord_index >= 0) ? idx.texcoord_index : std::numeric_limits<int>::min(),
                                    (idx.normal_index >= 0) ? idx.normal_index : std::numeric_limits<int>::min()
                                };

                                auto it = vertexMap.find(key);
                                if (it != vertexMap.end())
                                {
                                    indices.push_back(it->second);
                                }
                                else
                                {
                                    Vertex vert{};

                                    // position
                                    vert.position = {
                                        positions[idx.position_index * 3 + 0],
                                        positions[idx.position_index * 3 + 1],
                                        positions[idx.position_index * 3 + 2]
                                    };

                                    // texcoord
                                    if (idx.texcoord_index >= 0 && !texcoords.empty())
                                    {
                                        vert.texCoord = Vector2{ texcoords[idx.texcoord_index * 2 + 0], texcoords[idx.texcoord_index * 2 + 1] };
                                        //vert.texCoord = Vector2{ texcoords[idx.texcoord_index * 2 + 0], 1.0f - texcoords[idx.texcoord_index * 2 + 1] };
                                    }
                                    else
                                        vert.texCoord = Vector2{ 0.0f, 0.0f };

                                    // normal
                                    if (idx.normal_index >= 0 && hasNormals)
                                    {
                                        vert.normal = Vector3{
                                            normals[idx.normal_index * 3 + 0],
                                            normals[idx.normal_index * 3 + 1],
                                            normals[idx.normal_index * 3 + 2]
                                        }.Normalize();
                                    }
                                    else if (!smoothNormals.empty())
                                        vert.normal = smoothNormals[idx.position_index];
                                    else
                                        vert.normal = { 0.0f, 1.0f, 0.0f };

                                    // color
                                    //if (!colors.empty())
                                    //{
                                    //    size_t cBase = static_cast<size_t>(idx.position_index) * 3;
                                    //    if (cBase + 2 < colors.size())
                                    //    {
                                    //        vert.color = Color(
                                    //            static_cast<unsigned char>(std::clamp(colors[cBase + 0] * 255.0f, 0.0f, 255.0f)),
                                    //            static_cast<unsigned char>(std::clamp(colors[cBase + 1] * 255.0f, 0.0f, 255.0f)),
                                    //            static_cast<unsigned char>(std::clamp(colors[cBase + 2] * 255.0f, 0.0f, 255.0f)),
                                    //            255
                                    //        );
                                    //    }
                                    //}

                                    // default tangent/bitangent
                                    vert.tangent = Vector4(0, 0, 0, 1.0f);  // Default handedness to +1


                                    // skeletal defaults
                                    for (int bi = 0; bi < 4; ++bi)
                                    {
                                        vert.boneIndices[bi] = 0.0f;
                                        vert.boneWeights[bi] = 0.0f;
                                    }

                                    uint32_t newIndex = static_cast<uint32_t>(vertices.size());
                                    vertices.push_back(vert);
                                    indices.push_back(newIndex);
                                    vertexMap.emplace(key, newIndex);
                                }
                            }

                            idxOffset += numVerts;
                        } //

                        // Get material pointer
                        Material* mat = defaultMaterial;
                        if (matId >= 0 && static_cast<size_t>(matId) < materials.size() && materials[matId])
                            mat = materials[matId];

                        bool needsTangents = (mat->GetMaterialMap(MaterialMapType::Normal) != nullptr) && !vertices.empty();

                        // Compute tangents only if normal map present
                        if (needsTangents)
                        {
                            // Accumulate tangents
                            std::vector<Vector3> tanAccum(vertices.size(), { 0,0,0 });
                            std::vector<int> tanCount(vertices.size(), 0);
                            size_t triCount = indices.size() / 3;

                            // For large meshes use parallel
                            static const int TriThreshold = []() {
                                const int threads = static_cast<int>(GetThreadCount());
                                const int baseThreshold = 40000;
                                const int minThreshold = (threads > 8) ? 10000 : 20000;
                                return std::max(minThreshold, baseThreshold / std::max(1, threads));
                             }();
                            static const int VertThreshold = TriThreshold;

                            if (TriThreshold > TriThreshold && vertices.size() > VertThreshold)
                            {
                                // parallel loop by chunks
                                const size_t chunk = 4096;
                                std::atomic<size_t> triIdx(0);
                                size_t workerCount = std::min(GetThreadCount(), size_t(std::max<size_t>(1, (int)std::thread::hardware_concurrency())));
                                std::vector<std::thread> workers;
                                workers.reserve(workerCount);
                                for (size_t w = 0; w < workerCount; ++w)
                                {
                                    workers.emplace_back([&]() {
                                        size_t i;
                                        while ((i = triIdx.fetch_add(chunk)) < triCount)
                                        {
                                            size_t end = std::min(triCount, i + chunk);
                                            for (size_t tri = i; tri < end; ++tri)
                                            {
                                                uint32_t i0 = indices[tri * 3 + 0];
                                                uint32_t i1 = indices[tri * 3 + 1];
                                                uint32_t i2 = indices[tri * 3 + 2];

                                                const Vertex& v0 = vertices[i0];
                                                const Vertex& v1 = vertices[i1];
                                                const Vertex& v2 = vertices[i2];

                                                Vector3 edge1 = v1.position - v0.position;
                                                Vector3 edge2 = v2.position - v0.position;
                                                Vector2 deltaUV1 = v1.texCoord - v0.texCoord;
                                                Vector2 deltaUV2 = v2.texCoord - v0.texCoord;

                                                float denom = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
                                                float f = fabs(denom) < 1e-6f ? 0.0f : (1.0f / denom);

                                                Vector3 tangent;
                                                tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
                                                tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
                                                tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
                                                tangent = tangent.Normalize();

                                                tanAccum[i0] = tanAccum[i0] + tangent;
                                                tanAccum[i1] = tanAccum[i1] + tangent;
                                                tanAccum[i2] = tanAccum[i2] + tangent;
                                                tanCount[i0]++;
                                                tanCount[i1]++;
                                                tanCount[i2]++;
                                            }
                                        }
                                        });
                                }
                                for (auto& w : workers) w.join();
                            }
                            else
                            {
                                for (size_t tri = 0; tri < triCount; ++tri)
                                {
                                    uint32_t i0 = indices[tri * 3 + 0];
                                    uint32_t i1 = indices[tri * 3 + 1];
                                    uint32_t i2 = indices[tri * 3 + 2];

                                    const Vertex& v0 = vertices[i0];
                                    const Vertex& v1 = vertices[i1];
                                    const Vertex& v2 = vertices[i2];

                                    Vector3 edge1 = v1.position - v0.position;
                                    Vector3 edge2 = v2.position - v0.position;
                                    Vector2 deltaUV1 = v1.texCoord - v0.texCoord;
                                    Vector2 deltaUV2 = v2.texCoord - v0.texCoord;

                                    float denom = (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
                                    float f = fabs(denom) < 1e-6f ? 0.0f : (1.0f / denom);

                                    Vector3 tangent;
                                    tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
                                    tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
                                    tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
                                    tangent = tangent.Normalize();

                                    tanAccum[i0] = tanAccum[i0] + tangent;
                                    tanAccum[i1] = tanAccum[i1] + tangent;
                                    tanAccum[i2] = tanAccum[i2] + tangent;
                                    tanCount[i0]++;
                                    tanCount[i1]++;
                                    tanCount[i2]++;
                                }
                            }

                            // Normalize and orthogonalize per-vertex
                            for (size_t vi = 0; vi < vertices.size(); ++vi)
                            {
                                if (tanCount[vi] > 0)
                                {
                                    Vector3 tangent = (tanAccum[vi] / static_cast<float>(tanCount[vi])).Normalize();
                                    Vector3 normal = vertices[vi].normal;

                                    // Gram-Schmidt orthogonalize
                                    tangent = (tangent - normal * Vector3::Dot(normal, tangent)).Normalize();

                                    vertices[vi].tangent = Vector4(tangent.x, tangent.y, tangent.z, 1.0f);
                                }
                            }
                        }

                        // Create mesh object
                        auto newMesh = std::make_shared<Mesh>();
                        newMesh->SetSkinned(false);
                        newMesh->SetMaterial(mat);
                        newMesh->SetVertices(vertices);
                        newMesh->SetIndices(indices);

                        std::lock_guard<std::mutex> glock(allMeshesMutex);
                        allMeshes.push_back(newMesh);
                    }
                }
            });
        }

        for (auto& worker : shapeWorkers)
            worker.join();

        for (auto& mesh : allMeshes)
        {
            if (!mergeMeshes)
                mesh->Upload();

            model->AddMesh(mesh);
        }

        if (mergeMeshes)
        {
            if (!model->MergeMeshes())
            {
                for (auto& mesh : model->GetMeshes())
                    mesh.get()->Upload();
            }
        }

        return model;
    }
}