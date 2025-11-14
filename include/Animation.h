#pragma once
#include "Maths.h"
#include <vector>
#include <string>
#include <unordered_map>
#include "Mesh.h"

namespace cl
{
    struct Bone
    {
        std::string name;
        int parentIndex;
        Matrix4 inverseBindMatrix;
        Matrix4 localTransform;

        Bone() : parentIndex(-1), inverseBindMatrix(Matrix4::Identity()), localTransform(Matrix4::Identity()) {}
    };

    struct Skeleton
    {
        std::vector<Bone> bones;
        std::unordered_map<std::string, int> boneMap;
        std::vector<Matrix4> finalMatrices;

        int FindBoneIndex(std::string_view name) const
        {
            auto it = boneMap.find(name.data());
            if (it != boneMap.end())
                return it->second;

            return -1;
        }

        void ComputeBoneMatrix(int index, const Matrix4& parentTransform)
        {
            Bone& bone = bones[index];

            Matrix4 globalTransform = parentTransform * bone.localTransform;

            finalMatrices[index] = globalTransform * bone.inverseBindMatrix;

            for (size_t i = 0; i < bones.size(); ++i)
            {
                if (bones[i].parentIndex == index)
                    ComputeBoneMatrix((int)i, globalTransform);
            }
        }

        void UpdateFinalMatrices()
        {
            if (bones.empty())
                return;

            finalMatrices.resize(bones.size());

            //for (size_t i = 0; i < bones.size(); ++i)
            //    ComputeBoneMatrix((int)i, Matrix4::Identity());
            for (size_t i = 0; i < bones.size(); ++i)
            {
                if (bones[i].parentIndex == -1)
                    ComputeBoneMatrix((int)i, Matrix4::Identity());
            }
        }
    };

    enum class AnimationInterpolation
    {
        Linear,
        Step,
        CubicSpline
    };

    struct AnimationKeyframe
    {
        float time;
        Vector3 translation;
        Quaternion rotation;
        Vector3 scale;

        AnimationKeyframe() : time(0.0f), translation(0.0f, 0.0f, 0.0f), rotation(0.0f, 0.0f, 0.0f, 1.0f), scale(1.0f, 1.0f, 1.0f) {}
    };

    struct NodeAnimationChannel
    {
        int targetNodeIndex;
        std::vector<float> times;
        std::vector<Vector3> translations;
        std::vector<Quaternion> rotations;
        std::vector<Vector3> scales;
        AnimationInterpolation interpolation;

        // Cubic spline interpolation
        std::vector<Vector3> inTangents;
        std::vector<Vector3> outTangents;
        std::vector<Vector3> inTangentsScale;
        std::vector<Vector3> outTangentsScale;
        std::vector<Quaternion> inTangentsQuat;
        std::vector<Quaternion> outTangentsQuat;

        NodeAnimationChannel() : targetNodeIndex(-1), interpolation(AnimationInterpolation::Linear) {}
    };

    struct AnimationChannel
    {
        int targetBoneIndex;
        std::vector<float> times;
        std::vector<Vector3> translations;
        std::vector<Quaternion> rotations;
        std::vector<Vector3> scales;
        AnimationInterpolation interpolation;

        // Cubic spline iterpolation
        std::vector<Vector3> inTangents;
        std::vector<Vector3> outTangents;
        std::vector<Vector3> inTangentsScale;
        std::vector<Vector3> outTangentsScale;
        std::vector<Quaternion> inTangentsQuat;
        std::vector<Quaternion> outTangentsQuat;

        AnimationChannel() : targetBoneIndex(-1), interpolation(AnimationInterpolation::Linear) {}
    };

    struct MorphWeightChannel
    {
        int targetNodeIndex;
        std::vector<float> times;
        std::vector<std::vector<float>> weights; // [keyframe][target_index]
        AnimationInterpolation interpolation;

        MorphWeightChannel() : targetNodeIndex(-1), interpolation(AnimationInterpolation::Linear) {}
    };


    enum class AnimationType
    {
        Skeletal,
        NodeBased
    };

    class AnimationClip
    {
    public:
        static std::vector<AnimationClip*> s_clips;

