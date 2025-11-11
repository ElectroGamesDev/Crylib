#include "Renderer.h"
#include <bgfx.h>
#include <platform.h>
#include <algorithm>

#ifdef PLATFORM_WINDOWS
#define NOMINMAX
#include <windows.h>
#endif
#include <iostream>

namespace cl
{
    static bgfx::UniformHandle u_BoneMatrices = BGFX_INVALID_HANDLE;
    static bgfx::UniformHandle u_IsSkinned = BGFX_INVALID_HANDLE;
    static std::unordered_map<InstanceBatchKey, InstanceBatch, InstanceBatchKeyHasher> s_instanceBatches;

    RendererState* s_renderer = nullptr;

    bool InitRenderer(Window* window, const Config& config)
    {
        if (!window)
            return false;

        s_renderer = new RendererState();
        s_renderer->window = window;

        // Get window size
        window->GetWindowSize(s_renderer->width, s_renderer->height);

        // Setup bgfx platform data
        bgfx::PlatformData pd;
#ifdef PLATFORM_WINDOWS
        pd.nwh = window->GetNativeWindowHandle();
#endif
        pd.ndt = nullptr;
        pd.context = nullptr;
        pd.backBuffer = nullptr;
        pd.backBufferDS = nullptr;

        // Initialize bgfx
        bgfx::Init init;
        init.vendorId = BGFX_PCI_ID_NONE; // Todo: According to the vendorID comments, there's a chance integrated graphis may be used instead of the dedicated graphics card
        init.platformData = pd;
        init.resolution.width = s_renderer->width;
        init.resolution.height = s_renderer->height;

        if (config.windowVSync)
            init.resolution.reset = BGFX_RESET_VSYNC;
        else
            init.resolution.reset = BGFX_RESET_NONE;

        init.type = bgfx::RendererType::Noop;

        if (config.renderingAPI == DirectX11)
            init.type = bgfx::RendererType::Direct3D11;
        else if (config.renderingAPI == DirectX12)
            init.type = bgfx::RendererType::Direct3D12;
        else if (config.renderingAPI == Metal)
            init.type = bgfx::RendererType::Metal;
        else if (config.renderingAPI == OpenGLES)
            init.type = bgfx::RendererType::OpenGLES;
        else if (config.renderingAPI == OpenGL)
            init.type = bgfx::RendererType::OpenGL;
        else if (config.renderingAPI == Vulkan)
            init.type = bgfx::RendererType::Vulkan;

        if (!bgfx::init(init))
        {
            delete s_renderer;
            s_renderer = nullptr;
            return false;
        }

        // Set view rectangle
        //bgfx::setViewRect(0, 0, 0, uint16_t(s_renderer->width), uint16_t(s_renderer->height));
        //bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ff, 1.0f, 0);

        // Todo: Likely remove this and CreateDefautlShader()
        // Create default shader
        //s_renderer->defaultProgram = CreateDefaultShader();

        u_BoneMatrices = bgfx::createUniform("u_BoneMatrices", bgfx::UniformType::Vec4, 128); // This is enough for most models, but to configure it, it would also need to be set in the shader. Also, should we instead be using mat4?
        u_IsSkinned = bgfx::createUniform("u_IsSkinned", bgfx::UniformType::Vec4);

        return true;
    }

    void ShutdownRenderer()
    {
        if (s_renderer)
        {
            // Todo: Probably remove this
            //if (bgfx::isValid(s_renderer->defaultProgram))
            //    bgfx::destroy(s_renderer->defaultProgram);

            if (bgfx::isValid(u_BoneMatrices))
                bgfx::destroy(u_BoneMatrices);

            if (bgfx::isValid(u_IsSkinned))
                bgfx::destroy(u_IsSkinned);

            bgfx::shutdown();
            delete s_renderer;
            s_renderer = nullptr;
        }
    }

    void BeginFrame()
    {
        if (!s_renderer)
            return;

        s_renderer->frameStartTime = std::chrono::steady_clock::now();
        s_renderer->drawStats = DrawStats();

        if (s_renderer->profilerEnabled)
            s_renderer->profileMarkers.clear();

        s_renderer->currentViewId = 0;
        s_renderer->window->GetWindowSize(s_renderer->width, s_renderer->height);
    }

