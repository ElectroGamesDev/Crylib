#include "Crylib.h"
#include <iostream>
// Remove
#include <bx/math.h>

int main()
{
    cl::Config config;
    config.windowTitle = "Crylib Test Window";
    config.windowWidth = 1280;
    config.windowHeight = 720;

    if (!cl::Init(config))
    {
        std::cerr << "Failed to initialize Crylib!" << std::endl;
        return -1;
    }

    cl::LoadDefaultShader("shaders/vs_default.bin", "shaders/fs_default.bin");

    cl::Model* model = cl::LoadModel("models/truck/binary/CesiumMilkTruck.glb");
    //cl::Model* model = cl::LoadModel("models/Tree.glb"); // Todo: This isnt loading/rendering properly
    //cl::Model* model = cl::LoadModel("models/Tree3.fbx");

    //model->SetPosition({0,-0.5f,0});
    model->SetRotation({0, 90, 0});
    //model->SetScale({3,3,3});

    // Camera
    cl::Camera camera(
        { 0, 0, -10 }, // Position
        { 0, 0, 0 }, // Rotation
        { 0, 1, 0}, // Up
        false); // Use Target

    // Animations
    //cl::Model* model = cl::LoadModel("models/AnimatedTriangle/AnimatedTriangle.gltf"); // Todo: Not working
    //cl::Model* model = cl::LoadModel("models/Animated Character/Character_anim.fbx"); // Todo: not working
    //model->PlayAnimationByIndex(0, true);

    //for (const auto& mesh : model->GetMeshes())
    //{
    //    if (mesh->GetMaterial())
    //        mesh->GetMaterial()->SetShader(cl::GetDefaultShader());
    //}

    //player.SetMaterial(0, bodyMaterial);
    //player.SetMaterial(1, weaponMaterial);

    //player.SetPosition(10.0f, 0.0f, 5.0f);
    //player.SetRotation(0.0f, 45.0f, 0.0f);
    //player.SetScale(1.5f);

    //cl::Model* instancedModel = cl::LoadInstance(model);


    // Lighting:
    
    // Lighting can currently be set like this:
    //float lightingControl[4] = { 1.0f, 0.0f, 0.0f, 0.0f }; // x=1.0 enables lighting
    //cl::GetDefaultShader()->SetUniform("u_LightingControl", lightingControl);

    //// Setup lighting uniforms
    //float lightDir[3] = { -0.5f, -1.0f, -0.3f };
    //pbrShader->SetUniform("u_LightDir", lightDir);

    //float lightColor[4] = { 1.0f, 1.0f, 1.0f, 3.0f }; // White light, intensity 3.0
    //pbrShader->SetUniform("u_LightColor", lightColor);

    //float ambientColor[3] = { 0.03f, 0.03f, 0.03f }; // Subtle ambient
    //pbrShader->SetUniform("u_AmbientColor", ambientColor);

    //// Setup material flags (this is automatic in Material::ApplyPBRUniforms)
    //float flags0[4] = { 1.0f, 1.0f, 0.0f, 0.0f }; // hasAlbedo, hasNormal
    //pbrShader->SetUniform("u_MaterialFlags0", flags0);

    //float flags1[4] = { 1.0f, 1.0f, 0.0f, 0.0f }; // hasMetallicRoughness, hasAO
    //pbrShader->SetUniform("u_MaterialFlags1", flags1);

    // But eventually add a way to easily setup lights (assuming the shader supports the uniforms)

    // Todo: Need to make Sounds, Music, and AudioStream return a pointer. Sounds work fine with a copy, but Music doesn't. Not too sure about AudioStream. Pointers are needed to cleanup automatically anyways
    // Sounds
    //cl::Sound shot = cl::LoadSound("sounds/test.mp3");
    //cl::PlaySound(shot);

    // Load and play music
    //cl::Music music = cl::LoadMusicStream("sounds/test.mp3");
    //cl::SetMusicLooping(music, true);
    //cl::PlayMusicStream(music);

    // 3D Audio
    //cl::SetSoundPosition(shot, 10.0f, 0.0f, 5.0f);
    //cl::SetAudioListenerPosition(0.0f, 0.0f, 0.0f);

    // Effects
    //cl::SetMusicEffect(music, cl::AUDIO_EFFECT_LOWPASS, 500.0f);

    // Old Camera setup
    //float view[16];  // View matrix
    //float proj[16];  // Projection matrix

    //// Camera position and orientation
    //bx::Vec3 cameraPos = { 0.0f, 2.0f, -5.0f };   // In front of origin
    //bx::Vec3 target = { 0.0f, 0.0f, 0.0f };    // Look at origin
    //bx::Vec3 up = { 0.0f, 1.0f, 0.0f };    // Up vector

    //bx::mtxLookAt(view, cameraPos, target, up);
    //bx::mtxProj(proj, 60.0f, float(config.windowWidth) / config.windowHeight, 0.1f, 1000.0f, bgfx::getCaps()->homogeneousDepth);

    // Create uniform handles for camera matrices (assuming Crylib exposes this)
    //static bgfx::UniformHandle u_View = bgfx::createUniform("u_View", bgfx::UniformType::Mat4);
    //static bgfx::UniformHandle u_Proj = bgfx::createUniform("u_Proj", bgfx::UniformType::Mat4);
    //static bgfx::UniformHandle u_CamPos = bgfx::createUniform("u_CameraPos", bgfx::UniformType::Vec4);

    while (!cl::ShouldClose())
    {
        cl::Update();
        cl::BeginFrame();

        // Update the camera uniforms
        
        //bgfx::setViewTransform(0, view, proj); // Now being done in Camera

        // Movement // Todo: add delta time once it's implemented
        if (cl::Input::IsKeyDown(cl::KeyCode::W))
            camera.MoveForward(0.005f);

        if (cl::Input::IsKeyDown(cl::KeyCode::A))
            camera.MoveLeft(0.005f);

        if (cl::Input::IsKeyDown(cl::KeyCode::S))
            camera.MoveBackward(0.005f);

        if (cl::Input::IsKeyDown(cl::KeyCode::D))
            camera.MoveRight(0.005f);

        // Mouse Look
        static float lookSensitivity = 0.1f;
        cl::Vector3 camRotation = camera.GetRotation();
        float deltaX = cl::Input::GetMouseDelta().x;
        float deltaY = cl::Input::GetMouseDelta().y;

        // Apply mouse deltas
        camRotation.y += deltaX * lookSensitivity;
        camRotation.x += deltaY * lookSensitivity;

        // Clamp pitch to avoid flipping
        if (camRotation.x > 89.0f) camRotation.x = 89.0f;
        if (camRotation.x < -89.0f) camRotation.x = -89.0f;

        // Apply rotation
        camera.Rotate(camRotation);

        // Rendering
        camera.Begin();

        cl::Clear(cl::Color(48, 48, 48, 255)); // Must go after camera.Begin()

        cl::GetDefaultShader()->SetUniform("u_CameraPos", { camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z });


        // Animations
        //model->UpdateAnimation(deltaTime);

        //model->Rotate(0.0f, deltaTime * 90.0f, 0.0f); // Todo: Add delta time

        cl::DrawModel(model);

        // Drawing instanced models example
        
        // Option 1. Using transform parameters
        //cl::DrawModel(model, { 3,0,0 }, { 0,0,0 }, {1,1,1});

        // Option 2. Using model instancing / cloned models
        //cl::DrawModel(instancedModel);


        // Shaders

        // shader.SetUniform() sets the shader's global uniform, across all meshes using this uniform
        // material.SetUniform() sets the shader's uniform just for that material

        cl::EndFrame();
    }

    cl::Shutdown();

    return 0;
}