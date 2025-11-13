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
        Vector4 tangent; // xyz = tangent direction, w = handedness (+1 or -1)
        Vector2 texCoord;
        Vector2 texCoord1;
        float boneIndices[4];
        float boneWeights[4];

        Vertex()
            : tangent(0.0f, 0.0f, 0.0f, 1.0f)
            , texCoord1(0.0f, 0.0f)
            , boneIndices{ 0.0f, 0.0f, 0.0f, 0.0f }
            , boneWeights{ 0.0f, 0.0f, 0.0f, 0.0f }
        {
        }
    };

    struct MorphTarget
    {
        std::vector<Vector3> positionDeltas;
        std::vector<Vector3> normalDeltas;
        std::vector<Vector3> tangentDeltas;
        std::string name;
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
        std::vector<Vertex>& GetVertices();
        std::vector<uint32_t>& GetIndices();
        int GetTriangleCount();
        void Upload();
        void Destroy();

        bgfx::VertexBufferHandle GetVertexBuffer() const { return m_vbh; }
        bgfx::IndexBufferHandle GetIndexBuffer() const { return m_ibh; }
        bool IsValid() const { return bgfx::isValid(m_vbh) && bgfx::isValid(m_ibh); }

        void SetMorphTargets(const std::vector<MorphTarget>& targets) { m_morphTargets = targets; }
        void SetMorphWeights(const std::vector<float>& weights) { m_morphWeights = weights; }
        const std::vector<MorphTarget>& GetMorphTargets() const { return m_morphTargets; }
        const std::vector<float>& GetMorphWeights() const { return m_morphWeights; }
        bool HasMorphTargets() const { return !m_morphTargets.empty(); }
        void ApplyMorphTargets();

        void SetMaterial(Material* material);
        Material* GetMaterial() { return m_material; }

        void SetSkinned(bool skinned);
        bool IsSkinned() const { return m_skinned; }

    private:
        std::vector<Vertex> m_vertices;
        std::vector<uint32_t> m_indices;
        bgfx::VertexBufferHandle m_vbh;
        bgfx::IndexBufferHandle m_ibh;
        std::vector<MorphTarget> m_morphTargets;
        std::vector<float> m_morphWeights;
        bool m_uploaded;
        bool m_skinned;
        Material* m_material;
    };
}