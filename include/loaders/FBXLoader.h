#pragma once

#include "Model.h"
#include "Animation.h"
#include <vector>
#include <string>

namespace cl
{
    Model* LoadFBX(std::string_view filePath);

    /// Load animation by index
    AnimationClip* LoadAnimationFromFBX(std::string_view filePath, size_t animationIndex = 0);

    /// Load animation by name
    AnimationClip* LoadAnimationFromFBX(std::string_view filePath, std::string_view animationName);

    // Load all animations in file
    std::vector<AnimationClip*> LoadAnimationsFromFBX(std::string_view filePath);
}