        AnimationClip();
        ~AnimationClip();
        AnimationClip(const AnimationClip&) = delete;
        AnimationClip& operator=(const AnimationClip&) = delete;
        AnimationClip(AnimationClip&& other) noexcept;
        AnimationClip& operator=(AnimationClip&& other) noexcept;

        void SetName(std::string_view name) { m_name = name; }
        const std::string& GetName() const { return m_name; }

        void SetDuration(float duration) { m_duration = duration; }
        float GetDuration() const { return m_duration; }

        void AddChannel(const AnimationChannel& channel) { m_channels.push_back(channel); }
        const std::vector<AnimationChannel>& GetChannels() const { return m_channels; }

        void AddNodeChannel(const NodeAnimationChannel& channel) { m_nodeChannels.push_back(channel); }
        const std::vector<NodeAnimationChannel>& GetNodeChannels() const { return m_nodeChannels; }

        void SetAnimationType(AnimationType type) { m_animationType = type; }
        AnimationType GetAnimationType() const { return m_animationType; }

        void AddMorphWeightChannel(const MorphWeightChannel& channel) { m_morphWeightChannels.push_back(channel); }
        const std::vector<MorphWeightChannel>& GetMorphWeightChannels() const { return m_morphWeightChannels; }

        void Destroy();

    private:
        std::string m_name;
        float m_duration;
        std::vector<AnimationChannel> m_channels;
        std::vector<NodeAnimationChannel> m_nodeChannels;
        std::vector<MorphWeightChannel> m_morphWeightChannels;
        AnimationType m_animationType = AnimationType::Skeletal;
    };

    class Animator
    {
    public:
        Animator();
        ~Animator();

        void SetSkeleton(Skeleton* skeleton);
        Skeleton* GetSkeleton() const { return m_skeleton; }

        void PlayAnimation(AnimationClip* clip, bool loop = true);
        void StopAnimation();
        void PauseAnimation();
        void ResumeAnimation();

        void Update(float deltaTime, std::vector<std::shared_ptr<Mesh>>& meshes);

        void SetSpeed(float speed) { m_speed = speed; }
        float GetSpeed() const { return m_speed; }

        void SetTime(float time);
        float GetTime() const { return m_currentTime; }

        bool IsPlaying() const { return m_playing && !m_paused; }
        bool IsPaused() const { return m_paused; }

        void SetLooping(bool loop) { m_loop = loop; }
        bool IsLooping() const { return m_loop; }

        const std::vector<Matrix4>& GetBoneMatrices() const { return m_boneMatrices; }
        const std::vector<Matrix4>& GetFinalBoneMatrices() const
        {
            if (!m_skeleton)
                return {};

            return m_skeleton->finalMatrices;
        }

        AnimationClip* GetCurrentClip() const { return m_currentClip; }

        const std::vector<Matrix4>& GetNodeTransforms() const { return m_nodeTransforms; }
        Matrix4 GetNodeTransform(int nodeIndex) const;
    private:
        Skeleton* m_skeleton;
        AnimationClip* m_currentClip;
        std::vector<Matrix4> m_boneMatrices;
        std::vector<Matrix4> m_localTransforms;

        std::vector<Matrix4> m_nodeTransforms;
        std::unordered_map<int, Matrix4> m_animatedNodeTransforms;

        float m_currentTime;
        float m_speed;
        bool m_playing;
        bool m_paused;
        bool m_loop;

        void CalculateBoneTransforms();
        void SampleAnimation(float time);
        void SampleNodeAnimation(float time);
        void SampleMorphWeights(float time, std::vector<std::shared_ptr<Mesh>>& meshes);
        Vector3 InterpolateTranslation(const AnimationChannel& channel, float time) const;
        Quaternion InterpolateRotation(const AnimationChannel& channel, float time) const;
        Vector3 InterpolateScale(const AnimationChannel& channel, float time) const;

        Vector3 InterpolateNodeTranslation(const NodeAnimationChannel& channel, float time) const;
        Quaternion InterpolateNodeRotation(const NodeAnimationChannel& channel, float time) const;
        Vector3 InterpolateNodeScale(const NodeAnimationChannel& channel, float time) const;

        int FindKeyframeIndex(const std::vector<float>& times, float time) const;
    };
}