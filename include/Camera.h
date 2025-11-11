#pragma once

#include "Maths.h"
#include <cstdint>

namespace cl
{
    enum CameraMode
    {
        CAMERA_PERSPECTIVE,
        CAMERA_ORTHOGRAPHIC
    };

    class Camera
    {
        friend class Camera2D;
    public:
        Camera();
        Camera(const Vector3& position, const Vector3& rotation, const Vector3& up, bool useTarget);
        ~Camera();

        // Camera setup
        void SetPosition(const Vector3& position);
        void SetPosition(float x, float y, float z);
        const Vector3& GetPosition() const { return m_position; }
        Vector3& GetPosition() { return m_position; }

        // Target-based orientation
        void SetTarget(const Vector3& target);
        void SetTarget(float x, float y, float z);
        const Vector3& GetTarget() const { return m_target; }
        Vector3& GetTarget() { return m_target; }

        void SetUp(const Vector3& up);
        const Vector3& GetUp() const { return m_up; }

        // Rotation-based orientation
        void SetRotation(const Vector3& eulerAngles); // In degrees (pitch, yaw, roll)
        void SetRotation(const Quaternion& rotation);
        Vector3 GetRotation() const; // Returns Euler angles in degrees
        Quaternion GetRotationQuaternion() const { return m_rotation; }

        void Rotate(const Vector3& eulerAngles); // Rotate by euler angles in degrees
        void RotateAroundTarget(float horizontal, float vertical); // Orbit camera around target

        // Perspective settings
        void SetFOV(float fov);
        float GetFOV() const { return m_fov; }

        void SetAspectRatio(float aspect);
        float GetAspectRatio() const { return m_aspectRatio; }

        void SetNearPlane(float near);
        float GetNearPlane() const { return m_nearPlane; }

        void SetFarPlane(float far);
        float GetFarPlane() const { return m_farPlane; }

        // Orthographic settings
        void SetOrthographicSize(float size);
        float GetOrthographicSize() const { return m_orthoSize; }

        // Camera mode
        void SetMode(CameraMode mode);
        CameraMode GetMode() const { return m_mode; }

        // Movement
        void Move(const Vector3& offset);
        void Move(const Vector3& direction, float distance);
        void MoveForward(float distance);
        void MoveBackward(float distance);
        void MoveLeft(float distance);
        void MoveRight(float distance);
        void MoveUp(float distance);
        void MoveDown(float distance);

        // Look at
        void LookAt(const Vector3& target);
        void LookAt(float x, float y, float z);

        // Get camera vectors
        Vector3 GetForward() const;
        Vector3 GetRight() const;
        Vector3 GetWorldUp() const { return m_worldUp; }

        // Matrix access
        const Matrix4& GetViewMatrix();
        const Matrix4& GetProjectionMatrix();
        Matrix4 GetViewProjectionMatrix();

        // Apply camera to renderer
        void Begin();

        // Screen/World conversions
        Vector3 ScreenToWorld(const Vector2& screenPos, float depth) const;
        Vector2 WorldToScreen(const Vector3& worldPos) const;

        // Distance from target
        void SetDistanceFromTarget(float distance);
        float GetDistanceFromTarget() const;

        // Reset
        void Reset();

        uint16_t GetId();
    private:
        // Transform
        Vector3 m_position;
        Vector3 m_up;
        Vector3 m_worldUp;

        // Orientation
        Vector3 m_target;
        Quaternion m_rotation;
        bool m_useTarget;

        // Perspective settings
        float m_fov;
        float m_aspectRatio;
        float m_nearPlane;
        float m_farPlane;

        // Orthographic settings
        float m_orthoSize;

        // Camera mode
        CameraMode m_mode;

        // Cached matrices
        Matrix4 m_viewMatrix;
        Matrix4 m_projectionMatrix;
        bool m_viewDirty;
        bool m_projectionDirty;

        uint16_t m_id;
        static uint16_t s_lastId;

        // Internal methods
        void UpdateViewMatrix();
        void UpdateProjectionMatrix();
        void MarkViewDirty() { m_viewDirty = true; }
        void MarkProjectionDirty() { m_projectionDirty = true; }
        Vector3 CalculateForward() const;
        Vector3 CalculateRight() const;
    };

    void BeginCamera(Camera& camera);
}