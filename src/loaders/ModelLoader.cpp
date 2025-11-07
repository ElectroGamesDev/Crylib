#include "loaders/ModelLoader.h"
#include "loaders/GLTFLoader.h"
#include "loaders/FBXLoader.h"
#include "loaders/OBJLoader.h"
#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <iostream>

namespace cl
{
    Model* LoadModel(std::string_view filePath)
    {
        std::filesystem::path path = filePath;

        if (path.extension() == ".gltf" || path.extension() == ".glb")
            return LoadGLTF(filePath);
        else if (path.extension() == ".fbx")
            return LoadFBX(filePath);
        else if (path.extension() == ".obj")
            return LoadOBJ(filePath);

        std::cout << "[ERROR] Failed to load model \"" << filePath << "\". The model format may not be supported." << std::endl;

        return nullptr;
    }

    Model* LoadInstance(const Model* model)
    {
        if (!model)
            return nullptr;

        Model* instance = new Model();
        instance->m_meshes = model->GetMeshes();
        instance->m_skeleton = model->GetSkeleton();
        instance->m_animations = model->m_animations;
        instance->Reset();

        if (instance->m_skeleton)
            instance->m_animator.SetSkeleton(instance->m_skeleton);

        return instance;
    }
    AnimationClip* LoadAnimation(std::string_view filePath, size_t animationIndex)
    {
        std::filesystem::path path = filePath;

        if (path.extension() == ".gltf" || path.extension() == ".glb")
            return LoadAnimationFromGLTF(filePath, animationIndex);
        else if (path.extension() == ".fbx")
            return LoadAnimationFromFBX(filePath, animationIndex);

        std::cout << "[ERROR] Failed to load animation for \"" << filePath << "\". This model format may not support animations." << std::endl;

        return nullptr;
    }
    AnimationClip* LoadAnimation(std::string_view filePath, std::string_view animationName)
    {
        std::filesystem::path path = filePath;

        if (path.extension() == ".gltf" || path.extension() == ".glb")
            return LoadAnimationFromGLTF(filePath, animationName);
        else if (path.extension() == ".fbx")
            return LoadAnimationFromFBX(filePath, animationName);

        std::cout << "[ERROR] Failed to load animation for \"" << filePath << "\". This model format may not support animations." << std::endl;

        return nullptr;
    }
    std::vector<AnimationClip*> LoadAnimations(std::string_view filePath)
    {
        std::filesystem::path path = filePath;

        if (path.extension() == ".gltf" || path.extension() == ".glb")
            return LoadAnimations(filePath);
        else if (path.extension() == ".fbx")
            return LoadAnimations(filePath);

        std::cout << "[ERROR] Failed to load animations for \"" << filePath << "\". This model format may not support animations." << std::endl;

        return {};
    }
}