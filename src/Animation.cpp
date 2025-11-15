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
        , m_blendTreeRoot(nullptr)
        , m_blendParameter(0.0f)
        , m_blendParameterY(0.0f)
        , m_additiveRefClip(nullptr)
        , m_nextLayerId(0)
    {
        CreateLayer("Base", 0);
    }

    Animator::~Animator()
    {
        delete m_blendTreeRoot;
    }

    void Animator::SetSkeleton(Skeleton* skeleton)
    {
        m_skeleton = skeleton;

        if (!m_skeleton)
            return;

        m_localTransforms.resize(m_skeleton->bones.size());
        m_boneMatrices.resize(m_skeleton->bones.size());

        for (auto& bone : m_skeleton->bones)
            bone.children.clear();

        for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
        {
            m_localTransforms[i] = m_skeleton->bones[i].localTransform;

            int parent = m_skeleton->bones[i].parentIndex;
            if (parent >= 0)
                m_skeleton->bones[parent].children.push_back((int)i);
        }

        CalculateBoneTransforms();

        m_skeleton->finalMatrices = m_boneMatrices;
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
                std::cout << "[WARNING] Failed to play the animation \"" << clip->GetName() << "\". No skeleton assigned." << std::endl;
                m_playing = false;
                return;
            }

            m_localTransforms.resize(m_skeleton->bones.size());
            m_boneMatrices.resize(m_skeleton->bones.size());

            for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
                m_localTransforms[i] = m_skeleton->bones[i].localTransform;

            SampleAnimation(m_currentTime);
            CalculateBoneTransforms();

            m_skeleton->finalMatrices = m_boneMatrices;
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
        if (!m_playing || m_paused)
            return;

        UpdateCrossfade(deltaTime);

        if (m_blendTreeRoot)
            UpdateBlendTree(deltaTime);
        else if (!m_layers.empty())
            UpdateLayers(deltaTime, meshes);
        else if (m_currentClip)
        {
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
                }
            }
            else
                SampleNodeAnimation(m_currentTime);

            SampleMorphWeights(m_currentTime, meshes);
        }
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

            m_skeleton->finalMatrices = m_boneMatrices;
        }
    }

    int Animator::GetLayerIndex(int layerId) const
    {
        auto it = m_layerIdToIndex.find(layerId);
        return (it != m_layerIdToIndex.end()) ? static_cast<int>(it->second) : -1;
    }

    int Animator::CreateLayer(const std::string& name, int priority)
    {
        AnimationLayer layer;
        layer.id = m_nextLayerId++;
        layer.priority = priority;

        m_layers.push_back(layer);

        // Sort by priority
        std::sort(m_layers.begin(), m_layers.end(), [](const AnimationLayer& a, const AnimationLayer& b)
            { return a.priority < b.priority; });

        // Rebuild mappings
        m_layerIdToIndex.clear();
        for (size_t i = 0; i < m_layers.size(); ++i)
            m_layerIdToIndex[m_layers[i].id] = i;

        m_layerNames[name] = layer.id;

        return layer.id;
    }

    void Animator::RemoveLayer(int layerId)
    {
        auto it = m_layerIdToIndex.find(layerId);
        if (it == m_layerIdToIndex.end())
            return;

        size_t index = it->second;
        m_layers.erase(m_layers.begin() + index);
        m_boneMasks.erase(layerId);

        // Rebuild mappings
        m_layerIdToIndex.clear();
        for (size_t i = 0; i < m_layers.size(); ++i)
            m_layerIdToIndex[m_layers[i].id] = i;
    }

    void Animator::SetLayerWeight(int layerId, float weight)
    {
        int index = GetLayerIndex(layerId);
        if (index < 0)
            return;

        m_layers[index].weight = std::max(0.0f, std::min(1.0f, weight));
    }

    float Animator::GetLayerWeight(int layerId) const
    {
        int index = GetLayerIndex(layerId);
        if (index < 0)
            return 0.0f;

        return m_layers[index].weight;
    }

    void Animator::SetLayerBlendMode(int layerId, BlendMode mode)
    {
        int index = GetLayerIndex(layerId);
        if (index < 0)
            return;

        m_layers[index].blendMode = mode;
    }

    bool Animator::PlayAnimationOnLayer(int layerId, AnimationClip* clip, bool loop)
    {
        if (!clip)
            return false;

        int index = GetLayerIndex(layerId);
        if (index < 0)
            return false;

        m_layers[index].clip = clip;
        m_layers[index].currentTime = 0.0f;
        m_layers[index].loop = loop;
        m_layers[index].active = true;
        m_playing = true;

        return true;
    }

    void Animator::StopLayer(int layerId)
    {
        int index = GetLayerIndex(layerId);
        if (index < 0)
            return;

        m_layers[index].active = false;
    }

    void Animator::CrossfadeToAnimation(AnimationClip* clip, float duration, bool loop, int layerIndex)
    {
        if (!clip || layerIndex < 0 || layerIndex >= static_cast<int>(m_layers.size()))
            return;

        m_crossfade.fromClip = m_layers[layerIndex].clip;
        m_crossfade.toClip = clip;
        m_crossfade.duration = duration;
        m_crossfade.elapsed = 0.0f;
        m_crossfade.active = true;
        m_crossfade.targetLayer = layerIndex;

        m_layers[layerIndex].loop = loop;
    }

    float Animator::GetCrossfadeProgress() const
    {
        if (!m_crossfade.active || m_crossfade.duration <= 0.0f)
            return 0.0f;
        return m_crossfade.elapsed / m_crossfade.duration;
    }

    void Animator::SetBoneMask(const std::vector<int>& boneIndices, int layerIndex)
    {
        m_boneMasks[layerIndex] = boneIndices;
    }

    void Animator::ClearBoneMask(int layerIndex)
    {
        m_boneMasks.erase(layerIndex);
    }

    const std::vector<int>& Animator::GetBoneMask(int layerIndex) const
    {
        static std::vector<int> empty;
        auto it = m_boneMasks.find(layerIndex);
        return (it != m_boneMasks.end()) ? it->second : empty;
    }

    void Animator::SyncLayerToLayer(int sourceLayerId, int targetLayerId)
    {
        int srcIdx = GetLayerIndex(sourceLayerId);
        int tgtIdx = GetLayerIndex(targetLayerId);

        if (srcIdx < 0 || tgtIdx < 0)
            return;

        m_layers[tgtIdx].currentTime = m_layers[srcIdx].currentTime;
    }

    void Animator::SetLayerTimeScale(int layerIndex, float scale)
    {
        if (layerIndex < 0 || layerIndex >= static_cast<int>(m_layers.size()))
            return;

        m_layers[layerIndex].timeScale = scale;
    }

    void Animator::UpdateLayers(float deltaTime, std::vector<std::shared_ptr<Mesh>>& meshes)
    {
        if (!m_skeleton || m_layers.empty())
            return;

        std::vector<Matrix4> finalTransforms(m_skeleton->bones.size());
        for (size_t i = 0; i < finalTransforms.size(); ++i)
            finalTransforms[i] = m_skeleton->bones[i].localTransform;

        bool firstLayer = true;

        for (size_t layerIdx = 0; layerIdx < m_layers.size(); ++layerIdx)
        {
            auto& layer = m_layers[layerIdx];

            if (!layer.active || !layer.clip || layer.weight <= 0.0f)
                continue;

            float duration = layer.clip->GetDuration();
            if (duration <= 0.0f)
                continue;

            layer.currentTime += deltaTime * m_speed * layer.timeScale;
            if (layer.currentTime > duration)
            {
                if (layer.loop)
                {
                    while (layer.currentTime > duration)
                        layer.currentTime -= duration;
                }
                else
                {
                    layer.currentTime = duration;
                    layer.active = false;
                }
            }

            std::vector<Matrix4> layerTransforms(m_skeleton->bones.size());
            SampleAnimationToBuffer(layer.clip, layer.currentTime, layerTransforms);

            if (firstLayer && layer.blendMode == BlendMode::Override)
            {
                finalTransforms = layerTransforms;
                firstLayer = false;
            }
            else
            {
                switch (layer.blendMode)
                {
                    case BlendMode::Override:
                    case BlendMode::Blend:
                        BlendBoneTransforms(finalTransforms, layerTransforms, layer.weight, finalTransforms, layer.id);
                        break;
                    case BlendMode::Additive:
                        ApplyAdditiveAnimation(layerTransforms, finalTransforms);
                        break;
                }
            }
        }

        m_localTransforms = finalTransforms;
        CalculateBoneTransforms();
        SampleMorphWeights(m_layers[0].currentTime, meshes);
    }

    void Animator::UpdateCrossfade(float deltaTime)
    {
        if (!m_crossfade.active)
            return;

        m_crossfade.elapsed += deltaTime;
        float t = std::min(1.0f, m_crossfade.elapsed / m_crossfade.duration);

        if (t >= 1.0f)
        {
            PlayAnimationOnLayer(m_crossfade.targetLayer, m_crossfade.toClip, m_layers[m_crossfade.targetLayer].loop);
            m_crossfade.active = false;
            return;
        }

        if (!m_skeleton || !m_crossfade.fromClip || !m_crossfade.toClip)
            return;

        std::vector<Matrix4> fromTransforms(m_skeleton->bones.size());
        std::vector<Matrix4> toTransforms(m_skeleton->bones.size());

        SampleAnimationToBuffer(m_crossfade.fromClip, m_layers[m_crossfade.targetLayer].currentTime, fromTransforms);
        SampleAnimationToBuffer(m_crossfade.toClip, m_layers[m_crossfade.targetLayer].currentTime, toTransforms);

        BlendBoneTransforms(fromTransforms, toTransforms, t, m_localTransforms);
        CalculateBoneTransforms();
    }

    void Animator::UpdateBlendTree(float deltaTime)
    {
        if (!m_blendTreeRoot || !m_skeleton)
            return;

        EvaluateBlendTree(m_blendTreeRoot, deltaTime, m_localTransforms);
        CalculateBoneTransforms();
    }

    void Animator::BlendBoneTransforms(const std::vector<Matrix4>& from, const std::vector<Matrix4>& to, float weight, std::vector<Matrix4>& result, int layerId)
    {
        if (result.size() != from.size())
            result.resize(from.size());

        // Get bone mask for this layer
        const auto& mask = GetBoneMask(layerId);
        bool hasMask = !mask.empty();

        // Todo: This is O(n²). This needs to be optimized
        for (size_t i = 0; i < from.size(); ++i)
        {
            // Check if a mask is being used
            if (hasMask)
            {
                bool isMasked = std::find(mask.begin(), mask.end(), static_cast<int>(i)) != mask.end();
                if (!isMasked)
                {
                    // Bone is not in the mask, so it won't be blended
                    result[i] = from[i];
                    continue;
                }
            }

            // Blend this bone
            result[i] = BlendMatrices(from[i], to[i], weight);
        }
    }

    void Animator::ApplyAdditiveAnimation(const std::vector<Matrix4>& additive, std::vector<Matrix4>& result)
    {
        // If we don't have a reference pose, sample it once
        if (m_additiveBaseTransforms.empty() && m_additiveRefClip)
            SampleAnimationToBuffer(m_additiveRefClip, 0.0f, m_additiveBaseTransforms);

        for (size_t i = 0; i < additive.size() && i < result.size(); ++i)
        {
            Matrix4 baseRef = (i < m_additiveBaseTransforms.size()) ?
                m_additiveBaseTransforms[i] : Matrix4::Identity();

            // Extract components for proper additive calculation
            Vector3 baseRefT = baseRef.GetTranslation();
            Quaternion baseRefR = baseRef.GetRotation();
            Vector3 baseRefS = baseRef.GetScale();

            Vector3 additiveT = additive[i].GetTranslation();
            Quaternion additiveR = additive[i].GetRotation();
            Vector3 additiveS = additive[i].GetScale();

            Vector3 resultT = result[i].GetTranslation();
            Quaternion resultR = result[i].GetRotation();
            Vector3 resultS = result[i].GetScale();

            // Compute deltas
            Vector3 deltaT = additiveT - baseRefT;
            Quaternion deltaR = additiveR * baseRefR.Inverse();
            Vector3 deltaS = Vector3(additiveS.x / (baseRefS.x != 0.0f ? baseRefS.x : 1.0f), additiveS.y / (baseRefS.y != 0.0f ? baseRefS.y : 1.0f), additiveS.z / (baseRefS.z != 0.0f ? baseRefS.z : 1.0f));

            // Apply deltas
            Vector3 finalT = resultT + deltaT;
            Quaternion finalR = resultR * deltaR;
            Vector3 finalS = Vector3(resultS.x * deltaS.x, resultS.y * deltaS.y, resultS.z * deltaS.z);

            result[i] = Matrix4::Translate(finalT) * Matrix4::FromQuaternion(finalR) * Matrix4::Scale(finalS);
        }
    }

    void Animator::SampleAnimationToBuffer(AnimationClip* clip, float time, std::vector<Matrix4>& buffer)
    {
        if (!clip || !m_skeleton)
            return;

        buffer.resize(m_skeleton->bones.size());
        for (size_t i = 0; i < buffer.size(); ++i)
            buffer[i] = m_skeleton->bones[i].localTransform;

        struct AnimData { bool hasT = false, hasR = false, hasS = false; Vector3 t; Quaternion r; Vector3 s; };
        std::vector<AnimData> animData(m_skeleton->bones.size());

        for (const AnimationChannel& channel : clip->GetChannels())
        {
            int boneIndex = channel.targetBoneIndex;
            if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeleton->bones.size()))
                continue;

            if (!channel.translations.empty())
            {
                animData[boneIndex].t = InterpolateTranslation(channel, time);
                animData[boneIndex].hasT = true;
            }
            if (!channel.rotations.empty())
            {
                animData[boneIndex].r = InterpolateRotation(channel, time);
                animData[boneIndex].hasR = true;
            }
            if (!channel.scales.empty())
            {
                animData[boneIndex].s = InterpolateScale(channel, time);
                animData[boneIndex].hasS = true;
            }
        }

        for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
        {
            if (animData[i].hasT || animData[i].hasR || animData[i].hasS)
            {
                Vector3 t = animData[i].hasT ? animData[i].t : m_skeleton->bones[i].localTransform.GetTranslation();
                Quaternion r = animData[i].hasR ? animData[i].r : m_skeleton->bones[i].localTransform.GetRotation();
                Vector3 s = animData[i].hasS ? animData[i].s : m_skeleton->bones[i].localTransform.GetScale();
                buffer[i] = Matrix4::Translate(t) * Matrix4::FromQuaternion(r) * Matrix4::Scale(s);
            }
        }
    }

    void Animator::EvaluateBlendTree(BlendTreeNode* node, float time, std::vector<Matrix4>& result)
    {
        if (!node)
            return;

        switch (node->type)
        {
            case BlendTreeNode::Type::Clip:
                if (node->clip)
                    SampleAnimationToBuffer(node->clip, time, result);
                break;

            case BlendTreeNode::Type::Blend1D:
            {
                if (node->children.size() < 2)
                    return;

                float param = m_blendParameter;
                int idx = 0;
                for (size_t i = 0; i < node->thresholds.size(); ++i)
                {
                    if (param >= node->thresholds[i])
                        idx = static_cast<int>(i);
                }

                int nextIdx = std::min(idx + 1, static_cast<int>(node->children.size()) - 1);

                if (idx >= static_cast<int>(node->thresholds.size()))
                    idx = static_cast<int>(node->thresholds.size()) - 1;
                if (nextIdx >= static_cast<int>(node->thresholds.size()))
                    nextIdx = static_cast<int>(node->thresholds.size()) - 1;

                float t0 = node->thresholds[idx];
                float t1 = node->thresholds[nextIdx];
                float blend = (t1 - t0 > 0.0001f) ? (param - t0) / (t1 - t0) : 0.0f;
                blend = std::max(0.0f, std::min(1.0f, blend));

                std::vector<Matrix4> temp1, temp2;
                EvaluateBlendTree(node->children[idx], time, temp1);
                EvaluateBlendTree(node->children[nextIdx], time, temp2);
                BlendBoneTransforms(temp1, temp2, blend, result);
                break;
            }

            case BlendTreeNode::Type::Blend2D:
            {
                if (node->children.size() < 3)
                {
                    // Fallback to simple blend
                    if (node->children.size() == 2)
                    {
                        std::vector<Matrix4> temp1, temp2;
                        EvaluateBlendTree(node->children[0], time, temp1);
                        EvaluateBlendTree(node->children[1], time, temp2);

                        float blend = (m_blendParameter + 1.0f) * 0.5f; // Normalize to 0-1
                        blend = std::max(0.0f, std::min(1.0f, blend));
                        BlendBoneTransforms(temp1, temp2, blend, result);
                    }
                    return;
                }

                // Find closest triangle and blend
                Vector2 point(m_blendParameter, m_blendParameterY);

                // Find 3 closest points and blend
                // Todo: Implement a proper Delaunay triangulation
                std::vector<std::pair<float, size_t>> distances;
                for (size_t i = 0; i < node->positions.size() && i < node->children.size(); ++i)
                {
                    Vector2 pos = node->positions[i];
                    float dist = (pos.x - point.x) * (pos.x - point.x) +
                        (pos.y - point.y) * (pos.y - point.y);
                    distances.push_back({ dist, i });
                }

                std::sort(distances.begin(), distances.end());

                // Blend between 3 closest
                size_t numBlend = std::min(size_t(3), distances.size());
                std::vector<Matrix4> blended;

                float totalWeight = 0.0f;
                for (size_t i = 0; i < numBlend; ++i)
                {
                    float weight = 1.0f / (distances[i].first + 0.001f); // Inverse distance
                    totalWeight += weight;
                }

                for (size_t i = 0; i < numBlend; ++i)
                {
                    std::vector<Matrix4> temp;
                    EvaluateBlendTree(node->children[distances[i].second], time, temp);

                    float weight = (1.0f / (distances[i].first + 0.001f)) / totalWeight;

                    if (i == 0)
                        blended = temp;
                    else
                        BlendBoneTransforms(blended, temp, weight / (weight + (1.0f - weight)), blended);
                }

                result = blended;
                break;
            }

            case BlendTreeNode::Type::Additive:
                if (!node->children.empty())
                {
                    EvaluateBlendTree(node->children[0], time, result);
                    if (node->children.size() > 1)
                    {
                        std::vector<Matrix4> additiveTransforms;
                        EvaluateBlendTree(node->children[1], time, additiveTransforms);
                        ApplyAdditiveAnimation(additiveTransforms, result);
                    }
                }
                break;
        }
    }

    Matrix4 Animator::BlendMatrices(const Matrix4& a, const Matrix4& b, float t)
    {
        Vector3 transA = a.GetTranslation();
        Vector3 transB = b.GetTranslation();
        Quaternion rotA = a.GetRotation();
        Quaternion rotB = b.GetRotation();
        Vector3 scaleA = a.GetScale();
        Vector3 scaleB = b.GetScale();

        Vector3 trans = transA + (transB - transA) * t;
        Quaternion rot = Quaternion::Slerp(rotA, rotB, t);
        Vector3 scale = scaleA + (scaleB - scaleA) * t;

        return Matrix4::Translate(trans) * Matrix4::FromQuaternion(rot) * Matrix4::Scale(scale);
    }

    void Animator::SampleAnimation(float time)
    {
        if (!m_currentClip || !m_skeleton)
            return;

        for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
            m_localTransforms[i] = m_skeleton->bones[i].localTransform;

        struct AnimData
        {
            bool hasT = false, hasR = false, hasS = false;
            Vector3 t;
            Quaternion r;
            Vector3 s;
        };
        std::vector<AnimData> animData(m_skeleton->bones.size());

        // Get the animated components
        for (const AnimationChannel& channel : m_currentClip->GetChannels())
        {
            int boneIndex = channel.targetBoneIndex;
            if (boneIndex < 0 || boneIndex >= static_cast<int>(m_skeleton->bones.size()))
                continue;

            if (!channel.translations.empty())
            {
                animData[boneIndex].t = InterpolateTranslation(channel, time);
                animData[boneIndex].hasT = true;
            }

            if (!channel.rotations.empty())
            {
                animData[boneIndex].r = InterpolateRotation(channel, time);
                animData[boneIndex].hasR = true;
            }

            if (!channel.scales.empty())
            {
                animData[boneIndex].s = InterpolateScale(channel, time);
                animData[boneIndex].hasS = true;
            }
        }

        // Reconstruct only bones that have animation
        for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
        {
            if (animData[i].hasT || animData[i].hasR || animData[i].hasS)
            {
                Vector3 t = animData[i].hasT ? animData[i].t : m_skeleton->bones[i].localTransform.GetTranslation();
                Quaternion r = animData[i].hasR ? animData[i].r : m_skeleton->bones[i].localTransform.GetRotation();
                Vector3 s = animData[i].hasS ? animData[i].s : m_skeleton->bones[i].localTransform.GetScale();

                m_localTransforms[i] = Matrix4::Translate(t) * Matrix4::FromQuaternion(r) * Matrix4::Scale(s);
            }
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

        std::function<void(int, const Matrix4&)> compute = [&](int index, const Matrix4& parentGlobal)
        {
            const Bone& bone = m_skeleton->bones[index];
            Matrix4 global = parentGlobal * m_localTransforms[index];

            m_boneMatrices[index] = global * bone.inverseBindMatrix;

            for (int child : bone.children)
                compute(child, global);
        };

        for (size_t i = 0; i < m_skeleton->bones.size(); ++i)
        {
            if (m_skeleton->bones[i].parentIndex == -1)
                compute((int)i, Matrix4::Identity());
        }

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