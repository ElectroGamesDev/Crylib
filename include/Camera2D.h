#pragma once

#include "Maths.h"

namespace cl
{
    class Camera2D
    {
    public:
        Camera2D();
        Camera2D(const Vector2& position, float rotation = 0.0f, float zoom = 1.0f);
        ~Camera2D();

        // Position
        void SetPosition(const Vector2& position);
        void SetPosition(float x, float y);
        const Vector2& GetPosition() const { return m_position; }
        Vector2& GetPosition() { return m_position; }

        // Offset (target position on screen)
        void SetOffset(const Vector2& offset);
        void SetOffset(float x, float y);
        const Vector2& GetOffset() const { return m_offset; }

        // Rotation (in degrees)
        void SetRotation(float rotation);
        float GetRotation() const { return m_rotation; }

        // Zoom
        void SetZoom(float zoom);
        float GetZoom() const { return m_zoom; }

        // Movement
        void Move(const Vector2& delta);
        void Move(float dx, float dy);

        // Matrix access
        Matrix4 GetViewMatrix();
        Matrix4 GetProjectionMatrix();
        Matrix4 GetViewProjectionMatrix();

        // Apply camera to renderer
        void Begin();

        // Screen/World conversions
        Vector2 ScreenToWorld(const Vector2& screenPos) const;
        Vector2 WorldToScreen(const Vector2& worldPos) const;

        // Reset
        void Reset();

        uint16_t GetId();
    private:
        Vector2 m_position;
        Vector2 m_offset;
        float m_rotation;
        float m_zoom;
        uint16_t m_id;

        // Cached matrices
        Matrix4 m_viewMatrix;
        Matrix4 m_projectionMatrix;
        bool m_viewDirty;
        bool m_projectionDirty;

        void UpdateViewMatrix();
        void UpdateProjectionMatrix();
        void MarkViewDirty() { m_viewDirty = true; }
        void MarkProjectionDirty() { m_projectionDirty = true; }
    };

    void BeginCamera(Camera2D& camera);
}