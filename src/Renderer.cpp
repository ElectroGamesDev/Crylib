#include "Renderer.h"
#include <bgfx.h>
#include <platform.h>

#ifdef PLATFORM_WINDOWS
#include <windows.h>
#endif
#include <iostream>

namespace cl
{
    static bgfx::UniformHandle u_BoneMatrices = BGFX_INVALID_HANDLE;

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
        init.resolution.reset = BGFX_RESET_VSYNC;

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

        u_BoneMatrices = bgfx::createUniform("u_BoneMatrices", bgfx::UniformType::Vec4, 384);

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

            bgfx::shutdown();
            delete s_renderer;
            s_renderer = nullptr;
        }
    }

    void BeginFrame()
    {
        if (!s_renderer)
            return;

        // Update window size if changed
        s_renderer->currentViewId = -1;
        s_renderer->window->GetWindowSize(s_renderer->width, s_renderer->height); // Todo: Maybe I can add this to Update() or something so it doesnt need to be manually called. Also, maybe just rename Update() to BeginFrame()

        // This is now handled in cameras
        //bgfx::setViewRect(0, 0, 0, uint16_t(s_renderer->width), uint16_t(s_renderer->height));

        //// Touch view to ensure it's submitted
        //bgfx::touch(0);
    }

    void EndFrame()
    {
        if (!s_renderer)
            return;

        bgfx::frame();
    }

    void Clear(const Color& color, float depth)
    {
        if (!s_renderer || s_renderer->currentViewId == -1)
            return;

        uint32_t rgba = 0;
        rgba |= uint32_t(color.r) << 24;
        rgba |= uint32_t(color.g) << 16;
        rgba |= uint32_t(color.b) << 8;
        rgba |= uint32_t(color.a);

        bgfx::setViewClear(s_renderer->currentViewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, rgba, depth, 0);
    }

    void SetViewport(int x, int y, int width, int height)
    {
        if (!s_renderer || s_renderer->currentViewId == -1)
            return;

        bgfx::setViewRect(s_renderer->currentViewId, uint16_t(x), uint16_t(y), uint16_t(width), uint16_t(height));
    }

    void SetViewTransform(const Matrix4& view, const Matrix4& projection)
    {
        if (!s_renderer || s_renderer->currentViewId == -1)
            return;

        bgfx::setViewTransform(s_renderer->currentViewId, view.m, projection.m);
    }

    void DrawMesh(Mesh* mesh, const Matrix4& transform)
    {
        if (s_renderer->currentViewId == -1 || !mesh || !mesh->IsValid() || !mesh->GetMaterial() || !mesh->GetMaterial()->GetShader())
            return;

        bgfx::setTransform(transform.m);
        bgfx::setVertexBuffer(0, mesh->GetVertexBuffer());
        bgfx::setIndexBuffer(mesh->GetIndexBuffer());

        uint64_t state = 0
            | BGFX_STATE_WRITE_RGB
            | BGFX_STATE_WRITE_A
            | BGFX_STATE_WRITE_Z
            | BGFX_STATE_DEPTH_TEST_LESS
            | BGFX_STATE_CULL_CW
            | BGFX_STATE_MSAA;

        // In BGFX, uniforms must be set each frame

        // Apply global uniforms
        mesh->GetMaterial()->GetShader()->ApplyUniforms();

        // Apply material specific uniforms
        mesh->GetMaterial()->ApplyShaderUniforms();

        // Apply PBR material map uniforms
        mesh->GetMaterial()->ApplyPBRUniforms();

        // Set bone transforms
        // Todo: add this

        bgfx::setState(state);
        bgfx::submit(s_renderer->currentViewId, mesh->GetMaterial()->GetShader()->GetHandle());
    }

    void DrawMesh(Mesh* mesh, const Vector3& position, const Vector3& rotation, const Vector3& scale)
    {
        if (!mesh)
            return;

        Quaternion rotQuat = Quaternion::FromEuler(rotation.y, rotation.x, rotation.z);
        Matrix4 transform = Matrix4::Translate(position) * rotQuat.ToMatrix() * Matrix4::Scale(scale);

        // Todo: Allow this to be used in batch calls. Potentially use bgfx::setInstanceDataBuffer(). Maybe can create like a Batch class/struct with a DrawBatch() function
        DrawMesh(mesh, transform);
    }

    void DrawModel(Model* model)
    {
        if (!model)
            return;

        model->UpdateTransformMatrix();

        // Set Bone Matrices uniform
        if (model->HasSkeleton())
        {
            Skeleton* skeleton = model->GetSkeleton();
            skeleton->UpdateFinalMatrices(); // Todo: Stop calculating this in each frame. Can use a dirty flag which when animating, set it to dirty so it updates every frame

            const auto& finalMats = skeleton->finalMatrices;
            std::vector<float> boneMatrixData;
            boneMatrixData.reserve(finalMats.size() * 12);

            for (const auto& m : finalMats)
            {
                boneMatrixData.insert(boneMatrixData.end(), {
                    m.m[0], m.m[1], m.m[2], m.m[3],
                    m.m[4], m.m[5], m.m[6], m.m[7],
                    m.m[8], m.m[9], m.m[10], m.m[11]
                });
            }

            bgfx::setUniform(u_BoneMatrices, boneMatrixData.data(), (uint16_t)(finalMats.size() * 3));
        }

        // Todo: Fix this to do a single batch call. Potentially use bgfx::setInstanceDataBuffer(). Maybe can create like a Batch class/struct with a DrawBatch() function
        for (const auto& mesh : model->GetMeshes())
            DrawMesh(mesh.get(), model->GetTransformMatrix());
    }

    void DrawModel(Model* model, const Vector3& position, const Vector3& rotation, const Vector3& scale)
    {
        if (!model || !model->HasMeshes())
            return;

        Quaternion rotQuat = Quaternion::FromEuler(rotation.y, rotation.x, rotation.z);
        Matrix4 transform = Matrix4::Translate(position) * rotQuat.ToMatrix() * Matrix4::Scale(scale);

        // Todo: Fix this to do a single batch call. Potentially use bgfx::setInstanceDataBuffer(). Maybe can create like a Batch class/struct with a DrawBatch() function
        for (const auto& mesh : model->GetMeshes())
            DrawMesh(mesh.get(), transform);
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
}