#include "Model.h"
#include "Material.h"
#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <future>

namespace cl
{
    std::vector<Model*> Model::s_models;

    Model::Model()
        : m_position(0.0f, 0.0f, 0.0f)
        , m_rotationQuat(0.0f, 0.0f, 0.0f, 1.0f)
        , m_scale(1.0f, 1.0f, 1.0f)
        , m_transformMatrix(Matrix4::Identity())
        , m_transformDirty(false)
        , m_skeleton(nullptr)
        , m_nodeCount(0)
    {
        s_models.push_back(this);
    }

    Model::~Model()
    {
        Destroy();

        for (size_t i = 0; i < s_models.size(); ++i)
        {
            if (s_models[i] == this)
            {
                if (i != s_models.size() - 1)
                    std::swap(s_models[i], s_models.back());

                s_models.pop_back();
                return;
            }
        }
    }

    Model::Model(Model&& other) noexcept
        : m_meshes(std::move(other.m_meshes))
        , m_position(other.m_position)
        , m_rotationQuat(other.m_rotationQuat)
        , m_scale(other.m_scale)
        , m_transformMatrix(other.m_transformMatrix)
        , m_transformDirty(other.m_transformDirty)
        , m_skeleton(other.m_skeleton)
        , m_animations(std::move(other.m_animations))
        , m_animator(std::move(other.m_animator))
        , m_nodeCount(other.m_nodeCount)
    {
        other.m_meshes.clear();
        other.m_skeleton = nullptr;
        other.m_animations.clear();
        other.m_nodeCount = 0;
        s_models.push_back(this);
    }

