//
// Created by Hoppz on 25-12-8.
//

#ifndef LIBZDS_UTILS_TYPE_H
#define LIBZDS_UTILS_TYPE_H
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "libzds/utils/status.h"

namespace libzds{

struct Vector3D{
    float x{0};
    float y{0};
    float z{0};

    // --- 构造 ---
    constexpr Vector3D() = default;
    constexpr Vector3D(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr Vector3D(const Vector3D& other)
        : x(other.x), y(other.y), z(other.z) {}

    Vector3D& operator=(const Vector3D& other) {
        if (this != &other) {
            x = other.x;
            y = other.y;
            z = other.z;
        }
        return *this;
    }

    // --- 基本运算 ---
    inline Vector3D operator+(const Vector3D& rhs) const {
        return { x + rhs.x, y + rhs.y, z + rhs.z };
    }

    inline Vector3D operator-(const Vector3D& rhs) const {
        return { x - rhs.x, y - rhs.y, z - rhs.z };
    }

    inline Vector3D operator*(float s) const {
        return { x * s, y * s, z * s };
    }

    inline Vector3D operator/(float s) const {
        float inv = 1.0f / s;
        return { x * inv, y * inv, z * inv };
    }

    inline Vector3D& operator+=(const Vector3D& rhs) {
        x += rhs.x; y += rhs.y; z += rhs.z; return *this;
    }

    inline Vector3D& operator-=(const Vector3D& rhs) {
        x -= rhs.x; y -= rhs.y; z -= rhs.z; return *this;
    }

    inline Vector3D& operator*=(float s) {
        x *= s; y *= s; z *= s; return *this;
    }

    inline bool operator== (const Vector3D& rhs) const
    {
        return (rhs.x == x && rhs.y == y && rhs.z == z);
    }

    // --- 数学函数 ---
    inline float norm2() const {
        return dot(*this);
    }

    inline float norm() const {
        return std::sqrt(norm2());
    }

    inline Vector3D normalized() const {
        float len = norm();
        if (len <= 1e-6f) return {};
        return *this / len;
    }

    inline float dot(const Vector3D& rhs) const
    {
        return x * rhs.x + y * rhs.y + z * rhs.z;
    }

    static inline Vector3D maxAll(const Vector3D& lhs, const Vector3D& rhs)
    {
        return Vector3D{
            std::max(lhs.x, rhs.x),
            std::max(lhs.y, rhs.y),
            std::max(lhs.z, rhs.z)
        };
    }

    static inline Vector3D minAll(const Vector3D& lhs, const Vector3D& rhs)
    {
        return Vector3D{
            std::min(lhs.x, rhs.x),
            std::min(lhs.y, rhs.y),
            std::min(lhs.z, rhs.z)
        };
    }
};

struct Vector4D{
    float x{0};
    float y{0};
    float z{0};
    float w{0};

    /// --------------------- 构造
    constexpr Vector4D() = default;
    constexpr Vector4D(float _x, float _y, float _z, float _w)
        : x(_x), y(_y), z(_z), w(_w) {}

    constexpr Vector4D(const Vector4D& other)
        : x(other.x), y(other.y), z(other.z), w(other.w) {}

    Vector4D& operator=(const Vector4D& other) {
        if (this != &other) {
            x = other.x;y = other.y;z = other.z;w = other.w;
        }
        return *this;
    }

    inline Vector4D operator+(const Vector4D& rhs) const
    {
        return {x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w};
    }

    inline Vector4D operator-(const Vector4D& rhs) const
    {
        return {x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w};
    }

    inline Vector4D& operator+=(const Vector4D& rhs)
    {
        x += rhs.x; y += rhs.y; z += rhs.z; w += rhs.w;
        return *this;
    }

    inline Vector4D& operator-=(const Vector4D& rhs)
    {
        x -= rhs.x; y -= rhs.y; z -= rhs.z; w -= rhs.w;
        return *this;
    }

    inline Vector4D operator*(float s) const
    {
        return {x * s, y * s, z * s, w * s};
    }

    inline Vector4D operator/(float s) const
    {
        const float inv = 1.0f / s;
        return {x * inv, y * inv, z * inv, w * inv};
    }

    inline Vector4D& operator*=(float s)
    {
        x *= s; y *= s; z *= s; w *= s;
        return *this;
    }

    inline Vector4D& operator/=(float s)
    {
        const float inv = 1.0f / s;
        x *= inv; y *= inv; z *= inv; w *= inv;
        return *this;
    }

    inline float dot(const Vector4D& rhs) const
    {
        return x * rhs.x + y * rhs.y + z * rhs.z + w * rhs.w;
    }

    inline bool operator== (const Vector4D& rhs) const
    {
        return (rhs.x == x && y == rhs.y && z == rhs.z && w == rhs.w);
    }

    static inline Vector4D maxAll(const Vector4D& lhs, const Vector4D& rhs)
    {
        return {
            std::max(lhs.x, rhs.x),
            std::max(lhs.y, rhs.y),
            std::max(lhs.z, rhs.z),
            std::max(lhs.w, rhs.w)
        };
    }

    static inline Vector4D minAll(const Vector4D& lhs, const Vector4D& rhs)
    {
        return {
            std::min(lhs.x, rhs.x),
            std::min(lhs.y, rhs.y),
            std::min(lhs.z, rhs.z),
            std::min(lhs.w, rhs.w)
        };
    }
};

#define MAX_POLY_COEFFS 8

struct Poly
{
    float coeffs[MAX_POLY_COEFFS];

