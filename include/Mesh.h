#pragma once
#include "Maths.h"
#include <vector>
#include <bgfx.h>
#include "Material.h"

namespace cl
{
    struct Vertex
    {
        Vector3 position;
        Vector3 normal;
        Vector3 tangent;
        Vector3 bitangent;
        Vector2 texCoord;
        Color color;
        float boneIndices[4];
        float boneWeights[4];

        Vertex()
            : color(Color::White())
            , boneIndices{ 0.0f, 0.0f, 0.0f, 0.0f }
            , boneWeights{ 0.0f, 0.0f, 0.0f, 0.0f }
        {
        }
    };

    class Mesh
    {
    public:
        static std::vector<Mesh*> s_meshes;

        Mesh();
        ~Mesh();
        Mesh(const Mesh& other);

        void SetVertices(const std::vector<Vertex>& vertices);
        void SetIndices(const std::vector<uint32_t>& indices);
        const std::vector<Vertex>& GetVertices();
        const std::vector<uint32_t>& GetIndices();
        int GetTriangleCount();
        void Upload();
        void Destroy();

        bgfx::VertexBufferHandle GetVertexBuffer() const { return m_vbh; }
        bgfx::IndexBufferHandle GetIndexBuffer() const { return m_ibh; }
        bool IsValid() const { return bgfx::isValid(m_vbh) && bgfx::isValid(m_ibh); }

        void SetMaterial(Material* material);
        Material* GetMaterial() { return m_material; }

        void SetSkinned(bool skinned);
        bool IsSkinned() const { return m_skinned; }

    private:
        std::vector<Vertex> m_vertices;
        std::vector<uint32_t> m_indices;
        bgfx::VertexBufferHandle m_vbh;
        bgfx::IndexBufferHandle m_ibh;
        bool m_uploaded;
        bool m_skinned;
        Material* m_material;
    };
}