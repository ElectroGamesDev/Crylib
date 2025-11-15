#pragma once
#include "Mesh.h"
#include "Maths.h"
#include "Material.h"
#include "Animation.h"
#include <vector>
#include <string>
#include <memory>

namespace cl
{
    class Model
    {
        friend Model* CloneModel(const Model* model);
    public:
        static std::vector<Model*> s_models;

        Model();
        ~Model();
        Model(const Model&) = delete;
        Model& operator=(const Model&) = delete;
        Model(Model&& other) noexcept;
        Model& operator=(Model&& other) noexcept;

        void AddMesh(std::shared_ptr<Mesh> mesh);
        void RemoveMesh(size_t index);
        void RemoveMesh(const std::shared_ptr<Mesh>& mesh);
        void ClearMeshes();
        std::shared_ptr<Mesh> GetMesh(size_t index) const;
        const std::vector<std::shared_ptr<Mesh>>& GetMeshes() const { return m_meshes; }
        size_t GetMeshCount() const { return m_meshes.size(); }
        bool HasMeshes() const { return !m_meshes.empty(); }
        /// Merges compatible meshes to reduce draw calls. By default, this is automatically done when loading models, but will need to be manually called if new meshes are added to this model.
        bool MergeMeshes();

        void SetMaterial(Material* material);

        void SetPosition(const Vector3& pos);
        void SetPosition(float x, float y, float z);
        const Vector3& GetPosition() const { return m_position; }
        Vector3& GetPosition() { return m_position; }

        void SetRotation(const Vector3& rot);
        void SetRotation(float x, float y, float z);
        void SetRotationQuat(const Quaternion& rot);
        Vector3 GetRotation() const;
        Quaternion GetRotationQuat() const;

        void SetScale(const Vector3& scale);
        void SetScale(float x, float y, float z);
        void SetScale(float uniformScale);
        const Vector3& GetScale() const { return m_scale; }
        Vector3& GetScale() { return m_scale; }

        void Translate(const Vector3& offset);
        void Translate(float x, float y, float z);
        void Rotate(const Vector3& angles);
        void Rotate(float x, float y, float z);
        void Scale(const Vector3& scale);
        void Scale(float uniformScale);

        const Matrix4& GetTransformMatrix() const;
        void UpdateTransformMatrix();

        void Reset();
        void Destroy();

        // Animation methods
        void SetSkeleton(Skeleton* skeleton);
        Skeleton* GetSkeleton() const { return m_skeleton; }
        bool HasSkeleton() const { return m_skeleton != nullptr; }

        void AddAnimation(AnimationClip* clip);
        AnimationClip* GetAnimation(size_t index) const;
        AnimationClip* GetAnimation(const std::string_view name) const;

        size_t GetAnimationCount() const { return m_animations.size(); }

        Animator* GetAnimator() { return &m_animator; }
        const Animator* GetAnimator() const { return &m_animator; }
        std::vector<AnimationClip*>& GetAnimations() { return m_animations; };
        bool PlayAnimationByIndex(size_t index, bool loop);
        bool PlayAnimationByName(std::string_view name, bool loop);
        bool PlayAnimation(AnimationClip* clip, bool loop);
        void StopAnimation();
        void CrossfadeToAnimation(size_t index, float duration, bool loop = true);
        void CrossfadeToAnimation(std::string_view name, float duration, bool loop = true);

        void UpdateAnimation(float deltaTime);

        /// This sets every mesh within this model to skinned
        void SetSkinned(bool skinned);

        // Node management for animations
        void SetNodeCount(int count) { m_nodeCount = count; }
        //int GetNodeCount() const { return m_nodeCount; }

    private:
        Material* material;
        std::vector<std::shared_ptr<Mesh>> m_meshes;
        Vector3 m_position;
        Quaternion m_rotationQuat;
        Vector3 m_scale;
        Matrix4 m_transformMatrix;
        bool m_transformDirty;

        Skeleton* m_skeleton;
        std::vector<AnimationClip*> m_animations;
        Animator m_animator;

        int m_nodeCount;

        void MarkTransformDirty() { m_transformDirty = true; }
    };
}