#pragma once

#include "Model.h"
#include "Animation.h"
#include <vector>
#include <string>

namespace cl
{
    Model* LoadGLTF(std::string_view filePath, bool mergeMeshes = true, int sceneIndex = -1);

    /// Load animation by index
    AnimationClip* LoadAnimationFromGLTF(std::string_view filePath, size_t animationIndex = 0);

    /// Load animation by name
    AnimationClip* LoadAnimationFromGLTF(std::string_view filePath, std::string_view animationName);

    // Load all animations in file
    std::vector<AnimationClip*> LoadAnimationsFromGLTF(std::string_view filePath);
}