    void EndFrame()
    {
        if (!s_renderer)
            return;

        bgfx::frame();

        // CPU time
        s_renderer->frameEndTime = std::chrono::steady_clock::now();
        std::chrono::duration<float, std::milli> cpuTime = s_renderer->frameEndTime - s_renderer->frameStartTime;
        s_renderer->drawStats.cpuTime = cpuTime.count();

        // Fetch BGFX stats
        const bgfx::Stats* stats = bgfx::getStats();
        if (!stats)
            return;

        // GPU time
        if (stats->gpuTimeEnd > stats->gpuTimeBegin && stats->gpuTimerFreq > 0)
            s_renderer->drawStats.gpuTime = float(stats->gpuTimeEnd - stats->gpuTimeBegin) / stats->gpuTimerFreq * 1000.0f;
        else
            s_renderer->drawStats.gpuTime = 0.0f;

        // Draw call counts
        s_renderer->drawStats.drawCalls = stats->numDraw;

        // Texture and shader counts
        s_renderer->drawStats.textureBinds = stats->numTextures;
        s_renderer->drawStats.shaderSwitches = stats->numShaders;

        // Memory usage
        s_renderer->drawStats.textureMemoryUsed = stats->textureMemoryUsed;
        s_renderer->drawStats.gpuMemoryUsed = stats->gpuMemoryUsed;
    }


    void Clear(const Color& color, float depth)
    {
        if (!s_renderer)
            return;

        uint32_t rgba = 0;
        rgba |= uint32_t(color.r) << 24;
        rgba |= uint32_t(color.g) << 16;
        rgba |= uint32_t(color.b) << 8;
        rgba |= uint32_t(color.a);

        s_renderer->clearColor = rgba;
        s_renderer->clearDepth = depth;

        if (s_renderer->currentViewId != 0)
            bgfx::setViewClear(s_renderer->currentViewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, depth, 0);
    }

    void SetViewport(int x, int y, int width, int height)
    {
        if (!s_renderer || s_renderer->currentViewId == 0)
            return;

        bgfx::setViewRect(s_renderer->currentViewId, uint16_t(x), uint16_t(y), uint16_t(width), uint16_t(height));
    }

    void SetViewTransform(const Matrix4& view, const Matrix4& projection)
    {
        if (!s_renderer || s_renderer->currentViewId == 0)
            return;

        bgfx::setViewTransform(s_renderer->currentViewId, view.m, projection.m);
    }

    uint64_t GetBlendState(BlendMode mode)
    {
        switch (mode)
        {
        case BlendMode::None:
            return 0;

        case BlendMode::Alpha:
            return BGFX_STATE_BLEND_ALPHA;

        case BlendMode::Additive:
            return BGFX_STATE_BLEND_ADD;

        case BlendMode::Multiplied:
            return BGFX_STATE_BLEND_MULTIPLY;

        case BlendMode::Subtract:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_ONE) | BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_REVSUB);

        case BlendMode::Screen:
            return BGFX_STATE_BLEND_SCREEN;

        case BlendMode::Darken:
            return BGFX_STATE_BLEND_DARKEN;

        case BlendMode::Lighten:
            return BGFX_STATE_BLEND_LIGHTEN;

        case BlendMode::LinearBurn:
            return BGFX_STATE_BLEND_LINEAR_BURN;

