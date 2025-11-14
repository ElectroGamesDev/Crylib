#include "Maths.h"
#include <random>
#include <chrono>

namespace cl
{
    static std::mt19937 s_rng;

    Matrix4 Matrix4::Perspective(float fov, float aspect, float nearPlane, float farPlane)
    {
        // Todo: This works for DX and Vukan, but isnt correct for Opengl
        float tanHalfFov = tanf(fov * 0.5f * (PI / 180.0f));

        Matrix4 result;

        result.m[0] = 1.0f / (aspect * tanHalfFov);
        result.m[5] = 1.0f / (tanHalfFov);
        result.m[10] = farPlane / (farPlane - nearPlane);
        result.m[11] = 1.0f;
        result.m[14] = -(nearPlane * farPlane) / (farPlane - nearPlane);
        result.m[15] = 0.0f;

        return result;
    }

    Matrix4 Matrix4::Orthographic(float size, float aspectRatio, float nearPlane, float farPlane)
    {
        // Todo: This works for DX and Vukan, but isnt correct for Opengl

        Matrix4 result = {};

        float halfWidth = size * aspectRatio * 0.5f;
        float halfHeight = size * 0.5f;

        result.m[0] = 1.0f / halfWidth;
        result.m[5] = 1.0f / halfHeight;
        result.m[10] = 1.0f / (farPlane - nearPlane);
        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = -nearPlane / (farPlane - nearPlane);
        result.m[15] = 1.0f;

        return result;
    }

    Matrix4 Matrix4::LookAt(const Vector3& eye, const Vector3& target, const Vector3& up)
    {
        // Todo: This works for DX and Vukan, but isnt correct for Opengl

        Vector3 forward = (target - eye).Normalize();
        Vector3 right = Vector3::Cross(up, forward).Normalize();
        Vector3 newUp = Vector3::Cross(forward, right);

        Matrix4 result = Matrix4::Identity();

        // Column-major layout
        result.m[0] = right.x;
        result.m[1] = right.y;
        result.m[2] = right.z;
        result.m[3] = 0.0f;

        result.m[4] = newUp.x;
        result.m[5] = newUp.y;
        result.m[6] = newUp.z;
        result.m[7] = 0.0f;

        result.m[8] = -forward.x;
        result.m[9] = -forward.y;
        result.m[10] = -forward.z;
        result.m[11] = 0.0f;

        result.m[12] = -Vector3::Dot(right, eye);
        result.m[13] = -Vector3::Dot(newUp, eye);
        result.m[14] = Vector3::Dot(forward, eye);
        result.m[15] = 1.0f;

        return result;
    }


    Matrix4 Matrix4::Translate(const Vector3& translation)
    {
        Matrix4 result = Identity();
        result.m[12] = translation.x;
        result.m[13] = translation.y;
        result.m[14] = translation.z;
        return result;
    }

    Matrix4 Matrix4::Scale(const Vector3& scale)
    {
        Matrix4 result = Identity();
        result.m[0] = scale.x;
        result.m[5] = scale.y;
        result.m[10] = scale.z;
        return result;
    }

    Matrix4 Matrix4::RotateX(float angle)
    {
        float rad = angle * PI / 180.0f;
        float c = cosf(rad);
        float s = sinf(rad);

        Matrix4 result = Identity();
        result.m[5] = c;
        result.m[6] = -s;
        result.m[9] = s;
        result.m[10] = c;
        return result;
    }

    Matrix4 Matrix4::RotateY(float angle)
    {
        float rad = angle * PI / 180.0f;
        float c = cosf(rad);
        float s = sinf(rad);

        Matrix4 result = Identity();
        result.m[0] = c;
        result.m[2] = s;
        result.m[8] = -s;
        result.m[10] = c;
        return result;
    }

    Matrix4 Matrix4::RotateZ(float angle)
    {
        float rad = angle * PI / 180.0f;
        float c = cosf(rad);
        float s = sinf(rad);

        Matrix4 result = Identity();
        result.m[0] = c;
        result.m[1] = -s;
        result.m[4] = s;
        result.m[5] = c;
        return result;
    }

    Matrix4 Matrix4::RotateYawPitchRoll(float yaw, float pitch, float roll)
    {
        // Yaw (Y), Pitch (X), Roll (Z). All in degrees
        Matrix4 rotY = RotateY(yaw);
        Matrix4 rotX = RotateX(pitch);
        Matrix4 rotZ = RotateZ(roll);

        return rotY * rotX * rotZ;
    }

