#include "Primitives.h"
#include "Shader.h"
#include <cmath>

namespace cl
{
    static Material m_primitiveMaterial;

    static void CalculateTangentsAndBitangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
    {
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            Vertex& v0 = vertices[indices[i]];
            Vertex& v1 = vertices[indices[i + 1]];
            Vertex& v2 = vertices[indices[i + 2]];

            Vector3 edge1 = v1.position - v0.position;
            Vector3 edge2 = v2.position - v0.position;
            Vector2 deltaUV1 = v1.texCoord - v0.texCoord;
            Vector2 deltaUV2 = v2.texCoord - v0.texCoord;

            float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y + 0.00001f);

            Vector3 tangent;
            tangent.x = f * (deltaUV2.y * edge1.x - deltaUV1.y * edge2.x);
            tangent.y = f * (deltaUV2.y * edge1.y - deltaUV1.y * edge2.y);
            tangent.z = f * (deltaUV2.y * edge1.z - deltaUV1.y * edge2.z);
            tangent = tangent.Normalize();

            Vector3 bitangent;
            bitangent.x = f * (-deltaUV2.x * edge1.x + deltaUV1.x * edge2.x);
            bitangent.y = f * (-deltaUV2.x * edge1.y + deltaUV1.x * edge2.y);
            bitangent.z = f * (-deltaUV2.x * edge1.z + deltaUV1.x * edge2.z);
            bitangent = bitangent.Normalize();

