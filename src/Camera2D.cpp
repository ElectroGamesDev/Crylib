#include "Camera.h"
#include "Camera2D.h"
#include "Renderer.h"

namespace cl
{
    Camera2D::Camera2D()
        : m_position(0.0f, 0.0f)
        , m_offset(0.0f, 0.0f)
        , m_rotation(0.0f)
        , m_zoom(1.0f)
        , m_viewDirty(true)
        , m_projectionDirty(true)
    {
        m_id = Camera::s_lastId + 1;
        Camera::s_lastId = m_id;
    }

    Camera2D::Camera2D(const Vector2& position, float rotation, float zoom)
        : m_position(position)
        , m_offset(0.0f, 0.0f)
        , m_rotation(rotation)
        , m_zoom(zoom)
        , m_viewDirty(true)
        , m_projectionDirty(true)
    {
        m_id = Camera::s_lastId + 1;
        Camera::s_lastId = m_id;
    }

    Camera2D::~Camera2D()
    {
    }

    void Camera2D::SetPosition(const Vector2& position)
    {
        m_position = position;
        MarkViewDirty();
    }

    void Camera2D::SetPosition(float x, float y)
    {
        m_position = Vector2(x, y);
        MarkViewDirty();
    }

    void Camera2D::SetOffset(const Vector2& offset)
    {
        m_offset = offset;
        MarkViewDirty();
    }

    void Camera2D::SetOffset(float x, float y)
    {
        m_offset = Vector2(x, y);
        MarkViewDirty();
    }

    void Camera2D::SetRotation(float rotation)
    {
        m_rotation = rotation;
        MarkViewDirty();
    }

    void Camera2D::SetZoom(float zoom)
    {
        if (zoom <= 0.0f)
            zoom = 0.001f;

        m_zoom = zoom;
        MarkViewDirty();
    }

    void Camera2D::Move(const Vector2& delta)
    {
        m_position = m_position + delta;
        MarkViewDirty();
    }

    void Camera2D::Move(float dx, float dy)
    {
        m_position.x += dx;
        m_position.y += dy;
        MarkViewDirty();
    }

    Matrix4 Camera2D::GetViewMatrix()
    {
        if (m_viewDirty)
            UpdateViewMatrix();

        return m_viewMatrix;
    }

    Matrix4 Camera2D::GetProjectionMatrix()
    {
        if (m_projectionDirty)
            UpdateProjectionMatrix();

        return m_projectionMatrix;
    }

    Matrix4 Camera2D::GetViewProjectionMatrix()
    {
        return GetProjectionMatrix() * GetViewMatrix();
    }

    void Camera2D::Begin()
    {
        s_renderer->currentViewId = m_id;
        bgfx::setViewRect(m_id, 0, 0, uint16_t(s_renderer->width), uint16_t(s_renderer->height));
        SetViewTransform(GetViewMatrix(), GetProjectionMatrix()); // Todo: Should be be before or after touch?
        bgfx::touch(m_id);
    }

    Vector2 Camera2D::ScreenToWorld(const Vector2& screenPos) const
    {
        int width = GetViewWidth();
        int height = GetViewHeight();

        // Convert screen coordinates to normalized device coordinates (NDC)
        float ndcX = (2.0f * screenPos.x) / width - 1.0f;
        float ndcY = 1.0f - (2.0f * screenPos.y) / height;

        Vector4 clipCoords(ndcX, ndcY, 0.0f, 1.0f);

        // Get inverse view-projection matrix
        Matrix4 viewProj = const_cast<Camera2D*>(this)->GetViewProjectionMatrix();
        Matrix4 invViewProj = viewProj.Inverse();

        // Transform by inverse view-projection
        float x = invViewProj.m[0] * clipCoords.x + invViewProj.m[4] * clipCoords.y +
            invViewProj.m[8] * clipCoords.z + invViewProj.m[12] * clipCoords.w;
        float y = invViewProj.m[1] * clipCoords.x + invViewProj.m[5] * clipCoords.y +
            invViewProj.m[9] * clipCoords.z + invViewProj.m[13] * clipCoords.w;
        float w = invViewProj.m[3] * clipCoords.x + invViewProj.m[7] * clipCoords.y +
            invViewProj.m[11] * clipCoords.z + invViewProj.m[15] * clipCoords.w;

        // Perspective divide
        if (w != 0.0f)
        {
            x /= w;
            y /= w;
        }

        return Vector2(x, y);
    }

    Vector2 Camera2D::WorldToScreen(const Vector2& worldPos) const
    {
        // Transform world position to screen space
        Vector2 screenPos;
        screenPos.x = (worldPos.x - m_position.x + m_offset.x) * m_zoom;
        screenPos.y = (worldPos.y - m_position.y + m_offset.y) * m_zoom;

        return screenPos;
    }

    void Camera2D::Reset()
    {
        m_position = Vector2(0.0f, 0.0f);
        m_offset = Vector2(0.0f, 0.0f);
        m_rotation = 0.0f;
        m_zoom = 1.0f;
        MarkViewDirty();
        MarkProjectionDirty();
    }

    void Camera2D::UpdateViewMatrix()
    {
        // Build 2D view matrix
        Matrix4 matOrigin = Matrix4::Translate(Vector3(-m_offset.x, -m_offset.y, 0.0f));
        Matrix4 matRotation = Matrix4::RotateZ(-m_rotation * PI / 180.0f);
        Matrix4 matScale = Matrix4::Scale(Vector3(m_zoom, m_zoom, 1.0f));
        Matrix4 matTranslation = Matrix4::Translate(Vector3(-m_position.x, -m_position.y, 0.0f));

        m_viewMatrix = matOrigin * matRotation * matScale * matTranslation;
        m_viewDirty = false;
    }

    void Camera2D::UpdateProjectionMatrix()
    {
        // Orthographic projection for 2D rendering
        float width = (float)GetViewWidth();
        float height = (float)GetViewHeight();

        m_projectionMatrix = Matrix4::Identity();
        m_projectionMatrix.m[0] = 2.0f / width;
        m_projectionMatrix.m[5] = -2.0f / height; // Negative for top-left origin
        m_projectionMatrix.m[10] = -1.0f;
        m_projectionMatrix.m[12] = -1.0f;
        m_projectionMatrix.m[13] = 1.0f;

        m_projectionDirty = false;
    }
}