    Matrix4 Matrix4::Rotate(float angle, const Vector3& axis)
    {
        float rad = angle * PI / 180.0f;

        float c = std::cosf(rad);
        float s = std::sinf(rad);
        float t = 1.0f - c;

        Vector3 a = axis.Normalize();

        Matrix4 result;
        result.m[0] = t * a.x * a.x + c;
        result.m[1] = t * a.x * a.y + s * a.z;
        result.m[2] = t * a.x * a.z - s * a.y;
        result.m[3] = 0.0f;

        result.m[4] = t * a.x * a.y - s * a.z;
        result.m[5] = t * a.y * a.y + c;
        result.m[6] = t * a.y * a.z + s * a.x;
        result.m[7] = 0.0f;

        result.m[8] = t * a.x * a.z + s * a.y;
        result.m[9] = t * a.y * a.z - s * a.x;
        result.m[10] = t * a.z * a.z + c;
        result.m[11] = 0.0f;

        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[15] = 1.0f;

        return result;
    }

    Matrix4 Matrix4::FromQuaternion(const Quaternion& q)
    {
        Matrix4 result;

        float xx = q.x * q.x;
        float yy = q.y * q.y;
        float zz = q.z * q.z;
        float xy = q.x * q.y;
        float xz = q.x * q.z;
        float yz = q.y * q.z;
        float wx = q.w * q.x;
        float wy = q.w * q.y;
        float wz = q.w * q.z;

        result.m[0] = 1.0f - 2.0f * (yy + zz);
        result.m[1] = 2.0f * (xy + wz);
        result.m[2] = 2.0f * (xz - wy);
        result.m[3] = 0.0f;

        result.m[4] = 2.0f * (xy - wz);
        result.m[5] = 1.0f - 2.0f * (xx + zz);
        result.m[6] = 2.0f * (yz + wx);
        result.m[7] = 0.0f;

        result.m[8] = 2.0f * (xz + wy);
        result.m[9] = 2.0f * (yz - wx);
        result.m[10] = 1.0f - 2.0f * (xx + yy);
        result.m[11] = 0.0f;

        result.m[12] = 0.0f;
        result.m[13] = 0.0f;
        result.m[14] = 0.0f;
        result.m[15] = 1.0f;

        return result;
    }

