#include "Animation.h"
#include <algorithm>
#include <iostream>
#include <functional>

namespace cl
{
    std::vector<AnimationClip*> AnimationClip::s_clips;

    AnimationClip::AnimationClip()
        : m_duration(0.0f)
    {
        s_clips.push_back(this);
    }

    AnimationClip::~AnimationClip()
    {
        Destroy();

        for (size_t i = 0; i < s_clips.size(); ++i)
        {
            if (s_clips[i] == this)
            {
                if (i != s_clips.size() - 1)
                    std::swap(s_clips[i], s_clips.back());

                s_clips.pop_back();
                return;
            }
        }
    }

    AnimationClip::AnimationClip(AnimationClip&& other) noexcept
        : m_name(std::move(other.m_name))
        , m_duration(other.m_duration)
        , m_channels(std::move(other.m_channels))
        , m_nodeChannels(std::move(other.m_nodeChannels))
        , m_morphWeightChannels(std::move(other.m_morphWeightChannels))
    {
        other.m_duration = 0.0f;
        s_clips.push_back(this);
    }

    AnimationClip& AnimationClip::operator=(AnimationClip&& other) noexcept
    {
        if (this != &other)
        {
            Destroy();
            m_name = std::move(other.m_name);
            m_duration = other.m_duration;
            m_channels = std::move(other.m_channels);
            m_nodeChannels = std::move(other.m_nodeChannels);
            m_morphWeightChannels = std::move(other.m_morphWeightChannels);
            other.m_duration = 0.0f;
        }
        return *this;
    }

    void AnimationClip::Destroy()
    {
        m_channels.clear();
        m_nodeChannels.clear();
        m_morphWeightChannels.clear();
        m_duration = 0.0f;
    }

    Animator::Animator()
        : m_skeleton(nullptr)
        , m_currentClip(nullptr)
        , m_currentTime(0.0f)
        , m_speed(1.0f)
        , m_playing(false)
        , m_paused(false)
        , m_loop(true)
    {
    }

    Animator::~Animator()
    {
    }

    void Animator::SetSkeleton(Skeleton* skeleton)
    {
        m_skeleton = skeleton;
        if (m_skeleton)
        {
            m_localTransforms.resize(m_skeleton->bones.size());
            m_boneMatrices.resize(m_skeleton->bones.size());

            for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
                m_localTransforms[i] = m_skeleton->bones[i].localTransform;

            CalculateBoneTransforms();
        }
    }

    void Animator::PlayAnimation(AnimationClip* clip, bool loop)
    {
        if (!clip)
            return;

        m_currentClip = clip;
        m_currentTime = 0.0f;
        m_playing = true;
        m_paused = false;
        m_loop = loop;

        if (clip->GetAnimationType() == AnimationType::Skeletal)
        {
            if (!m_skeleton)
            {
                std::cout << "[WARNING] Failed to play the animation \"" << clip->GetName() << "\". No skeleton assigned.\n";
                m_playing = false;
                return;
            }

            m_localTransforms.resize(m_skeleton->bones.size());
            m_boneMatrices.resize(m_skeleton->bones.size());

            for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
                m_localTransforms[i] = m_skeleton->bones[i].localTransform;

            SampleAnimation(m_currentTime);

            CalculateBoneTransforms();
        }
        else
        {
            m_animatedNodeTransforms.clear();
            m_nodeTransforms.clear();
            const auto& nodeChannels = clip->GetNodeChannels();
            int maxNodeIndex = -1;
            for (const auto& channel : nodeChannels)
            {
                if (channel.targetNodeIndex > maxNodeIndex)
                    maxNodeIndex = channel.targetNodeIndex;
            }
            if (maxNodeIndex >= 0)
                m_nodeTransforms.resize(maxNodeIndex + 1, Matrix4::Identity());
        }
    }