            // Accumulate tangent
            v0.tangent = Vector4(v0.tangent.x + tangent.x, v0.tangent.y + tangent.y, v0.tangent.z + tangent.z, v0.tangent.w);
            v1.tangent = Vector4(v1.tangent.x + tangent.x, v1.tangent.y + tangent.y, v1.tangent.z + tangent.z, v1.tangent.w);
            v2.tangent = Vector4(v2.tangent.x + tangent.x, v2.tangent.y + tangent.y, v2.tangent.z + tangent.z, v2.tangent.w);
        }

        // Normalize and calculate handedness
        for (auto& v : vertices)
        {
            Vector3 tangent = Vector3(v.tangent.x, v.tangent.y, v.tangent.z).Normalize();
            Vector3 normal = v.normal;

            // Gram-Schmidt orthogonalize
            tangent = (tangent - normal * Vector3::Dot(normal, tangent)).Normalize();

            // Calculate handedness
            Vector3 bitangent = Vector3::Cross(normal, tangent);
            float handedness = 1.0f; // Default to right-handed

            v.tangent = Vector4(tangent.x, tangent.y, tangent.z, handedness);
        }
    }

    static void ApplyInwardNormals(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
    {
        for (auto& v : vertices)
            v.normal = v.normal * -1.0f;

        for (size_t i = 0; i < indices.size(); i += 3)
            std::swap(indices[i + 1], indices[i + 2]);
    }

    static void ApplyCentering(std::vector<Vertex>& vertices, const Vector3& offset)
    {
        for (auto& v : vertices)
            v.position = v.position + offset;
    }

    void InitPrimitives()
    {
        m_primitiveMaterial.SetShader(s_defaultShader);
    }

    Material* GetPrimitiveMaterial()
    {
        return &m_primitiveMaterial;
    }

    // Generate meshes

    Mesh GenCubeMesh(float width, float height, float length, bool smoothNormals, bool generateUVs, bool inward, bool centered)
    {
        Mesh mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float hw = width * 0.5f;
        float hh = height * 0.5f;
        float hl = length * 0.5f;

        Vector3 offset = centered ? Vector3(0.0f, 0.0f, 0.0f) : Vector3(hw, hh, hl);

        if (smoothNormals)
        {
            Vector3 positions[8] = {
                {-hw, -hh, -hl}, {hw, -hh, -hl}, {hw, hh, -hl}, {-hw, hh, -hl},
                {-hw, -hh, hl}, {hw, -hh, hl}, {hw, hh, hl}, {-hw, hh, hl}
            };

            for (int i = 0; i < 8; ++i)
            {
                Vertex v;
                v.position = positions[i] + offset;
                v.normal = v.position.Normalize();
                v.texCoord = generateUVs ? Vector2((i & 1) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f) : Vector2(0.0f, 0.0f);
                vertices.push_back(v);
            }

            uint32_t cubeIndices[36] = {
                0, 1, 2, 0, 2, 3, 1, 5, 6, 1, 6, 2,
                5, 4, 7, 5, 7, 6, 4, 0, 3, 4, 3, 7,
                3, 2, 6, 3, 6, 7, 4, 5, 1, 4, 1, 0
            };
            indices.assign(cubeIndices, cubeIndices + 36);
        }
        else
        {
            struct Face { Vector3 v[4]; Vector3 n; Vector2 uv[4]; };
            Face faces[6] = {
                {{{hw, -hh, -hl}, {hw, -hh, hl}, {hw, hh, hl}, {hw, hh, -hl}}, {1, 0, 0}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
                {{{-hw, -hh, hl}, {-hw, -hh, -hl}, {-hw, hh, -hl}, {-hw, hh, hl}}, {-1, 0, 0}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
                {{{-hw, hh, -hl}, {hw, hh, -hl}, {hw, hh, hl}, {-hw, hh, hl}}, {0, 1, 0}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
                {{{-hw, -hh, hl}, {hw, -hh, hl}, {hw, -hh, -hl}, {-hw, -hh, -hl}}, {0, -1, 0}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
                {{{-hw, -hh, hl}, {hw, -hh, hl}, {hw, hh, hl}, {-hw, hh, hl}}, {0, 0, 1}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}},
                {{{hw, -hh, -hl}, {-hw, -hh, -hl}, {-hw, hh, -hl}, {hw, hh, -hl}}, {0, 0, -1}, {{0, 0}, {1, 0}, {1, 1}, {0, 1}}}
            };

            for (int f = 0; f < 6; ++f)
            {
                uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
                for (int i = 0; i < 4; ++i)
                {
                    Vertex v;
                    v.position = faces[f].v[i] + offset;
                    v.normal = faces[f].n;
                    v.texCoord = generateUVs ? faces[f].uv[i] : Vector2(0.0f, 0.0f);
                    vertices.push_back(v);
                }
                indices.push_back(baseIdx); indices.push_back(baseIdx + 2); indices.push_back(baseIdx + 1);
                indices.push_back(baseIdx); indices.push_back(baseIdx + 3); indices.push_back(baseIdx + 2);
            }
        }

        if (inward)
            ApplyInwardNormals(vertices, indices);

        CalculateTangentsAndBitangents(vertices, indices);
        mesh.SetVertices(vertices);
        mesh.SetIndices(indices);
        mesh.SetMaterial(&m_primitiveMaterial);
        mesh.Upload();
        return mesh;
    }

    Mesh GenSphereMesh(float radius, int rings, int slices, bool smoothNormals, bool generateUVs, bool inward, bool centered, float startAngle, float endAngle, bool hemiTop, bool hemiBottom)
    {
        Mesh mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        int ringStart = hemiTop ? rings / 2 : 0;
        int ringEnd = hemiBottom ? rings / 2 : rings;
        float angleStart = startAngle * (3.14159265359f / 180.0f);
        float angleEnd = endAngle * (3.14159265359f / 180.0f);

        Vector3 offset = centered ? Vector3(0.0f, 0.0f, 0.0f) : Vector3(0.0f, radius, 0.0f);

        for (int r = ringStart; r <= ringEnd; ++r)
        {
            float phi = 3.14159265359f * static_cast<float>(r) / static_cast<float>(rings);
            float sinPhi = sinf(phi);
            float cosPhi = cosf(phi);

            for (int s = 0; s <= slices; ++s)
            {
                float theta = angleStart + (angleEnd - angleStart) * static_cast<float>(s) / static_cast<float>(slices);
                float sinTheta = sinf(theta);
                float cosTheta = cosf(theta);

                Vertex v;
                v.position.x = radius * sinPhi * cosTheta;
                v.position.y = radius * cosPhi;
                v.position.z = radius * sinPhi * sinTheta;
                v.position = v.position + offset;

                v.normal = Vector3(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
                v.texCoord = generateUVs ? Vector2(static_cast<float>(s) / slices, static_cast<float>(r - ringStart) / (ringEnd - ringStart)) : Vector2(0.0f, 0.0f);
                vertices.push_back(v);
            }
        }

        int ringCount = ringEnd - ringStart;
        for (int r = 0; r < ringCount; ++r)
        {
            for (int s = 0; s < slices; ++s)
            {
                uint32_t i0 = r * (slices + 1) + s;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + (slices + 1);
                uint32_t i3 = i2 + 1;

                indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
            }
        }

        if (inward)
            ApplyInwardNormals(vertices, indices);

        CalculateTangentsAndBitangents(vertices, indices);
        mesh.SetVertices(vertices);
        mesh.SetIndices(indices);
        mesh.SetMaterial(&m_primitiveMaterial);
        mesh.Upload();
        return mesh;
    }

    Mesh GenPlaneMesh(float width, float length, int resX, int resZ, bool smoothNormals, bool generateUVs, bool inward, bool centered, Vector2 texRepeat, bool doubleSided)
    {
        Mesh mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float hw = width * 0.5f;
        float hl = length * 0.5f;
        Vector3 offset = centered ? Vector3(0.0f, 0.0f, 0.0f) : Vector3(hw, 0.0f, hl);

        for (int z = 0; z <= resZ; ++z)
        {
            for (int x = 0; x <= resX; ++x)
            {
                float px = -hw + width * static_cast<float>(x) / resX;
                float pz = -hl + length * static_cast<float>(z) / resZ;

                Vertex v;
                v.position = Vector3(px, 0.0f, pz) + offset;
                v.normal = Vector3(0.0f, 1.0f, 0.0f);
                v.texCoord = generateUVs ? Vector2(texRepeat.x * static_cast<float>(x) / resX, texRepeat.y * static_cast<float>(z) / resZ) : Vector2(0.0f, 0.0f);
                vertices.push_back(v);
            }
        }

        for (int z = 0; z < resZ; ++z)
        {
            for (int x = 0; x < resX; ++x)
            {
                uint32_t i0 = z * (resX + 1) + x;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + (resX + 1);
                uint32_t i3 = i2 + 1;

                indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
            }
        }

        if (doubleSided)
        {
            size_t origVertCount = vertices.size();
            for (size_t i = 0; i < origVertCount; ++i)
            {
                Vertex v = vertices[i];
                v.normal = v.normal * -1.0f;
                vertices.push_back(v);
            }

            size_t origIndexCount = indices.size();
            for (size_t i = 0; i < origIndexCount; i += 3)
            {
                indices.push_back(indices[i] + static_cast<uint32_t>(origVertCount));
                indices.push_back(indices[i + 1] + static_cast<uint32_t>(origVertCount));
                indices.push_back(indices[i + 2] + static_cast<uint32_t>(origVertCount));
            }
        }
        else if (inward)
            ApplyInwardNormals(vertices, indices);

        CalculateTangentsAndBitangents(vertices, indices);
        mesh.SetVertices(vertices);
        mesh.SetIndices(indices);
        mesh.SetMaterial(&m_primitiveMaterial);
        mesh.Upload();
        return mesh;
    }

    Mesh GenCylinderMesh(float radius, float height, int slices, bool smoothNormals, bool generateUVs, bool inward, bool centered, bool cappedTop, bool cappedBottom)
    {
        Mesh mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float hh = height * 0.5f;
        Vector3 offset = centered ? Vector3(0.0f, 0.0f, 0.0f) : Vector3(0.0f, hh, 0.0f);

        for (int i = 0; i <= slices; ++i)
        {
            float angle = 2.0f * 3.14159265359f * static_cast<float>(i) / slices;
            float x = radius * cosf(angle);
            float z = radius * sinf(angle);

            Vertex vTop, vBottom;
            vTop.position = Vector3(x, hh, z) + offset;
            vBottom.position = Vector3(x, -hh, z) + offset;
            vTop.normal = vBottom.normal = Vector3(x, 0.0f, z).Normalize();
            vTop.texCoord = generateUVs ? Vector2(static_cast<float>(i) / slices, 1.0f) : Vector2(0.0f, 0.0f);
            vBottom.texCoord = generateUVs ? Vector2(static_cast<float>(i) / slices, 0.0f) : Vector2(0.0f, 0.0f);

            vertices.push_back(vBottom);
            vertices.push_back(vTop);
        }

        for (int i = 0; i < slices; ++i)
        {
            uint32_t i0 = i * 2;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + 2;
            uint32_t i3 = i2 + 1;

            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }

        if (cappedBottom)
        {
            uint32_t centerIdx = static_cast<uint32_t>(vertices.size());
            Vertex center;
            center.position = Vector3(0.0f, -hh, 0.0f) + offset;
            center.normal = Vector3(0.0f, -1.0f, 0.0f);
            center.texCoord = Vector2(0.5f, 0.5f);
            vertices.push_back(center);

            for (int i = 0; i < slices; ++i)
            {
                float angle1 = 2.0f * 3.14159265359f * static_cast<float>(i) / slices;
                float angle2 = 2.0f * 3.14159265359f * static_cast<float>(i + 1) / slices;

                Vertex v1, v2;
                v1.position = Vector3(radius * cosf(angle1), -hh, radius * sinf(angle1)) + offset;
                v2.position = Vector3(radius * cosf(angle2), -hh, radius * sinf(angle2)) + offset;
                v1.normal = v2.normal = Vector3(0.0f, -1.0f, 0.0f);
                v1.texCoord = generateUVs ? Vector2(0.5f + 0.5f * cosf(angle1), 0.5f + 0.5f * sinf(angle1)) : Vector2(0.0f, 0.0f);
                v2.texCoord = generateUVs ? Vector2(0.5f + 0.5f * cosf(angle2), 0.5f + 0.5f * sinf(angle2)) : Vector2(0.0f, 0.0f);

                uint32_t idx1 = static_cast<uint32_t>(vertices.size());
                vertices.push_back(v1);
                vertices.push_back(v2);

                indices.push_back(centerIdx); indices.push_back(idx1 + 1); indices.push_back(idx1);
            }
        }

        if (cappedTop)
        {
            uint32_t centerIdx = static_cast<uint32_t>(vertices.size());
            Vertex center;
            center.position = Vector3(0.0f, hh, 0.0f) + offset;
            center.normal = Vector3(0.0f, 1.0f, 0.0f);
            center.texCoord = Vector2(0.5f, 0.5f);
            vertices.push_back(center);

            for (int i = 0; i < slices; ++i)
            {
                float angle1 = 2.0f * 3.14159265359f * static_cast<float>(i) / slices;
                float angle2 = 2.0f * 3.14159265359f * static_cast<float>(i + 1) / slices;

                Vertex v1, v2;
                v1.position = Vector3(radius * cosf(angle1), hh, radius * sinf(angle1)) + offset;
                v2.position = Vector3(radius * cosf(angle2), hh, radius * sinf(angle2)) + offset;
                v1.normal = v2.normal = Vector3(0.0f, 1.0f, 0.0f);
                v1.texCoord = generateUVs ? Vector2(0.5f + 0.5f * cosf(angle1), 0.5f + 0.5f * sinf(angle1)) : Vector2(0.0f, 0.0f);
                v2.texCoord = generateUVs ? Vector2(0.5f + 0.5f * cosf(angle2), 0.5f + 0.5f * sinf(angle2)) : Vector2(0.0f, 0.0f);

                uint32_t idx1 = static_cast<uint32_t>(vertices.size());
                vertices.push_back(v1);
                vertices.push_back(v2);

                indices.push_back(centerIdx); indices.push_back(idx1 + 1); indices.push_back(idx1);
            }
        }

        if (inward)
            ApplyInwardNormals(vertices, indices);

        CalculateTangentsAndBitangents(vertices, indices);
        mesh.SetVertices(vertices);
        mesh.SetIndices(indices);
        mesh.SetMaterial(&m_primitiveMaterial);
        mesh.Upload();
        return mesh;
    }

    Mesh GenConeMesh(float radius, float height, int slices, bool smoothNormals, bool generateUVs, bool inward, bool centered, bool capped, float topRadius)
    {
        Mesh mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float hh = height * 0.5f;
        Vector3 offset = centered ? Vector3(0.0f, 0.0f, 0.0f) : Vector3(0.0f, hh, 0.0f);

        float slant = sqrtf(radius * radius + height * height);
        float normalY = radius / slant;
        float normalXZ = height / slant;

        for (int i = 0; i <= slices; ++i)
        {
            float angle = 2.0f * 3.14159265359f * static_cast<float>(i) / slices;
            float x = cosf(angle);
            float z = sinf(angle);

            Vertex vTop, vBottom;
            vTop.position = Vector3(topRadius * x, hh, topRadius * z) + offset;
            vBottom.position = Vector3(radius * x, -hh, radius * z) + offset;

            if (smoothNormals)
                vTop.normal = vBottom.normal = Vector3(normalXZ * x, normalY, normalXZ * z).Normalize();
            else
                vTop.normal = vBottom.normal = Vector3(x, 0.0f, z).Normalize();

            vTop.texCoord = generateUVs ? Vector2(static_cast<float>(i) / slices, 1.0f) : Vector2(0.0f, 0.0f);
            vBottom.texCoord = generateUVs ? Vector2(static_cast<float>(i) / slices, 0.0f) : Vector2(0.0f, 0.0f);

            vertices.push_back(vBottom);
            vertices.push_back(vTop);
        }

        for (int i = 0; i < slices; ++i)
        {
            uint32_t i0 = i * 2;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + 2;
            uint32_t i3 = i2 + 1;

            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }

        if (capped)
        {
            uint32_t centerIdx = static_cast<uint32_t>(vertices.size());
            Vertex center;
            center.position = Vector3(0.0f, -hh, 0.0f) + offset;
            center.normal = Vector3(0.0f, -1.0f, 0.0f);
            center.texCoord = Vector2(0.5f, 0.5f);
            vertices.push_back(center);

            for (int i = 0; i < slices; ++i)
            {
                float angle1 = 2.0f * 3.14159265359f * static_cast<float>(i) / slices;
                float angle2 = 2.0f * 3.14159265359f * static_cast<float>(i + 1) / slices;

                Vertex v1, v2;
                v1.position = Vector3(radius * cosf(angle1), -hh, radius * sinf(angle1)) + offset;
                v2.position = Vector3(radius * cosf(angle2), -hh, radius * sinf(angle2)) + offset;
                v1.normal = v2.normal = Vector3(0.0f, -1.0f, 0.0f);
                v1.texCoord = generateUVs ? Vector2(0.5f + 0.5f * cosf(angle1), 0.5f + 0.5f * sinf(angle1)) : Vector2(0.0f, 0.0f);
                v2.texCoord = generateUVs ? Vector2(0.5f + 0.5f * cosf(angle2), 0.5f + 0.5f * sinf(angle2)) : Vector2(0.0f, 0.0f);

                uint32_t idx1 = static_cast<uint32_t>(vertices.size());
                vertices.push_back(v1);
                vertices.push_back(v2);

                indices.push_back(centerIdx); indices.push_back(idx1 + 1); indices.push_back(idx1);
            }
        }

        if (inward)
            ApplyInwardNormals(vertices, indices);

        CalculateTangentsAndBitangents(vertices, indices);
        mesh.SetVertices(vertices);
        mesh.SetIndices(indices);
        mesh.SetMaterial(&m_primitiveMaterial);
        mesh.Upload();
        return mesh;
    }

    Mesh GenQuadMesh(float width, float height, int resX, int resY, bool smoothNormals, bool generateUVs, bool inward, bool centered, Vector2 texRepeat, bool doubleSided)
    {
        Mesh mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float hw = width * 0.5f;
        float hh = height * 0.5f;
        Vector3 offset = centered ? Vector3(0.0f, 0.0f, 0.0f) : Vector3(hw, hh, 0.0f);

        for (int y = 0; y <= resY; ++y)
        {
            for (int x = 0; x <= resX; ++x)
            {
                float px = -hw + width * static_cast<float>(x) / resX;
                float py = -hh + height * static_cast<float>(y) / resY;

                Vertex v;
                v.position = Vector3(px, py, 0.0f) + offset;
                v.normal = Vector3(0.0f, 0.0f, -1.0f);
                v.texCoord = generateUVs ? Vector2(texRepeat.x * static_cast<float>(x) / resX, texRepeat.y * static_cast<float>(y) / resY) : Vector2(0.0f, 0.0f);
                vertices.push_back(v);
            }
        }

        for (int y = 0; y < resY; ++y)
        {
            for (int x = 0; x < resX; ++x)
            {
                uint32_t i0 = y * (resX + 1) + x;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + (resX + 1);
                uint32_t i3 = i2 + 1;

                indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
            }
        }

        if (doubleSided)
        {
            size_t origVertCount = vertices.size();
            for (size_t i = 0; i < origVertCount; ++i)
            {
                Vertex v = vertices[i];
                v.normal = v.normal * -1.0f;
                vertices.push_back(v);
            }

            size_t origIndexCount = indices.size();
            for (size_t i = 0; i < origIndexCount; i += 3)
            {
                indices.push_back(indices[i] + static_cast<uint32_t>(origVertCount));
                indices.push_back(indices[i + 1] + static_cast<uint32_t>(origVertCount));
                indices.push_back(indices[i + 2] + static_cast<uint32_t>(origVertCount));
            }
        }
        else if (inward)
            ApplyInwardNormals(vertices, indices);

        CalculateTangentsAndBitangents(vertices, indices);
        mesh.SetVertices(vertices);
        mesh.SetIndices(indices);
        mesh.SetMaterial(&m_primitiveMaterial);
        mesh.Upload();
        return mesh;
    }

    Mesh GenCapsuleMesh(float radius, float height, int slices, int stacks, bool smoothNormals, bool generateUVs, bool inward, bool centered, float capRatio)
    {
        Mesh mesh;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float cylHeight = height * (1.0f - capRatio);
        float capHeight = height * capRatio * 0.5f;
        float hCyl = cylHeight * 0.5f;

        Vector3 offset = centered ? Vector3(0.0f, 0.0f, 0.0f) : Vector3(0.0f, height * 0.5f, 0.0f);

        for (int i = 0; i <= slices; ++i)
        {
            float angle = 2.0f * 3.14159265359f * static_cast<float>(i) / slices;
            float x = radius * cosf(angle);
            float z = radius * sinf(angle);

            Vertex vTop, vBottom;
            vTop.position = Vector3(x, hCyl, z) + offset;
            vBottom.position = Vector3(x, -hCyl, z) + offset;
            vTop.normal = vBottom.normal = Vector3(x, 0.0f, z).Normalize();
            vTop.texCoord = generateUVs ? Vector2(static_cast<float>(i) / slices, 0.75f) : Vector2(0.0f, 0.0f);
            vBottom.texCoord = generateUVs ? Vector2(static_cast<float>(i) / slices, 0.25f) : Vector2(0.0f, 0.0f);

            vertices.push_back(vBottom);
            vertices.push_back(vTop);
        }

        for (int i = 0; i < slices; ++i)
        {
            uint32_t i0 = i * 2;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + 2;
            uint32_t i3 = i2 + 1;

            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
        }

        for (int r = 0; r <= stacks; ++r)
        {
            float phi = 3.14159265359f * 0.5f * static_cast<float>(r) / stacks;
            float sinPhi = sinf(phi);
            float cosPhi = cosf(phi);

            for (int s = 0; s <= slices; ++s)
            {
                float theta = 2.0f * 3.14159265359f * static_cast<float>(s) / slices;
                float sinTheta = sinf(theta);
                float cosTheta = cosf(theta);

                Vertex v;
                v.position.x = radius * sinPhi * cosTheta;
                v.position.y = hCyl + radius * cosPhi;
                v.position.z = radius * sinPhi * sinTheta;
                v.position = v.position + offset;
                v.normal = Vector3(sinPhi * cosTheta, cosPhi, sinPhi * sinTheta);
                v.texCoord = generateUVs ? Vector2(static_cast<float>(s) / slices, 0.75f + 0.25f * static_cast<float>(r) / stacks) : Vector2(0.0f, 0.0f);
                vertices.push_back(v);
            }
        }

        uint32_t topCapBase = static_cast<uint32_t>(vertices.size()) - (slices + 1) * (stacks + 1);
        for (int r = 0; r < stacks; ++r)
        {
            for (int s = 0; s < slices; ++s)
            {
                uint32_t i0 = topCapBase + r * (slices + 1) + s;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + (slices + 1);
                uint32_t i3 = i2 + 1;

                indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
            }
        }

        for (int r = 0; r <= stacks; ++r)
        {
            float phi = 3.14159265359f * 0.5f * static_cast<float>(r) / stacks;
            float sinPhi = sinf(phi);
            float cosPhi = cosf(phi);

            for (int s = 0; s <= slices; ++s)
            {
                float theta = 2.0f * 3.14159265359f * static_cast<float>(s) / slices;
                float sinTheta = sinf(theta);
                float cosTheta = cosf(theta);

                Vertex v;
                v.position.x = radius * sinPhi * cosTheta;
                v.position.y = -hCyl - radius * cosPhi;
                v.position.z = radius * sinPhi * sinTheta;
                v.position = v.position + offset;
                v.normal = Vector3(sinPhi * cosTheta, -cosPhi, sinPhi * sinTheta);
                v.texCoord = generateUVs ? Vector2(static_cast<float>(s) / slices, 0.25f - 0.25f * static_cast<float>(r) / stacks) : Vector2(0.0f, 0.0f);
                vertices.push_back(v);
            }
        }

        uint32_t bottomCapBase = static_cast<uint32_t>(vertices.size()) - (slices + 1) * (stacks + 1);
        for (int r = 0; r < stacks; ++r)
        {
            for (int s = 0; s < slices; ++s)
            {
                uint32_t i0 = bottomCapBase + r * (slices + 1) + s;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + (slices + 1);
                uint32_t i3 = i2 + 1;

                indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
                indices.push_back(i1); indices.push_back(i3); indices.push_back(i2);
            }
        }

        if (inward)
            ApplyInwardNormals(vertices, indices);

        CalculateTangentsAndBitangents(vertices, indices);
        mesh.SetVertices(vertices);
        mesh.SetIndices(indices);
        mesh.SetMaterial(&m_primitiveMaterial);
        mesh.Upload();
        return mesh;
    }

    // Generate model from mesh

    Model GenModelFromMesh(const Mesh& mesh)
    {
        Model model;
        auto meshPtr = std::make_shared<Mesh>(mesh);
        meshPtr->SetMaterial(&m_primitiveMaterial);
        model.AddMesh(meshPtr);
        return model;
    }

    // Generate models

    Model GenCubeModel(float width, float height, float length, bool smoothNormals, bool generateUVs, bool inward, bool centered)
    {
        Mesh mesh = GenCubeMesh(width, height, length, smoothNormals, generateUVs, inward, centered);
        return GenModelFromMesh(mesh);
    }

    Model GenSphereModel(float radius, int rings, int slices, bool smoothNormals, bool generateUVs, bool inward, bool centered, float startAngle, float endAngle, bool hemiTop, bool hemiBottom)
    {
        Mesh mesh = GenSphereMesh(radius, rings, slices, smoothNormals, generateUVs, inward, centered, startAngle, endAngle, hemiTop, hemiBottom);
        return GenModelFromMesh(mesh);
    }

    Model GenPlaneModel(float width, float length, int resX, int resZ, bool smoothNormals, bool generateUVs, bool inward, bool centered, Vector2 texRepeat, bool doubleSided)
    {
        Mesh mesh = GenPlaneMesh(width, length, resX, resZ, smoothNormals, generateUVs, inward, centered, texRepeat, doubleSided);
        return GenModelFromMesh(mesh);
    }

    Model GenCylinderModel(float radius, float height, int slices, bool smoothNormals, bool generateUVs, bool inward, bool centered, bool cappedTop, bool cappedBottom)
    {
        Mesh mesh = GenCylinderMesh(radius, height, slices, smoothNormals, generateUVs, inward, centered, cappedTop, cappedBottom);
        return GenModelFromMesh(mesh);
    }

    Model GenConeModel(float radius, float height, int slices, bool smoothNormals, bool generateUVs, bool inward, bool centered, bool capped, float topRadius)
    {
        Mesh mesh = GenConeMesh(radius, height, slices, smoothNormals, generateUVs, inward, centered, capped, topRadius);
        return GenModelFromMesh(mesh);
    }

    Model GenQuadModel(float width, float length, int resX, int resZ, bool smoothNormals, bool generateUVs, bool inward, bool centered, Vector2 texRepeat, bool doubleSided)
    {
        Mesh mesh = GenQuadMesh(width, length, resX, resZ, smoothNormals, generateUVs, inward, centered, texRepeat, doubleSided);
        return GenModelFromMesh(mesh);
    }

    Model GenCapsuleModel(float radius, float height, int slices, int stacks, bool smoothNormals, bool generateUVs, bool inward, bool centered, float capRatio)
    {
        Mesh mesh = GenCapsuleMesh(radius, height, slices, stacks, smoothNormals, generateUVs, inward, centered, capRatio);
        return GenModelFromMesh(mesh);
    }
}