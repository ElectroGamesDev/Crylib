#pragma once

#include <cmath>

namespace cl
{
    const float PI = 3.14159265359f;

    struct Vector2
    {
        float x, y;

        Vector2() : x(0), y(0) {}
        Vector2(float x, float y) : x(x), y(y) {}

        float Length() const { return std::sqrt(x * x + y * y); }
        Vector2 Normalize() const { float len = Length(); return len > 0 ? *this / len : Vector2(); }

        Vector2 operator+(const Vector2& other) const { return Vector2(x + other.x, y + other.y); }
        Vector2 operator-(const Vector2& other) const { return Vector2(x - other.x, y - other.y); }
        Vector2 operator*(float scalar) const { return Vector2(x * scalar, y * scalar); }
        Vector2 operator/(float scalar) const { return Vector2(x / scalar, y / scalar); }
        Vector2& operator+=(const Vector2& other) { x += other.x; y += other.y; return *this; }
        Vector2& operator-=(const Vector2& other) { x -= other.x; y -= other.y; return *this; }
        Vector2& operator*=(float scalar) { x *= scalar; y *= scalar; return *this; }
        Vector2& operator/=(float scalar) { x /= scalar; y /= scalar; return *this; }
        Vector2 operator-() const { return { -x, -y }; }
        bool operator==(const Vector2& other) const { return x == other.x && y == other.y; }
        bool operator!=(const Vector2& other) const { return !(*this == other); }
        friend Vector2 operator*(float scalar, const Vector2& v) { return { v.x * scalar, v.y * scalar }; }
    };

    struct Vector3
    {
        float x, y, z;

        Vector3() : x(0), y(0), z(0) {}
        Vector3(float x, float y, float z) : x(x), y(y), z(z) {}

        float Length() const { return std::sqrt(x * x + y * y + z * z); }
        Vector3 Normalize() const { float len = Length(); return len > 0 ? *this / len : Vector3(); }

        static Vector3 Cross(const Vector3& a, const Vector3& b)
        {
            return Vector3(
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            );
        }

        static float Dot(const Vector3& a, const Vector3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        Vector3 operator+(const Vector3& other) const { return Vector3(x + other.x, y + other.y, z + other.z); }
        Vector3 operator-(const Vector3& other) const { return Vector3(x - other.x, y - other.y, z - other.z); }
        Vector3 operator*(float scalar) const { return Vector3(x * scalar, y * scalar, z * scalar); }
        Vector3 operator/(float scalar) const { return Vector3(x / scalar, y / scalar, z / scalar); }
        Vector3& operator+=(const Vector3& other) { x += other.x; y += other.y; z += other.z; return *this; }
        Vector3& operator-=(const Vector3& other) { x -= other.x; y -= other.y; z -= other.z; return *this; }
        Vector3& operator*=(float scalar) { x *= scalar; y *= scalar; z *= scalar; return *this; }
        Vector3& operator/=(float scalar) { x /= scalar; y /= scalar; z /= scalar; return *this; }
        Vector3 operator-() const { return { -x, -y, -z }; }
        bool operator==(const Vector3& other) const { return x == other.x && y == other.y && z == other.z; }
        bool operator!=(const Vector3& other) const { return !(*this == other); }
        friend Vector3 operator*(float scalar, const Vector3& v) { return { v.x * scalar, v.y * scalar, v.z * scalar }; }
    };

    struct Vector4
    {
        float x, y, z, w;
        Vector4() : x(0), y(0), z(0), w(0) {}
        Vector4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    };

    struct Matrix4;

    struct Quaternion
    {
        float x, y, z, w;

        Quaternion(float x = 0.0f, float y = 0.0f, float z = 0.0f, float w = 1.0f) : x(x), y(y), z(z), w(w) {}
        Quaternion Normalize() const;

        static Quaternion FromAxisAngle(const Vector3& axis, float degrees);
        static Quaternion FromEuler(float yaw, float pitch, float roll);
        static Quaternion Slerp(const Quaternion& a, const Quaternion& b, float t);
        Vector3 ToEuler() const;
        Matrix4 ToMatrix() const;

        Quaternion operator*(const Quaternion& other) const;
        Quaternion& operator*=(const Quaternion& q) {
            *this = *this * q;
            return *this;
        }
        bool operator==(const Quaternion& other) const { return x == other.x && y == other.y && z == other.z && w == other.w; }
        bool operator!=(const Quaternion& other) const { return !(*this == other); }
        Quaternion operator-() const { return { -x, -y, -z, -w }; }
    };

    struct Matrix4
    {
        float m[16];

        Matrix4()
        {
            for (int i = 0; i < 16; i++)
                m[i] = 0;

            m[0] = m[5] = m[10] = m[15] = 1.0f; // Identity
        }

        static Matrix4 Identity()
        {
            return Matrix4();
        }

        static Matrix4 Perspective(float fov, float aspect, float nearPlane, float farPlane);
        static Matrix4 Orthographic(float size, float aspectRatio, float nearPlane, float farPlane);
        static Matrix4 LookAt(const Vector3& eye, const Vector3& target, const Vector3& up);
        static Matrix4 Translate(const Vector3& translation);
        static Matrix4 Rotate(float angle, const Vector3& axis);
        static Matrix4 RotateYawPitchRoll(float yaw, float pitch, float roll);
        static Matrix4 RotateX(float angle);
        static Matrix4 RotateY(float angle);
        static Matrix4 RotateZ(float angle);
        static Matrix4 Scale(const Vector3& scale);
        static Matrix4 FromQuaternion(const Quaternion& q);
        Vector3 TransformPoint(const Vector3& v) const;
        Vector3 TransformDirection(const Vector3& v) const;
        Matrix4 Inverse() const;

        Matrix4 operator*(const Matrix4& other) const;
        Matrix4& operator*=(const Matrix4& other) {
            *this = *this * other;
            return *this;
        }

        bool operator==(const Matrix4& other) const
        {
            constexpr float EPSILON = 1e-6f;
            for (int i = 0; i < 16; ++i)
            {
                if (fabsf(m[i] - other.m[i]) > EPSILON)
                    return false;
            }
            return true;
        }

        bool operator!=(const Matrix4& other) const { return !(*this == other); }

    };

    struct Color
    {
        unsigned char r, g, b, a;

        Color() : r(255), g(255), b(255), a(255) {}
        Color(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
            : r(r), g(g), b(b), a(a) {
        }

        static Color White() { return Color(255, 255, 255, 255); }
        static Color Black() { return Color(0, 0, 0, 255); }
        static Color Red() { return Color(255, 0, 0, 255); }
        static Color Green() { return Color(0, 255, 0, 255); }
        static Color Blue() { return Color(0, 0, 255, 255); }

        uint32_t ToUInt32() const { return (r << 24) | (g << 16) | (b << 8) | a; }
        static Color FromUInt32(uint32_t value)
        {
            return Color((value >> 24) & 0xFF, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
        }
    };

    // Random numbers
    
    // Sets the random number generator seed to a fixed value
    void SetRandomSeed(unsigned int seed);

    // Randomizes the random number generator seed
    void RandomizeSeed();

    // Returns a random integer between min and max (inclusive)
    int GetRandomInt(int min, int max);

    // Returns a random float value between min (inclusive) and max (exclusive)
    float GetRandomFloat(float min, float max);

}