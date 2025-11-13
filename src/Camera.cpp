#include "Camera.h"
#include "Renderer.h"

namespace cl
{
    uint16_t Camera::s_lastId = 0;

    Camera::Camera()
        : m_position(0.0f, 0.0f, 10.0f)
        , m_up(0.0f, 1.0f, 0.0f)
        , m_worldUp(0.0f, 1.0f, 0.0f)
        , m_target(0.0f, 0.0f, 0.0f)
        , m_rotation(0.0f, 0.0f, 0.0f, 1.0f)
        , m_useTarget(false)
        , m_fov(60.0f)
        , m_aspectRatio(16.0f / 9.0f)
        , m_nearPlane(0.1f)
        , m_farPlane(1000.0f)
        , m_orthoSize(10.0f)
        , m_mode(CAMERA_PERSPECTIVE)
        , m_viewDirty(true)
        , m_projectionDirty(true)
    {
        m_id = s_lastId + 1;
        s_lastId = m_id;
    }

    Camera::Camera(const Vector3& position, const Vector3& rotation, const Vector3& up, bool useTarget)
        : m_position(position)
        , m_up(up)
        , m_worldUp(up)
        , m_target({ 0.0f, 0.0f, 0.0f })
        , m_rotation(0.0f, 0.0f, 0.0f, 1.0f)
        , m_useTarget(useTarget)
        , m_fov(60.0f)
        , m_aspectRatio(16.0f / 9.0f)
        , m_nearPlane(0.1f)
        , m_farPlane(1000.0f)
        , m_orthoSize(10.0f)
        , m_mode(CAMERA_PERSPECTIVE)
        , m_viewDirty(true)
        , m_projectionDirty(true)
    {
        if (m_useTarget)
            SetTarget(rotation);
        else
            SetRotation(rotation);

        m_id = s_lastId + 1;
        s_lastId = m_id;
    }


    Camera::~Camera()
    {
    }

    void Camera::SetPosition(const Vector3& position)
    {
        m_position = position;
        MarkViewDirty();
    }

    void Camera::SetPosition(float x, float y, float z)
    {
        m_position = Vector3(x, y, z);
        MarkViewDirty();
    }

    void Camera::SetTarget(const Vector3& target)
    {
        m_target = target;
        m_useTarget = true;
        MarkViewDirty();
    }

    void Camera::SetTarget(float x, float y, float z)
    {
        m_target = Vector3(x, y, z);
        m_useTarget = true;
        MarkViewDirty();
    }

    void Camera::SetUp(const Vector3& up)
    {
        m_up = up;
        m_worldUp = up;
        MarkViewDirty();
    }

    void Camera::SetRotation(const Vector3& eulerAngles)
    {
        // Convert euler angles (in degrees) to quaternion
        m_rotation = Quaternion::FromEuler(eulerAngles.y, eulerAngles.x, eulerAngles.z);
        m_useTarget = false;
        MarkViewDirty();
    }

    void Camera::SetRotation(const Quaternion& rotation)
    {
        m_rotation = rotation;
        m_useTarget = false;
        MarkViewDirty();
    }

    Vector3 Camera::GetRotation() const
    {
        // Convert quaternion to euler angles (in degrees)
        Vector3 euler = m_rotation.ToEuler();
        return Vector3(euler.y, euler.x, euler.z); // Return as (pitch, yaw, roll)
    }

    void Camera::Rotate(const Vector3& eulerAngles)
    {
        // Apply incremental rotation
        Quaternion deltaRot = Quaternion::FromEuler(eulerAngles.y, eulerAngles.x, eulerAngles.z);
        m_rotation = m_rotation * deltaRot;
        m_useTarget = false;
        MarkViewDirty();
    }

    void Camera::RotateAroundTarget(float horizontal, float vertical)
    {
        if (!m_useTarget)
            return;

        float distance = (m_position - m_target).Length();

        // Get current direction from target to camera
        Vector3 direction = (m_position - m_target).Normalize();

        // Convert to spherical coordinates
        float theta = std::atan2(direction.x, direction.z);
        float phi = std::acos(direction.y);

        // Apply rotation
        theta += horizontal * PI / 180.0f;
        phi += vertical * PI / 180.0f;

        // Clamp phi to avoid gimbal lock
        const float epsilon = 0.001f;
        phi = std::max(epsilon, std::min(PI - epsilon, phi));

        // Convert back to cartesian
        direction.x = std::sin(phi) * std::sin(theta);
        direction.y = std::cos(phi);
        direction.z = std::sin(phi) * std::cos(theta);

        m_position = m_target + direction * distance;
        MarkViewDirty();
    }