    void Animator::StopAnimation()
    {
        m_playing = false;
        m_paused = false;
        m_currentTime = 0.0f;
    }

    void Animator::PauseAnimation()
    {
        m_paused = true;
    }

    void Animator::ResumeAnimation()
    {
        m_paused = false;
    }

    void Animator::Update(float deltaTime, std::vector<std::shared_ptr<Mesh>>& meshes)
    {
        if (!m_playing || m_paused || !m_currentClip)
            return;

        float duration = m_currentClip->GetDuration();
        if (duration <= 0.0f)
            return;

        m_currentTime += deltaTime * m_speed;

        if (m_currentTime > duration)
        {
            if (m_loop)
            {
                while (m_currentTime > duration)
                    m_currentTime -= duration;
            }
            else
            {
                m_currentTime = duration;
                m_playing = false;
            }
        }

        if (m_currentClip->GetAnimationType() == AnimationType::Skeletal)
        {
            if (m_skeleton)
            {
                SampleAnimation(m_currentTime);
                CalculateBoneTransforms();
                m_boneMatrices = m_skeleton->finalMatrices;
            }
        }
        else
            SampleNodeAnimation(m_currentTime);

        SampleMorphWeights(m_currentTime, meshes);
    }

    void Animator::SetTime(float time)
    {
        if (!m_currentClip)
            return;

        m_currentTime = std::max(0.0f, std::min(time, m_currentClip->GetDuration()));

        if (m_skeleton)
        {
            SampleAnimation(m_currentTime);
            CalculateBoneTransforms();
            m_boneMatrices = m_skeleton->finalMatrices;
        }
    }


    void Animator::SampleAnimation(float time)
    {
        if (!m_currentClip || !m_skeleton)
            return;

        // Reset to bind pose each sample to avoid accumulation errors
        for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
            m_localTransforms[i] = m_skeleton->bones[i].localTransform;

        for (const auto& channel : m_currentClip->GetChannels())
        {
            int boneIndex = channel.targetBoneIndex;
            if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeleton->bones.size()))
                continue;

            Vector3 translation = m_localTransforms[boneIndex].GetTranslation();
            Quaternion rotation = m_localTransforms[boneIndex].GetRotation();
            Vector3 scale = m_localTransforms[boneIndex].GetScale();

            if (!channel.translations.empty())
                translation = InterpolateTranslation(channel, time);

            if (!channel.rotations.empty())
                rotation = InterpolateRotation(channel, time);

            if (!channel.scales.empty())
                scale = InterpolateScale(channel, time);