    Model& Model::operator=(Model&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_meshes = std::move(other.m_meshes);
            m_position = other.m_position;
            m_rotationQuat = other.m_rotationQuat;
            m_scale = other.m_scale;
            m_transformMatrix = other.m_transformMatrix;
            m_transformDirty = other.m_transformDirty;
            m_skeleton = other.m_skeleton;
            m_animations = std::move(other.m_animations);
            m_nodeCount = other.m_nodeCount;
            m_animator = std::move(other.m_animator);
            other.m_meshes.clear();
            other.m_skeleton = nullptr;
            other.m_animations.clear();
            other.m_nodeCount = 0;
        }
        return *this;
    }

    void Model::AddMesh(std::shared_ptr<Mesh> mesh)
    {
        if (!mesh.get()->GetMaterial())
            mesh.get()->SetMaterial(material);

        m_meshes.push_back(mesh);
    }

    void Model::RemoveMesh(size_t index)
    {
        if (index >= m_meshes.size())
            return;

        m_meshes.erase(m_meshes.begin() + index);
    }

    void Model::RemoveMesh(const std::shared_ptr<Mesh>& mesh)
    {
        auto it = std::find(m_meshes.begin(), m_meshes.end(), mesh);
        if (it != m_meshes.end())
        {
            size_t index = std::distance(m_meshes.begin(), it);
            RemoveMesh(index);
        }
    }

    void Model::ClearMeshes()
    {
        m_meshes.clear();
    }

    std::shared_ptr<Mesh> Model::GetMesh(size_t index) const
    {
        if (index >= m_meshes.size())
            return nullptr;

        return m_meshes[index];
    }

    bool Model::MergeMeshes()
    {
        if (m_meshes.empty() || HasSkeleton() || GetAnimationCount() > 0)
            return false;
        
        // Todo: This likely breaks GetNodeCount(), but it doesn't look like that function is currently used

        std::unordered_map<Material*, std::vector<std::shared_ptr<Mesh>>> groups;

        // Group non-skinned meshes by material
        for (const auto& mesh : m_meshes)
        {
            if (!mesh || mesh->IsSkinned())
                continue;

            Material* mat = mesh->GetMaterial();
            groups[mat].push_back(mesh);
        }

        std::vector<std::shared_ptr<Mesh>> newMeshes;
        newMeshes.reserve(m_meshes.size());

        // Add skinned meshes to the newMeshes vector since they will not be merged
        for (const auto& mesh : m_meshes)
        {
            if (!mesh || mesh->IsSkinned())
                newMeshes.push_back(mesh);
        }

        std::vector<std::future<std::shared_ptr<Mesh>>> futures;

        for (auto& pair : groups)
        {
            auto& group = pair.second;
            if (group.size() <= 1)
            {
                if (!group.empty())
                {
                    auto mesh = group.front();
                    mesh->Upload();
                    newMeshes.push_back(mesh);
                }

                continue;
            }

            // Merge groups
            auto mergeGroup = [&group, mat = pair.first]() -> std::shared_ptr<Mesh>
            {
                size_t totalVertices = 0, totalIndices = 0;
                for (const auto& mesh : group)
                {
                    totalVertices += mesh->GetVertices().size();
                    totalIndices += mesh->GetIndices().size();
                }

                auto mergedMesh = std::make_shared<Mesh>();
                auto& mergedVertices = mergedMesh->GetVertices();
                auto& mergedIndices = mergedMesh->GetIndices();
                mergedVertices.reserve(totalVertices);
                mergedIndices.reserve(totalIndices);

                uint32_t vertexOffset = 0;
                for (const auto& mesh : group)
                {
                    const auto& srcVertices = mesh->GetVertices();
                    const auto& srcIndices = mesh->GetIndices();

                    size_t oldSize = mergedVertices.size();
                    mergedVertices.resize(oldSize + srcVertices.size());
                    std::memcpy(mergedVertices.data() + oldSize, srcVertices.data(), srcVertices.size() * sizeof(Vertex));

                    oldSize = mergedIndices.size();
                    mergedIndices.resize(oldSize + srcIndices.size());
                    const uint32_t* srcIdxPtr = srcIndices.data();
                    uint32_t* destIdxPtr = mergedIndices.data() + oldSize;
                    for (size_t i = 0; i < srcIndices.size(); ++i)
                        destIdxPtr[i] = srcIdxPtr[i] + vertexOffset;

                    vertexOffset += static_cast<uint32_t>(srcVertices.size());
                }

                mergedMesh->SetMaterial(mat);
                mergedMesh->SetSkinned(false);
                return mergedMesh;
            };

            // Only use multi threading if there's 2 or more groups
            if (groups.size() < 2)
            {
                auto merged = mergeGroup();
                merged->Upload();
                newMeshes.push_back(merged);
            }
            else
                futures.push_back(std::async(std::launch::async, mergeGroup));
        }

        // Upload meshes on main thread when the merging is finished
        for (auto& future : futures)
        {
            auto merged = future.get();
            merged->Upload();
            newMeshes.push_back(merged);
        }

        m_meshes = std::move(newMeshes);

        return true;
    }

    void Model::SetMaterial(Material* material)
    {
        this->material = material;

        for (auto& mesh : m_meshes)
            mesh.get()->SetMaterial(material);
    }

    void Model::SetPosition(const Vector3& pos)
    {
        m_position = pos;
        MarkTransformDirty();
    }

    void Model::SetPosition(float x, float y, float z)
    {
        m_position.x = x;
        m_position.y = y;
        m_position.z = z;
        MarkTransformDirty();
    }

    void Model::SetRotation(const Vector3& rot)
    {
        m_rotationQuat = Quaternion::FromEuler(rot.y, rot.x, rot.z);
        MarkTransformDirty();
    }

    void Model::SetRotation(float x, float y, float z)
    {
        m_rotationQuat = Quaternion::FromEuler(y, x, z);
        MarkTransformDirty();
    }

    void Model::SetRotationQuat(const Quaternion& rot)
    {
        m_rotationQuat = rot;
        MarkTransformDirty();
    }

    Vector3 Model::GetRotation() const
    {
        return m_rotationQuat.ToEuler();
    }

    Quaternion Model::GetRotationQuat() const
    {
        return m_rotationQuat;
    }

    void Model::SetScale(const Vector3& scale)
    {
        m_scale = scale;
        MarkTransformDirty();
    }

    void Model::SetScale(float x, float y, float z)
    {
        m_scale.x = x;
        m_scale.y = y;
        m_scale.z = z;
        MarkTransformDirty();
    }

    void Model::SetScale(float uniformScale)
    {
        m_scale.x = uniformScale;
        m_scale.y = uniformScale;
        m_scale.z = uniformScale;
        MarkTransformDirty();
    }

    void Model::Translate(const Vector3& offset)
    {
        m_position.x += offset.x;
        m_position.y += offset.y;
        m_position.z += offset.z;
        MarkTransformDirty();
    }

    void Model::Translate(float x, float y, float z)
    {
        m_position.x += x;
        m_position.y += y;
        m_position.z += z;
        MarkTransformDirty();
    }

    void Model::Rotate(const Vector3& angles)
    {
        Quaternion delta = Quaternion::FromEuler(angles.y, angles.x, angles.z);
        m_rotationQuat = m_rotationQuat * delta;
        MarkTransformDirty();
    }

    void Model::Rotate(float x, float y, float z)
    {
        Quaternion delta = Quaternion::FromEuler(y, x, z);
        m_rotationQuat = m_rotationQuat * delta;
        MarkTransformDirty();
    }

    void Model::Scale(const Vector3& scale)
    {
        m_scale.x *= scale.x;
        m_scale.y *= scale.y;
        m_scale.z *= scale.z;
        MarkTransformDirty();
    }

    void Model::Scale(float uniformScale)
    {
        m_scale.x *= uniformScale;
        m_scale.y *= uniformScale;
        m_scale.z *= uniformScale;
        MarkTransformDirty();
    }

    const Matrix4& Model::GetTransformMatrix() const
    {
        return m_transformMatrix;
    }

    void Model::UpdateTransformMatrix()
    {
        if (!m_transformDirty)
            return;

        Matrix4 translation = Matrix4::Translate(m_position);
        Matrix4 rotation = m_rotationQuat.ToMatrix();
        Matrix4 scale = Matrix4::Scale(m_scale);
        m_transformMatrix = translation * rotation * scale;
        m_transformDirty = false;
    }

    void Model::Reset()
    {
        m_position = Vector3(0.0f, 0.0f, 0.0f);
        m_rotationQuat = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        m_scale = Vector3(1.0f, 1.0f, 1.0f);
        m_transformMatrix = Matrix4::Identity();
        m_transformDirty = false;
    }

    void Model::Destroy()
    {
        m_meshes.clear();
        m_animations.clear();
        m_skeleton = nullptr;
        m_nodeCount = 0;
    }

    void Model::SetSkeleton(Skeleton* skeleton)
    {
        m_skeleton = skeleton;
        m_animator.SetSkeleton(skeleton);
    }

    void Model::AddAnimation(AnimationClip* clip)
    {
        if (clip)
            m_animations.push_back(clip);
    }

    AnimationClip* Model::GetAnimation(size_t index) const
    {
        if (index >= m_animations.size())
            return nullptr;
        return m_animations[index];
    }

    AnimationClip* Model::GetAnimation(std::string_view name) const
    {
        for (auto* clip : m_animations)
        {
            if (clip->GetName() == name)
                return clip;
        }
        return nullptr;
    }

    bool Model::PlayAnimationByIndex(size_t index, bool loop)
    {
        AnimationClip* clip = GetAnimation(index);
        if (!clip)
        {
            std::cout << "[WARNING] Tried to play the animation at the index \"" << index << "\", but no animation at this index exists on this model.";
            return false;
        }

        m_animator.PlayAnimation(clip, loop);
        return true;
    }

    bool Model::PlayAnimationByName(std::string_view name, bool loop)
    {
        AnimationClip* clip = GetAnimation(name);
        if (!clip)
        {
            std::cout << "[WARNING] Tried to play the animation \"" << name.data() << "\", but no animation with this name exists on this model.";
            return false;
        }

        m_animator.PlayAnimation(clip, loop);
        return true;
    }

    bool Model::PlayAnimation(AnimationClip* clip, bool loop)
    {
        if (!clip)
        {
            std::cout << "[WARNING] Tried to play an animation, but the clip is null.";
            return false;
        }
        
        m_animator.PlayAnimation(clip, loop);
        return true;
    }

    void Model::StopAnimation()
    {
        m_animator.StopAnimation();
    }

    void Model::CrossfadeToAnimation(size_t index, float duration, bool loop)
    {
        AnimationClip* clip = GetAnimation(index);
        if (clip)
            m_animator.CrossfadeToAnimation(clip, duration, loop);
    }

    void Model::CrossfadeToAnimation(std::string_view name, float duration, bool loop)
    {
        AnimationClip* clip = GetAnimation(name);
        if (clip)
            m_animator.CrossfadeToAnimation(clip, duration, loop);
    }

    void Model::UpdateAnimation(float deltaTime)
    {
        m_animator.Update(deltaTime, m_meshes);
    }

    void Model::SetSkinned(bool skinned)
    {
        for (auto mesh : m_meshes)
            mesh.get()->SetSkinned(skinned);
    }
}