    void Camera::SetFOV(float fov)
    {
        m_fov = fov;
        MarkProjectionDirty();
    }

    void Camera::SetAspectRatio(float aspect)
    {
        m_aspectRatio = aspect;
        MarkProjectionDirty();
    }

    void Camera::SetNearPlane(float near)
    {
        m_nearPlane = near;
        MarkProjectionDirty();
    }

    void Camera::SetFarPlane(float far)
    {
        m_farPlane = far;
        MarkProjectionDirty();
    }

    void Camera::SetOrthographicSize(float size)
    {
        m_orthoSize = size;
        MarkProjectionDirty();
    }

    void Camera::SetMode(CameraMode mode)
    {
        m_mode = mode;
        MarkProjectionDirty();
    }

    void Camera::Move(const Vector3& offset)
    {
        m_position = m_position + offset;
        MarkViewDirty();
    }

    void Camera::Move(const Vector3& direction, float distance)
    {
        Vector3 movement = GetForward() * direction.z + GetRight() * direction.x + m_worldUp * direction.y;
        m_position += movement.Normalize() * distance;

        MarkViewDirty();
    }

    void Camera::MoveForward(float distance)
    {
        Move({ 0.0f, 0.0f, -1.0f }, distance);
    }

    void Camera::MoveBackward(float distance)
    {
        Move({ 0.0f, 0.0f, 1.0f }, distance);
    }

    void Camera::MoveLeft(float distance)
    {
        Move({ 1.0f, 0.0f, 0.0f }, distance);
    }

    void Camera::MoveRight(float distance)
    {
        Move({ -1.0f, 0.0f, 0.0f }, distance);
    }

    void Camera::MoveUp(float distance)
    {
        Move({ 0.0f, 1.0f, 0.0f }, distance);
    }

    void Camera::MoveDown(float distance)
    {
        Move({ 0.0f, -1.0f, 0.0f }, distance);
    }

    void Camera::LookAt(const Vector3& target)
    {
        m_target = target;
        m_useTarget = true;
        MarkViewDirty();
    }

    void Camera::LookAt(float x, float y, float z)
    {
        m_target = Vector3(x, y, z);
        m_useTarget = true;
        MarkViewDirty();
    }

    Vector3 Camera::GetForward() const
    {
        return CalculateForward();
    }

    Vector3 Camera::GetRight() const
    {
        return CalculateRight();
    }

    const Matrix4& Camera::GetViewMatrix()
    {
        if (m_viewDirty)
            UpdateViewMatrix();

        return m_viewMatrix;
    }

    const Matrix4& Camera::GetProjectionMatrix()
    {
        if (m_projectionDirty)
            UpdateProjectionMatrix();

        return m_projectionMatrix;
    }

    Matrix4 Camera::GetViewProjectionMatrix()
    {
        return GetProjectionMatrix() * GetViewMatrix();
    }

    void Camera::Begin()
    {
        s_renderer->currentViewId = m_id;
        bgfx::setViewRect(m_id, 0, 0, uint16_t(s_renderer->width), uint16_t(s_renderer->height));
        bgfx::setViewClear(m_id, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, s_renderer->clearColor, s_renderer->clearDepth, 0);
        SetViewTransform(GetViewMatrix(), GetProjectionMatrix());
        bgfx::touch(m_id);
    }