        case BlendMode::LinearDodge:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_ONE) | BGFX_STATE_BLEND_EQUATION(BGFX_STATE_BLEND_EQUATION_ADD);

        case BlendMode::PremultipliedAlpha:
            return BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);

        default:
            return 0;
        }
    }

    void DrawMesh(Mesh* mesh, const Matrix4& transform)
    {
        if (s_renderer->currentViewId == 0 || !mesh || !mesh->IsValid() || !mesh->GetMaterial() || !mesh->GetMaterial()->GetShader())
            return;

        // Allocate instance data buffer for single instance
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, 1, sizeof(Matrix4));

        if (!bgfx::isValid(idb.handle))
        {
            std::cerr << "[ERROR] Failed to allocate instance data buffer for single mesh draw.\n";
            return;
        }

        // Copy transform data
        std::memcpy(idb.data, &transform, sizeof(Matrix4));

        bgfx::setVertexBuffer(0, mesh->GetVertexBuffer());
        bgfx::setIndexBuffer(mesh->GetIndexBuffer());
        bgfx::setInstanceDataBuffer(&idb);

        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_CULL_CW
            | BGFX_STATE_MSAA
            | GetBlendState(s_renderer->currentBlendMode);

        Material* material = mesh->GetMaterial();
        Shader* shader = material->GetShader();

        // Apply global uniforms
        shader->ApplyUniforms();

        // Apply material specific uniforms
        material->ApplyShaderUniforms();

        // Apply PBR material map uniforms
        material->ApplyPBRUniforms();

        // Apply skinned and bone uniforms
        if (mesh->IsSkinned())
        {
            const float enabled[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            bgfx::setUniform(u_IsSkinned, enabled);
        }

        bgfx::setState(state);
        bgfx::submit(s_renderer->currentViewId, shader->GetHandle());

        // Update stats
        s_renderer->drawStats.drawCalls++;
        s_renderer->drawStats.triangles += mesh->GetTriangleCount();
        s_renderer->drawStats.vertices += mesh->GetVertices().size();
        s_renderer->drawStats.indicies += mesh->GetIndices().size();
    }

    void DrawMesh(Mesh* mesh, const Vector3& position, const Quaternion& rotation, const Vector3& scale)
    {
        if (!mesh)
            return;

        Matrix4 transform = Matrix4::Translate(position) * rotation.ToMatrix() * Matrix4::Scale(scale);
        DrawMesh(mesh, transform);
    }

    void DrawMesh(Mesh* mesh, const Vector3& position, const Vector3& rotation, const Vector3& scale)
    {
        if (!mesh)
            return;

        Quaternion rotQuat = Quaternion::FromEuler(rotation.y, rotation.x, rotation.z);
        DrawMesh(mesh, position, rotQuat, scale);
    }

    void DrawModel(Model* model, const Matrix4& transform)
    {
        if (!model)
            return;

        for (const auto& mesh : model->GetMeshes())
            DrawMesh(mesh.get(), transform);
    }

    void DrawModel(Model* model, const Vector3& position, const Quaternion& rotation, const Vector3& scale)
    {
        if (!model || !model->HasMeshes())
            return;

        Matrix4 baseTransform = Matrix4::Translate(position) * rotation.ToMatrix() * Matrix4::Scale(scale);

        // Determine what animation data to use
        const Animator* animator = model->GetAnimator();
        const std::vector<Matrix4>* bones = nullptr;
        bool useNodeAnimation = false;

        if (animator && animator->IsPlaying())
        {
            AnimationClip* currentClip = animator->GetCurrentClip();
            if (currentClip)
            {
                if (currentClip->GetAnimationType() == AnimationType::Skeletal && model->HasSkeleton())
                    bones = &animator->GetBoneMatrices();
                else if (currentClip->GetAnimationType() == AnimationType::NodeBased)
                    useNodeAnimation = true;
            }
        }
        else if (animator && model->HasSkeleton())
            bones = &animator->GetBoneMatrices(); // Even though there isn't an animation playing, we still need to bind the pose

        // Draw all meshes
        if (useNodeAnimation)
        {
            const auto& nodeTransforms = animator->GetNodeTransforms();
            for (size_t i = 0; i < model->GetMeshes().size(); ++i)
            {
                const auto& mesh = model->GetMeshes()[i];
                Matrix4 meshTransform = baseTransform;

                // Checks if the mesh hs an animated node transform
                if (i < nodeTransforms.size() && nodeTransforms[i] != Matrix4::Identity())
                    meshTransform = baseTransform * nodeTransforms[i];
                else if (!nodeTransforms.empty())
                {
                    for (const auto& transform : nodeTransforms)
                    {
                        if (transform != Matrix4::Identity())
                        {
                            meshTransform = baseTransform * transform;
                            break;
                        }
                    }
                }

                DrawMesh(mesh.get(), meshTransform);
            }
        }
        else
        {
            if (bones)
                bgfx::setUniform(u_BoneMatrices, bones->data(), (uint16_t)bones->size()); // Todo: There may be issues if the bones > max bones set when creating the u_boneMatrices uniform
            for (const auto& mesh : model->GetMeshes())
                DrawMesh(mesh.get(), baseTransform);
        }
    }

    void DrawModel(Model* model, const Vector3& position, const Vector3& rotation, const Vector3& scale)
    {
        if (!model || !model->HasMeshes())
            return;

        Quaternion rotQuat = Quaternion::FromEuler(rotation.y, rotation.x, rotation.z);
        DrawModel(model, position, rotQuat, scale);
    }

    void DrawModel(Model* model)
    {
        if (!model)
            return;

        //model->UpdateTransformMatrix();
        for (const auto& mesh : model->GetMeshes())
            DrawModel(model, model->GetPosition(), model->GetRotationQuat(), model->GetScale());
    }

    void DrawMeshInstanced(Mesh* mesh, const std::vector<Matrix4>& transforms, const std::vector<Matrix4>* boneMatrices)
    {
        if (!mesh || !mesh->IsValid() || !mesh->GetMaterial() || !mesh->GetMaterial()->GetShader() || transforms.empty())
            return;

        Material* material = mesh->GetMaterial();
        Shader* shader = material->GetShader();

        uint64_t state = BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_CULL_CW
            | BGFX_STATE_MSAA
            | GetBlendState(s_renderer->currentBlendMode);

        constexpr uint32_t maxInstancesPerBatch = 512; // Todo: Should this be higher?
        uint32_t numInstances = static_cast<uint32_t>(transforms.size());
        uint32_t instanceOffset = 0;
        while (instanceOffset < numInstances)
        {
            uint32_t batchSize = std::min(maxInstancesPerBatch, numInstances - instanceOffset);
            bgfx::InstanceDataBuffer idb;
            bgfx::allocInstanceDataBuffer(&idb, batchSize, sizeof(Matrix4));

            // Check validity
            if (!bgfx::isValid(idb.handle))
            {
                std::cerr << "[ERROR] Failed to allocate instance data buffer for batch of " << batchSize << " instances.\n";
                instanceOffset += batchSize;
                continue;
            }

            // Copy transform data
            std::memcpy(idb.data, &transforms[instanceOffset], batchSize * sizeof(Matrix4));

            // Bind buffers
            bgfx::setVertexBuffer(0, mesh->GetVertexBuffer());
            bgfx::setIndexBuffer(mesh->GetIndexBuffer());
            bgfx::setInstanceDataBuffer(&idb);

            // Uniforms must be set each frame

            // Apply global uniforms
            shader->ApplyUniforms();

            // Apply material specific uniforms
            material->ApplyShaderUniforms();

            // Apply PBR material map uniforms
            material->ApplyPBRUniforms();

            // Bone matrices
            if (mesh->IsSkinned())
            {
                float enabled[4] = { boneMatrices ? 1.0f : 0.0f, 0, 0, 0 };
                bgfx::setUniform(u_IsSkinned, enabled);

                if (boneMatrices)
                    bgfx::setUniform(u_BoneMatrices, boneMatrices->data(), (uint16_t)boneMatrices->size());
            }

            bgfx::setState(state);
            bgfx::submit(s_renderer->currentViewId, shader->GetHandle());

            // Update stats
            s_renderer->drawStats.drawCalls++;
            s_renderer->drawStats.triangles += mesh->GetTriangleCount() * batchSize;
            s_renderer->drawStats.vertices += static_cast<uint32_t>(mesh->GetVertices().size()) * batchSize;
            s_renderer->drawStats.indicies += static_cast<uint32_t>(mesh->GetIndices().size()) * batchSize;
            instanceOffset += batchSize;
        }
    }

    void DrawModelInstanced(Model* model, const Vector3& position, const Quaternion& rotation, const Vector3& scale)
    {
        if (!model || !model->HasMeshes())
            return;

        Matrix4 baseTransform = Matrix4::Translate(position) * rotation.ToMatrix() * Matrix4::Scale(scale);
        const Animator* animator = model->GetAnimator();
        const std::vector<Matrix4>* bones = nullptr;
        bool skipInstancing = false;

        if (animator && animator->IsPlaying())
        {
            AnimationClip* clip = animator->GetCurrentClip();
            if (clip)
            {
                if (clip->GetAnimationType() == AnimationType::Skeletal && model->HasSkeleton())
                    bones = &animator->GetBoneMatrices();
                else if (clip->GetAnimationType() == AnimationType::NodeBased)
                    skipInstancing = true;
            }
        }
        else if (animator && model->HasSkeleton())
            bones = &animator->GetBoneMatrices();
        if (skipInstancing)
        {
            DrawModel(model, model->GetPosition(), model->GetRotationQuat(), model->GetScale());
            return;
        }
        for (const auto& mesh : model->GetMeshes())
        {
            if (!mesh || !mesh->IsValid() || !mesh->GetMaterial() || !mesh->GetMaterial()->GetShader())
                continue;

            InstanceBatchKey key{ mesh.get(), mesh->GetMaterial(), mesh->GetMaterial()->GetShader(), bones };
            auto it = s_instanceBatches.find(key);
            if (it == s_instanceBatches.end())
            {
                InstanceBatch batch;
                batch.mesh = mesh.get();
                batch.material = mesh->GetMaterial();
                batch.shader = mesh->GetMaterial()->GetShader();
                batch.boneMatrices = bones;
                batch.isSkinned = mesh->IsSkinned();
                batch.transforms.push_back(baseTransform);
                s_instanceBatches.emplace(key, std::move(batch));
            }
            else
                it->second.transforms.push_back(baseTransform);
        }
    }

    void DrawModelInstanced(Model* model, const Vector3& position, const Vector3& rotation, const Vector3& scale)
    {
        if (!model || !model->HasMeshes())
            return;

        Quaternion rotQuat = Quaternion::FromEuler(rotation.y, rotation.x, rotation.z);
        DrawModelInstanced(model, position, rotQuat, scale);
    }

    void DrawModelInstanced(Model* model)
    {
        if (!model || !model->HasMeshes())
            return;

        DrawModelInstanced(model, model->GetPosition(), model->GetRotationQuat(), model->GetScale());
    }

    void SubmitInstances()
    {
        for (auto& pair : s_instanceBatches)
        {
            InstanceBatch& batch = pair.second;
            if (batch.transforms.empty())
                continue;

            DrawMeshInstanced(batch.mesh, batch.transforms, batch.boneMatrices);
            pair.second.Clear();
        }
    }

    void SetCullMode(bool enabled, bool clockwise)
    {
        // Todo: Culling state is set per-draw call in bgfx
    }

    void SetDepthTest(bool enabled)
    {
        // Todo: Depth test state is set per-draw call in bgfx
    }

    void SetWireframe(bool enabled)
    {
        if (enabled)
            bgfx::setDebug(BGFX_DEBUG_WIREFRAME);
        else
            bgfx::setDebug(BGFX_DEBUG_NONE);
    }

    int GetViewWidth()
    {
        return s_renderer ? s_renderer->width : 0;
    }

    int GetViewHeight()
    {
        return s_renderer ? s_renderer->height : 0;
    }

    bgfx::ProgramHandle CreateDefaultShader()
    {
        return BGFX_INVALID_HANDLE;
    }

    // Blend Mode

    void SetBlendMode(BlendMode mode)
    {
        if (s_renderer)
            s_renderer->currentBlendMode = mode;
    }

    BlendMode GetBlendMode()
    {
        return s_renderer ? s_renderer->currentBlendMode : BlendMode::None;
    }

    // Performance Stats

    void ResetDrawStats()
    {
        if (s_renderer)
            s_renderer->drawStats = DrawStats();
    }

    int GetDrawCallCount()
    {
        return s_renderer ? s_renderer->drawStats.drawCalls : 0;
    }

    int GetTriangleCount()
    {
        return s_renderer ? s_renderer->drawStats.triangles : 0;
    }

    int GetVertexCount()
    {
        return s_renderer ? s_renderer->drawStats.vertices : 0;
    }

    int GetIndexCount()
    {
        return s_renderer ? s_renderer->drawStats.indicies : 0;
    }

    int GetTextureBindCount()
    {
        return s_renderer ? s_renderer->drawStats.textureBinds : 0;
    }

    int GetShaderSwitchCount()
    {
        return s_renderer ? s_renderer->drawStats.shaderSwitches : 0;
    }

    float GetCPUFrameTime()
    {
        return s_renderer ? s_renderer->drawStats.cpuTime : 0.0f;
    }

    float GetGPUFrameTime()
    {
        return s_renderer ? s_renderer->drawStats.gpuTime : 0.0f;
    }

    const DrawStats& GetDrawStats()
    {
        static DrawStats emptyStats;
        return s_renderer ? s_renderer->drawStats : emptyStats;
    }

    // Renderer Info

    std::string GetRendererName()
    {
        if (!s_renderer)
            return "Unknown";

        return bgfx::getRendererName(bgfx::getRendererType());
    }

    std::string GetGPUVendor()
    {
        if (!s_renderer)
            return "Unknown";

        const bgfx::Caps* caps = bgfx::getCaps();

        switch (caps->vendorId)
        {
            case BGFX_PCI_ID_NONE:
                return "Unknown";
            case BGFX_PCI_ID_SOFTWARE_RASTERIZER:
                return "Software Rasterizer";
            case BGFX_PCI_ID_AMD:
                return "AMD";
            case BGFX_PCI_ID_APPLE:
                return "Apple";
            case BGFX_PCI_ID_INTEL:
                return "Intel";
            case BGFX_PCI_ID_NVIDIA:
                return "NVIDIA";
            case BGFX_PCI_ID_MICROSOFT:
                return "Microsoft";
            case BGFX_PCI_ID_ARM:
                return "ARM";
            default:
                return "Unknown";
        }
    }

    int GetMaxTextureSize()
    {
        if (!s_renderer)
            return 0;

        return bgfx::getCaps()->limits.maxTextureSize;
    }

    int GetGPUMemoryUsage()
    {
        if (!s_renderer)
            return 0;

        return s_renderer->drawStats.gpuMemoryUsed;
    }

    int GetTextureMemoryUsage()
    {
        if (!s_renderer)
            return 0;

        return s_renderer->drawStats.textureMemoryUsed;
    }

    // Profiling and Markers

    void BeginProfileMarker(std::string_view name)
    {
        if (!s_renderer || !s_renderer->profilerEnabled)
            return;

        s_renderer->currentMarkerName = name;
        s_renderer->currentMarkerStart = std::chrono::high_resolution_clock::now();
    }

    void EndProfileMarker()
    {
        if (!s_renderer || !s_renderer->profilerEnabled || !s_renderer->currentMarkerName.empty())
            return;

        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float, std::milli> duration = endTime - s_renderer->currentMarkerStart;

        ProfileMarker marker;
        marker.name = s_renderer->currentMarkerName;
        marker.cpuTime = duration.count();
        marker.gpuTime = 0.0f;

        s_renderer->profileMarkers.push_back(marker);
        s_renderer->currentMarkerName = nullptr;
    }

    void SetProfilerEnabled(bool enabled)
    {
        if (s_renderer)
            s_renderer->profilerEnabled = enabled;
    }

    const std::vector<ProfileMarker>& GetProfileMarkers()
    {
        static std::vector<ProfileMarker> empty;
        return s_renderer ? s_renderer->profileMarkers : empty;
    }

    void SetDebugMarker(std::string_view marker)
    {
        if (!s_renderer)
            return;

        bgfx::setMarker(marker.data());
    }
}