    Vector3 Matrix4::TransformPoint(const Vector3& v) const
    {
        return Vector3(
            m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12],
            m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
            m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]
        );
    }

    Vector3 Matrix4::TransformDirection(const Vector3& v) const
    {
        return Vector3(
            m[0] * v.x + m[4] * v.y + m[8] * v.z,
            m[1] * v.x + m[5] * v.y + m[9] * v.z,
            m[2] * v.x + m[6] * v.y + m[10] * v.z
        );
    }

    Matrix4 Matrix4::operator*(const Matrix4& other) const
    {
        Matrix4 result;

        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k)
                    sum += m[k * 4 + row] * other.m[col * 4 + k];

                result.m[col * 4 + row] = sum;
            }
        }

        return result;
    }

    Quaternion Quaternion::Normalize() const
    {
        float len = std::sqrtf(x * x + y * y + z * z + w * w);
        if (len > 1e-6f)
            return Quaternion(x / len, y / len, z / len, w / len);

        return Quaternion(0, 0, 0, 1);
    }

    Quaternion Quaternion::operator*(const Quaternion& other) const
    {
        return Quaternion(
            w * other.x + x * other.w + y * other.z - z * other.y,
            w * other.y - x * other.z + y * other.w + z * other.x,
            w * other.z + x * other.y - y * other.x + z * other.w,
            w * other.w - x * other.x - y * other.y - z * other.z
        );
    }

    Quaternion Quaternion::FromAxisAngle(const Vector3& axis, float degrees)
    {
        float rad = degrees * PI / 180.0f * 0.5f;
        float s = std::sinf(rad);
        Vector3 a = axis.Normalize();
        return Quaternion(a.x * s, a.y * s, a.z * s, std::cosf(rad));
    }

    Quaternion Quaternion::FromEuler(float yaw, float pitch, float roll)
    {
        Quaternion qY = FromAxisAngle(Vector3(0, 1, 0), yaw);
        Quaternion qX = FromAxisAngle(Vector3(1, 0, 0), pitch);
        Quaternion qZ = FromAxisAngle(Vector3(0, 0, 1), roll);
        return qY * qX * qZ;
    }

    Quaternion Quaternion::Slerp(const Quaternion& a, const Quaternion& b, float t)
    {
        Quaternion result;

        float cosHalfTheta = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;

        Quaternion b_adjusted = b;
        if (cosHalfTheta < 0.0f)
        {
            b_adjusted.x = -b.x;
            b_adjusted.y = -b.y;
            b_adjusted.z = -b.z;
            b_adjusted.w = -b.w;
            cosHalfTheta = -cosHalfTheta;
        }

        if (cosHalfTheta > 0.9995f)
        {
            result.x = a.x + (b_adjusted.x - a.x) * t;
            result.y = a.y + (b_adjusted.y - a.y) * t;
            result.z = a.z + (b_adjusted.z - a.z) * t;
            result.w = a.w + (b_adjusted.w - a.w) * t;
            return result.Normalize();
        }

        float halfTheta = acosf(cosHalfTheta);
        float sinHalfTheta = sqrtf(1.0f - cosHalfTheta * cosHalfTheta);

        if (fabsf(sinHalfTheta) < 0.001f)
        {
            result.x = (a.x + b_adjusted.x) * 0.5f;
            result.y = (a.y + b_adjusted.y) * 0.5f;
            result.z = (a.z + b_adjusted.z) * 0.5f;
            result.w = (a.w + b_adjusted.w) * 0.5f;
            return result;
        }

        float ratioA = sinf((1.0f - t) * halfTheta) / sinHalfTheta;
        float ratioB = sinf(t * halfTheta) / sinHalfTheta;

        result.x = a.x * ratioA + b_adjusted.x * ratioB;
        result.y = a.y * ratioA + b_adjusted.y * ratioB;
        result.z = a.z * ratioA + b_adjusted.z * ratioB;
        result.w = a.w * ratioA + b_adjusted.w * ratioB;

        return result;
    }

    Vector3 Quaternion::ToEuler() const
    {
        Matrix4 mat = ToMatrix();
        float r32 = mat.m[6];
        float phi_rad; // roll (z)
        float theta_rad; // pitch (x)
        float psi_rad; // yaw (y)
        float eps = 1e-6f;
        float cos_phi = std::sqrtf(1.0f - r32 * r32);
        if (cos_phi < eps)
        {
            phi_rad = (PI / 2.0f) * (r32 > 0.0f ? 1.0f : -1.0f);
            theta_rad = 0.0f; // Arbitrary
            if (r32 > 0.0f)
                psi_rad = std::atan2f(mat.m[1], mat.m[0]) - theta_rad;
            else
                psi_rad = std::atan2f(mat.m[1], mat.m[0]) + theta_rad;
        }
        else
        {
            phi_rad = std::asinf(r32);
            theta_rad = std::atan2f(-mat.m[2] / cos_phi, mat.m[10] / cos_phi);
            psi_rad = std::atan2f(-mat.m[4] / cos_phi, mat.m[5] / cos_phi);
        }
        return Vector3(
            theta_rad * 180.0f / PI, // pitch (x)
            psi_rad * 180.0f / PI, // yaw (y)
            phi_rad * 180.0f / PI // roll (z)
        );
    }

    Matrix4 Quaternion::ToMatrix() const
    {
        Quaternion q = Normalize();
        float xx = q.x * q.x;
        float xy = q.x * q.y;
        float xz = q.x * q.z;
        float xw = q.x * q.w;
        float yy = q.y * q.y;
        float yz = q.y * q.z;
        float yw = q.y * q.w;
        float zz = q.z * q.z;
        float zw = q.z * q.w;

        Matrix4 result = Matrix4::Identity();
        result.m[0] = 1 - 2 * (yy + zz);
        result.m[1] = 2 * (xy + zw);
        result.m[2] = 2 * (xz - yw);
        result.m[4] = 2 * (xy - zw);
        result.m[5] = 1 - 2 * (xx + zz);
        result.m[6] = 2 * (yz + xw);
        result.m[8] = 2 * (xz + yw);
        result.m[9] = 2 * (yz - xw);
        result.m[10] = 1 - 2 * (xx + yy);

        return result;
    }

    Quaternion Quaternion::FromMatrix(const Matrix4& m)
    {
        Quaternion q;
        float trace = m.m[0] + m.m[5] + m.m[10];

        if (trace > 0.0f)
        {
            float s = sqrtf(trace + 1.0f) * 2.0f;
            q.w = 0.25f * s;
            q.x = (m.m[9] - m.m[6]) / s;
            q.y = (m.m[2] - m.m[8]) / s;
            q.z = (m.m[4] - m.m[1]) / s;
        }
        else if ((m.m[0] > m.m[5]) && (m.m[0] > m.m[10]))
        {
            float s = sqrtf(1.0f + m.m[0] - m.m[5] - m.m[10]) * 2.0f;
            q.w = (m.m[9] - m.m[6]) / s;
            q.x = 0.25f * s;
            q.y = (m.m[1] + m.m[4]) / s;
            q.z = (m.m[2] + m.m[8]) / s;
        }
        else if (m.m[5] > m.m[10])
        {
            float s = sqrtf(1.0f + m.m[5] - m.m[0] - m.m[10]) * 2.0f;
            q.w = (m.m[2] - m.m[8]) / s;
            q.x = (m.m[1] + m.m[4]) / s;
            q.y = 0.25f * s;
            q.z = (m.m[6] + m.m[9]) / s;
        }
        else
        {
            float s = sqrtf(1.0f + m.m[10] - m.m[0] - m.m[5]) * 2.0f;
            q.w = (m.m[4] - m.m[1]) / s;
            q.x = (m.m[2] + m.m[8]) / s;
            q.y = (m.m[6] + m.m[9]) / s;
            q.z = 0.25f * s;
        }

        return q.Normalize();
    }

    Matrix4 Matrix4::Inverse() const
    {
        Matrix4 inv;
        float det;

        inv.m[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
            m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];

        inv.m[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
            m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

        inv.m[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
            m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];

        inv.m[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

        inv.m[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
            m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

        inv.m[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
            m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];

        inv.m[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
            m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];

        inv.m[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

        inv.m[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
            m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];

        inv.m[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
            m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];

        inv.m[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];

        inv.m[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

        inv.m[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
            m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

        inv.m[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
            m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

        inv.m[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

        inv.m[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        det = m[0] * inv.m[0] + m[1] * inv.m[4] + m[2] * inv.m[8] + m[3] * inv.m[12];

        if (det == 0.0f)
            return Matrix4::Identity();

        det = 1.0f / det;

        for (int i = 0; i < 16; i++)
            inv.m[i] = inv.m[i] * det;

        return inv;
    }

    Vector3 Matrix4::GetTranslation() const
    {
        return Vector3(m[12], m[13], m[14]);
    }

    Quaternion Matrix4::GetRotation() const
    {
        Vector3 col0(m[0], m[1], m[2]);
        Vector3 col1(m[4], m[5], m[6]);
        Vector3 col2(m[8], m[9], m[10]);

        float len0 = col0.Length();
        float len1 = col1.Length();
        float len2 = col2.Length();

        if (len0 < 0.0001f || len1 < 0.0001f || len2 < 0.0001f)
            return Quaternion(0, 0, 0, 1);

        Matrix4 rotMat;
        rotMat.m[0] = col0.x / len0;
        rotMat.m[1] = col0.y / len0;
        rotMat.m[2] = col0.z / len0;
        rotMat.m[3] = 0;

        rotMat.m[4] = col1.x / len1;
        rotMat.m[5] = col1.y / len1;
        rotMat.m[6] = col1.z / len1;
        rotMat.m[7] = 0;

        rotMat.m[8] = col2.x / len2;
        rotMat.m[9] = col2.y / len2;
        rotMat.m[10] = col2.z / len2;
        rotMat.m[11] = 0;

        rotMat.m[12] = 0;
        rotMat.m[13] = 0;
        rotMat.m[14] = 0;
        rotMat.m[15] = 1;

        return Quaternion::FromMatrix(rotMat);
    }

    Vector3 Matrix4::GetScale() const
    {
        Vector3 colX(m[0], m[1], m[2]);
        Vector3 colY(m[4], m[5], m[6]);
        Vector3 colZ(m[8], m[9], m[10]);
        return Vector3(colX.Length(), colY.Length(), colZ.Length());
    }

    Matrix4 Matrix4::Transpose() const
    {
        Matrix4 result;
        result.m[0] = m[0];
        result.m[1] = m[4];
        result.m[2] = m[8];
        result.m[3] = m[12];
        result.m[4] = m[1];
        result.m[5] = m[5];
        result.m[6] = m[9];
        result.m[7] = m[13];
        result.m[8] = m[2];
        result.m[9] = m[6];
        result.m[10] = m[10];
        result.m[11] = m[14];
        result.m[12] = m[3];
        result.m[13] = m[7];
        result.m[14] = m[11];
        result.m[15] = m[15];
        return result;
    }

    // Random numbers

    void SetRandomSeed(unsigned int seed)
    {
        s_rng.seed(seed);
    }

    void RandomizeSeed()
    {
        auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        unsigned int seed = static_cast<unsigned int>(now);
        s_rng.seed(seed);
    }

    int GetRandomInt(int min, int max)
    {
        std::uniform_int_distribution<int> dist(min, max);
        return dist(s_rng);
    }

    float GetRandomFloat(float min, float max)
    {
        std::uniform_real_distribution<float> dist(min, max);
        return dist(s_rng);
    }
}