    Vector3 Camera::ScreenToWorld(const Vector2& screenPos, float depth) const
    {
        int width = GetViewWidth();
        int height = GetViewHeight();

        // Convert screen coordinates to NDC
        float ndcX = (2.0f * screenPos.x) / width - 1.0f;
        float ndcY = 1.0f - (2.0f * screenPos.y) / height;
        float ndcZ = depth; // * 2.0f - 1.0f // Todo: This isnt correct for OpenGL

        Vector4 clipCoords(ndcX, ndcY, ndcZ, 1.0f);

        // Get inverse view-projection matrix
        Matrix4 viewProj = const_cast<Camera*>(this)->GetViewProjectionMatrix();
        Matrix4 invViewProj = viewProj.Inverse();

        // Transform by inverse view-projection
        float x = invViewProj.m[0] * clipCoords.x + invViewProj.m[4] * clipCoords.y +
            invViewProj.m[8] * clipCoords.z + invViewProj.m[12] * clipCoords.w;
        float y = invViewProj.m[1] * clipCoords.x + invViewProj.m[5] * clipCoords.y +
            invViewProj.m[9] * clipCoords.z + invViewProj.m[13] * clipCoords.w;
        float z = invViewProj.m[2] * clipCoords.x + invViewProj.m[6] * clipCoords.y +
            invViewProj.m[10] * clipCoords.z + invViewProj.m[14] * clipCoords.w;
        float w = invViewProj.m[3] * clipCoords.x + invViewProj.m[7] * clipCoords.y +
            invViewProj.m[11] * clipCoords.z + invViewProj.m[15] * clipCoords.w;

        // Perspective divide
        if (w != 0.0f)
        {
            x /= w;
            y /= w;
            z /= w;
        }

        return Vector3(x, y, z);
    }

    Vector2 Camera::WorldToScreen(const Vector3& worldPos) const
    {
        // Todo: This isnt correct for Opengl
        Matrix4 viewProj = const_cast<Camera*>(this)->GetViewProjectionMatrix();

        // Transform to clip space
        float x = viewProj.m[0] * worldPos.x + viewProj.m[4] * worldPos.y + viewProj.m[8] * worldPos.z + viewProj.m[12];
        float y = viewProj.m[1] * worldPos.x + viewProj.m[5] * worldPos.y + viewProj.m[9] * worldPos.z + viewProj.m[13];
        float w = viewProj.m[3] * worldPos.x + viewProj.m[7] * worldPos.y + viewProj.m[11] * worldPos.z + viewProj.m[15];

        // Perspective divide
        float ndcX = x / w;
        float ndcY = y / w;

        // Convert NDC to screen
        int width = GetViewWidth();
        int height = GetViewHeight();

        float screenX = (ndcX + 1.0f) * 0.5f * width;
        float screenY = (1.0f - ndcY) * 0.5f * height;

        return Vector2(screenX, screenY);
    }


    void Camera::SetDistanceFromTarget(float distance)
    {
        if (!m_useTarget)
            return;

        Vector3 direction = (m_position - m_target).Normalize();
        m_position = m_target + direction * distance;
        MarkViewDirty();
    }

    float Camera::GetDistanceFromTarget() const
    {
        return (m_position - m_target).Length();
    }

    void Camera::Reset()
    {
        m_position = Vector3(0.0f, 0.0f, 10.0f);
        m_target = Vector3(0.0f, 0.0f, 0.0f);
        m_up = Vector3(0.0f, 1.0f, 0.0f);
        m_worldUp = Vector3(0.0f, 1.0f, 0.0f);
        m_rotation = Quaternion(0.0f, 0.0f, 0.0f, 1.0f);
        m_useTarget = true;
        m_fov = 60.0f;
        MarkViewDirty();
        MarkProjectionDirty();
    }

    uint16_t Camera::GetId()
    {
        return m_id;
    }

    Vector3 Camera::CalculateForward() const
    {
        if (m_useTarget)
            return (m_target - m_position).Normalize();
        else
        {
            Matrix4 rotMatrix = m_rotation.ToMatrix();
            return Vector3(-rotMatrix.m[2], -rotMatrix.m[6], -rotMatrix.m[10]).Normalize();
        }
    }


    Vector3 Camera::CalculateRight() const
    {
        return Vector3::Cross(CalculateForward(), m_worldUp).Normalize();
    }

    void Camera::UpdateViewMatrix()
    {
        Vector3 lookTarget = m_target;

        if (!m_useTarget)
            lookTarget = m_position + CalculateForward();

        m_viewMatrix = Matrix4::LookAt(m_position, lookTarget, m_up);
        m_viewDirty = false;
    }

    void Camera::UpdateProjectionMatrix()
    {
        if (m_mode == CAMERA_PERSPECTIVE)
            m_projectionMatrix = Matrix4::Perspective(m_fov, m_aspectRatio, m_nearPlane, m_farPlane);
        else
            m_projectionMatrix = Matrix4::Orthographic(m_orthoSize, m_aspectRatio, m_nearPlane, m_farPlane);

        m_projectionDirty = false;
    }

    void BeginCamera(Camera& camera)
    {
        camera.Begin();
    }
}