    ///< Zero means an all-zero polynomial, 1 means a constant term, 2
    ///<  means a linear segment, 3 means a quadratic polynomial and so on.
    uint8_t numCoeffs;

    Poly() {clear();}

    inline void clear()
    {
        std::memset(coeffs, 0, sizeof(coeffs));
        numCoeffs = 0;
    }

    inline float eval(float t) const
    {
        float result = 0.0f;
        const float* ptr = coeffs + numCoeffs;
        while (ptr > coeffs) {
            result = result * t + *(--ptr);
        }
        return result;
    }

    inline Poly derivative() const
    {
        Poly out;
        if (numCoeffs <= 1) {
            out.clear();
            return out;
        }

        out.numCoeffs = numCoeffs - 1;
        for (uint8_t i = 1; i < numCoeffs; ++i) {
            out.coeffs[i-1] = coeffs[i] * float(i);
        }
        return out;
    }

    inline Status polyMaxMin(float duration, float& maxVal, float& minVal)
    {
        if ( numCoeffs == 0 ) {
            maxVal = minVal = 0.f;
            return Status::OK();
        }
        if ( numCoeffs == 1 ) {
            maxVal = minVal = coeffs[0];
            return Status::OK();
        }

        Poly dp = derivative();
        minVal = maxVal = eval(0.f);

        // 检查区间右端点
        float endVal = eval(duration);
        minVal = std::min(minVal, endVal);
        maxVal = std::max(maxVal, endVal);

        if ( dp.numCoeffs == 1 ) {
            // 一阶导数常数 → 函数单调 → 最大最小已在端点
        } else if ( dp.numCoeffs == 2 ) {
            // 一次导数：极值 t = -a0 / a1
            float a0 = dp.coeffs[0];
            float a1 = dp.coeffs[1];
            if (std::abs(a1) > 1e-6f) {
                float t = -a0 / a1;
                if (t > 0.0f && t < duration) {
                    float v = eval(t);
                    minVal = std::min(minVal, v);
                    maxVal = std::max(maxVal, v);
                }
            }
        } else if ( dp.numCoeffs == 3 ) {
            // 二次导数 → 求极值点
            float a = dp.coeffs[2];
            float b = dp.coeffs[1];
            float c = dp.coeffs[0];
            float disc = b*b - 4*a*c;
            if (disc >= 0.0f) {
                float sqrtDisc = std::sqrt(disc);
                float t1 = (-b + sqrtDisc) / (2*a);
                float t2 = (-b - sqrtDisc) / (2*a);
                if (t1 > 0.0f && t1 < duration) {
                    float v = eval(t1);
                    minVal = std::min(minVal, v);
                    maxVal = std::max(maxVal, v);
                }
                if (t2 > 0.0f && t2 < duration) {
                    float v = eval(t2);
                    minVal = std::min(minVal, v);
                    maxVal = std::max(maxVal, v);
                }
            }
        } else {
            return Status::NotSupported("polyMaxMin", "too many coeffs");
        }

        return Status::OK();
    }

    // ------------------- toString
    std::string toString(int precision = 3) const
    {
        std::ostringstream oss;
        oss << "Poly[";
        for (uint8_t i = 0; i < numCoeffs; ++i) {
            if (i > 0) oss << ", ";
            oss << std::fixed << std::setprecision(precision) << coeffs[i];
        }
        oss << "]";
        return oss.str();
    }

};

struct Poly4D
{
    Poly x;
    Poly y;
    Poly z;
    Poly w;

    Poly4D() = default;

    inline void clear()
    {
        x.clear();
        y.clear();
        z.clear();
        w.clear();
    }

    inline Vector4D eval(float t) const
    {
        return {
            x.eval(t),
            y.eval(t),
            z.eval(t),
            w.eval(t)
        };
    }

    inline Poly4D derivative() const
    {
        Poly4D out;
        out.x = x.derivative();
        out.y = y.derivative();
        out.z = z.derivative();
        out.w = w.derivative();
        return out;
    }

    inline Status polyMaxMin(float duration, Vector4D& maxVal, Vector4D& minVal)
    {
        float x_max, x_min, y_max, y_min, z_max, z_min, w_max, w_min;
        Status status = x.polyMaxMin(duration, x_max, x_min);
        if ( !status.ok() ) return status;
        status = y.polyMaxMin(duration, y_max, y_min);
        if ( !status.ok() ) return status;
        status = z.polyMaxMin(duration, z_max, z_min);
        if ( !status.ok() ) return status;
        status = w.polyMaxMin(duration, w_max, w_min);
        if ( !status.ok() ) return status;

        maxVal = Vector4D(x_max, y_max, z_max, w_max);
        minVal = Vector4D(x_min, y_min, z_min, w_min);

        return Status::OK();
    }

    std::string toString(int precision = 5) const
    {
        std::ostringstream oss;
        oss << "Poly4D {\n"
            << "  x: " << x.toString(precision) << "\n"
            << "  y: " << y.toString(precision) << "\n"
            << "  z: " << z.toString(precision) << "\n"
            << "  w: " << w.toString(precision) << "\n"
            << "}";
        return oss.str();
    }
};

struct Color
{
    uint8_t r{0}, g{0}, b{0}, a{0};

    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}
    Color() = default;

    Color(const Color& other) = default;
    Color& operator=(const Color& other) = default;

    std::string toString(int precision = 3) const
    {
        std::ostringstream oss;
        oss << "Color[" << int(r) << ", " << int(g) << ", " << int(b) << ", " << int(a) << "]";
        return oss.str();
    }
};

};

#endif //LIBZDS_UTILS_TYPE_H