            Matrix4 t = Matrix4::Translate(translation);
            Matrix4 r = Matrix4::FromQuaternion(rotation);
            Matrix4 s = Matrix4::Scale(scale);
            m_localTransforms[boneIndex] = t * r * s;
        }
    }

    void Animator::SampleNodeAnimation(float time)
    {
        if (!m_currentClip)
            return;

        const auto& nodeChannels = m_currentClip->GetNodeChannels();

        if (nodeChannels.empty())
        {
            m_animatedNodeTransforms.clear();
            return;
        }

        m_animatedNodeTransforms.clear();

        for (const auto& channel : nodeChannels)
        {
            if (channel.targetNodeIndex < 0)
                continue;

            Vector3 translation = InterpolateNodeTranslation(channel, time);
            Quaternion rotation = InterpolateNodeRotation(channel, time);
            Vector3 scale = InterpolateNodeScale(channel, time);

            // Build the transform matrix
            Matrix4 t = Matrix4::Translate(translation);
            Matrix4 r = Matrix4::FromQuaternion(rotation);
            Matrix4 s = Matrix4::Scale(scale);

            Matrix4 localTransform = t * r * s;

            // Store the transform for this node
            m_animatedNodeTransforms[channel.targetNodeIndex] = localTransform;

            // Update the node transforms vector
            if (channel.targetNodeIndex < static_cast<int>(m_nodeTransforms.size()))
                m_nodeTransforms[channel.targetNodeIndex] = localTransform;
        }
    }

    void Animator::SampleMorphWeights(float time, std::vector<std::shared_ptr<Mesh>>& meshes)
    {
        if (!m_currentClip)
            return;

        const auto& morphChannels = m_currentClip->GetMorphWeightChannels();
        if (morphChannels.empty())
            return;

        for (const auto& channel : morphChannels)
        {
            if (channel.weights.empty() || channel.times.empty())
                continue;

            int index = FindKeyframeIndex(channel.times, time);
            int nextIndex = index + 1;

            float factor = 0.0f;
            if (nextIndex < static_cast<int>(channel.times.size()))
            {
                float t0 = channel.times[index];
                float t1 = channel.times[nextIndex];
                factor = (time - t0) / (t1 - t0);
            }

            // Determine interpolated weights for this keyframe
            std::vector<float> interpolatedWeights(channel.weights[index].size());

            for (size_t w = 0; w < interpolatedWeights.size(); ++w)
            {
                float w0 = channel.weights[index][w];
                float w1 = (nextIndex < static_cast<int>(channel.weights.size())) ? channel.weights[nextIndex][w] : w0;

                if (channel.interpolation == AnimationInterpolation::Step)
                    interpolatedWeights[w] = w0;
                else
                    interpolatedWeights[w] = w0 + (w1 - w0) * factor;
            }

            // Apply to the target mesh
            if (channel.targetNodeIndex >= 0 && channel.targetNodeIndex < static_cast<int>(m_nodeTransforms.size()))
            {
                if (meshes.size() < channel.targetNodeIndex)
                {
                    std::cout << "[WARNING] Failed to apply morph weight during animation." << std::endl;
                    continue;
                }

                meshes[channel.targetNodeIndex]->SetMorphWeights(interpolatedWeights);
            }
        }
    }

    Matrix4 Animator::GetNodeTransform(int nodeIndex) const
    {
        // Check if this node has an animated transform
        auto it = m_animatedNodeTransforms.find(nodeIndex);
        if (it != m_animatedNodeTransforms.end())
            return it->second;

        // Check the vector
        if (nodeIndex >= 0 && nodeIndex < static_cast<int>(m_nodeTransforms.size()))
            return m_nodeTransforms[nodeIndex];

        return Matrix4::Identity();
    }

    Vector3 Animator::InterpolateNodeTranslation(const NodeAnimationChannel& channel, float time) const
    {
        if (channel.translations.empty())
            return Vector3(0.0f, 0.0f, 0.0f);

        if (channel.translations.size() == 1 || channel.times.empty())
            return channel.translations[0];

        int index = FindKeyframeIndex(channel.times, time);

        if (index < 0)
            return channel.translations[0];

        if (index >= static_cast<int>(channel.translations.size()) - 1)
            return channel.translations.back();

        if (channel.interpolation == AnimationInterpolation::Step)
            return channel.translations[index];

        float t0 = channel.times[index];
        float t1 = channel.times[index + 1];
        float factor = (time - t0) / (t1 - t0);

        const Vector3& v0 = channel.translations[index];
        const Vector3& v1 = channel.translations[index + 1];

        return Vector3(
            v0.x + (v1.x - v0.x) * factor,
            v0.y + (v1.y - v0.y) * factor,
            v0.z + (v1.z - v0.z) * factor
        );
    }

    Quaternion Animator::InterpolateNodeRotation(const NodeAnimationChannel& channel, float time) const
    {
        if (channel.rotations.empty())
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);

        if (channel.rotations.size() == 1 || channel.times.empty())
            return channel.rotations[0];

        int index = FindKeyframeIndex(channel.times, time);

        if (index < 0)
            return channel.rotations[0];

        if (index >= static_cast<int>(channel.rotations.size()) - 1)
            return channel.rotations.back();

        if (channel.interpolation == AnimationInterpolation::Step)
            return channel.rotations[index];

        float t0 = channel.times[index];
        float t1 = channel.times[index + 1];
        float factor = (time - t0) / (t1 - t0);

        return Quaternion::Slerp(channel.rotations[index], channel.rotations[index + 1], factor);
    }

    Vector3 Animator::InterpolateNodeScale(const NodeAnimationChannel& channel, float time) const
    {
        if (channel.scales.empty())
            return Vector3(1.0f, 1.0f, 1.0f);

        if (channel.scales.size() == 1 || channel.times.empty())
            return channel.scales[0];

        int index = FindKeyframeIndex(channel.times, time);

        if (index < 0)
            return channel.scales[0];

        if (index >= static_cast<int>(channel.scales.size()) - 1)
            return channel.scales.back();

        if (channel.interpolation == AnimationInterpolation::Step)
            return channel.scales[index];

        float t0 = channel.times[index];
        float t1 = channel.times[index + 1];
        float factor = (time - t0) / (t1 - t0);

        const Vector3& v0 = channel.scales[index];
        const Vector3& v1 = channel.scales[index + 1];

        return Vector3(
            v0.x + (v1.x - v0.x) * factor,
            v0.y + (v1.y - v0.y) * factor,
            v0.z + (v1.z - v0.z) * factor
        );
    }


    void Animator::CalculateBoneTransforms()
    {
        if (!m_skeleton)
            return;

        m_boneMatrices.resize(m_skeleton->bones.size());

        // Todo: This is slow
        // Recursive computation to ensure topological order
        std::function<void(int, const Matrix4&)> computeBoneMatrix = [&](int index, const Matrix4& parentTransform)
        {
            const Bone& bone = m_skeleton->bones[index];

            Matrix4 globalTransform = parentTransform * m_localTransforms[index];
            m_boneMatrices[index] = globalTransform * bone.inverseBindMatrix;

            for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
            {
                if (m_skeleton->bones[i].parentIndex == index)
                    computeBoneMatrix(static_cast<int>(i), globalTransform);
            }
        };

        for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
        {
            if (m_skeleton->bones[i].parentIndex == -1)
                computeBoneMatrix(static_cast<int>(i), Matrix4::Identity());
        }

        if (m_skeleton)
            m_skeleton->finalMatrices = m_boneMatrices;
    }

    Vector3 Animator::InterpolateTranslation(const AnimationChannel& channel, float time) const
    {
        if (channel.translations.empty())
            return Vector3(0.0f, 0.0f, 0.0f);

        if (channel.translations.size() == 1 || channel.times.empty())
            return channel.translations[0];

        int index = FindKeyframeIndex(channel.times, time);

        if (index < 0)
            return channel.translations[0];

        if (index >= static_cast<int>(channel.translations.size()) - 1)
            return channel.translations.back();

        if (channel.interpolation == AnimationInterpolation::Step)
            return channel.translations[index];

        float t0 = channel.times[index];
        float t1 = channel.times[index + 1];
        float dt = t1 - t0;
        float s = (time - t0) / dt;
        const Vector3& p0 = channel.translations[index];
        const Vector3& p1 = channel.translations[index + 1];

        if (channel.interpolation == AnimationInterpolation::CubicSpline)
        {
            if (channel.outTangents.empty() || channel.inTangents.empty())
                return p0 + (p1 - p0) * s;

            const Vector3& a0 = channel.outTangents[index];
            const Vector3& b1 = channel.inTangents[index + 1];

            float s2 = s * s;
            float s3 = s2 * s;
            float h00 = 2 * s3 - 3 * s2 + 1;
            float h10 = s3 - 2 * s2 + s;
            float h01 = -2 * s3 + 3 * s2;
            float h11 = s3 - s2;

            return p0 * h00 + (a0 * dt) * h10 + p1 * h01 + (b1 * dt) * h11;
        }
        else
            return p0 + (p1 - p0) * s;
    }

    Quaternion Animator::InterpolateRotation(const AnimationChannel& channel, float time) const
    {
        if (channel.rotations.empty())
            return Quaternion(0.0f, 0.0f, 0.0f, 1.0f);

        if (channel.rotations.size() == 1 || channel.times.empty())
            return channel.rotations[0];

        int index = FindKeyframeIndex(channel.times, time);
        if (index < 0)
            return channel.rotations[0];

        if (index >= static_cast<int>(channel.rotations.size()) - 1)
            return channel.rotations.back();

        if (channel.interpolation == AnimationInterpolation::Step)
            return channel.rotations[index];

        float t0 = channel.times[index];
        float t1 = channel.times[index + 1];
        float dt = t1 - t0;
        float s = (time - t0) / dt;
        const Quaternion& q0 = channel.rotations[index];
        const Quaternion& q1 = channel.rotations[index + 1];

        if (channel.interpolation == AnimationInterpolation::CubicSpline)
        {
            if (channel.outTangentsQuat.empty() || channel.inTangentsQuat.empty())
                return Quaternion::Slerp(q0, q1, s);

            const Quaternion& a0 = channel.outTangentsQuat[index];
            const Quaternion& b1 = channel.inTangentsQuat[index + 1];

            float s2 = s * s;
            float s3 = s2 * s;
            float h00 = 2 * s3 - 3 * s2 + 1;
            float h10 = s3 - 2 * s2 + s;
            float h01 = -2 * s3 + 3 * s2;
            float h11 = s3 - s2;

            // Interpolate as 4D vector
            Quaternion res = q0 * h00 + (a0 * (h10 * dt)) + q1 * h01 + (b1 * (h11 * dt));
            return res.Normalize();
        }
        else
            return Quaternion::Slerp(q0, q1, s);
    }

    Vector3 Animator::InterpolateScale(const AnimationChannel& channel, float time) const
    {
        if (channel.scales.empty())
            return Vector3(1.0f, 1.0f, 1.0f);

        if (channel.scales.size() == 1 || channel.times.empty())
            return channel.scales[0];

        int index = FindKeyframeIndex(channel.times, time);
        if (index < 0)
            return channel.scales[0];

        if (index >= static_cast<int>(channel.scales.size()) - 1)
            return channel.scales.back();

        if (channel.interpolation == AnimationInterpolation::Step)
            return channel.scales[index];

        float t0 = channel.times[index];
        float t1 = channel.times[index + 1];
        float dt = t1 - t0;
        float s = (time - t0) / dt;
        const Vector3& p0 = channel.scales[index];
        const Vector3& p1 = channel.scales[index + 1];

        if (channel.interpolation == AnimationInterpolation::CubicSpline)
        {
            if (channel.outTangentsScale.empty() || channel.inTangentsScale.empty())
                return p0 + (p1 - p0) * s;

            const Vector3& a0 = channel.outTangentsScale[index];
            const Vector3& b1 = channel.inTangentsScale[index + 1];

            float s2 = s * s;
            float s3 = s2 * s;
            float h00 = 2 * s3 - 3 * s2 + 1;
            float h10 = s3 - 2 * s2 + s;
            float h01 = -2 * s3 + 3 * s2;
            float h11 = s3 - s2;

            return p0 * h00 + (a0 * dt) * h10 + p1 * h01 + (b1 * dt) * h11;
        }
        else
            return p0 + (p1 - p0) * s;
    }

    int Animator::FindKeyframeIndex(const std::vector<float>& times, float time) const
    {
        if (times.empty())
            return -1;

        for (size_t i = 0; i < times.size() - 1; ++i)
        {
            if (time >= times[i] && time < times[i + 1])
                return static_cast<int>(i);
        }

        if (time >= times.back())
            return static_cast<int>(times.size()) - 1;

        return 0;
    }
}