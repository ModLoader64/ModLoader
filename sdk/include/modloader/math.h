#pragma once

#include <modloader/types.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ModLoader::Math {

constexpr f32 gPi = std::numbers::pi_v<f32>;
constexpr f32 gTau = 2.0f * gPi;
constexpr f32 gHalfPi = 0.5f * gPi;

inline f32 Sin(f32 radians) {
    return std::sin(radians);
}

inline f32 Cos(f32 radians) {
    return std::cos(radians);
}

inline f32 Tan(f32 radians) {
    return std::tan(radians);
}

inline f32 Atan2(f32 y, f32 x) {
    return std::atan2(y, x);
}

inline f32 Sqrt(f32 value) {
    return std::sqrt(value);
}

inline f32 Abs(f32 value) {
    return std::abs(value);
}

inline f32 Floor(f32 value) {
    return std::floor(value);
}

inline f32 Ceil(f32 value) {
    return std::ceil(value);
}

inline f32 Round(f32 value) {
    return std::round(value);
}

inline f32 Lerp(f32 from, f32 to, f32 amount) {
    return std::lerp(from, to, amount);
}

template<typename T>
constexpr T Min(T left, T right) {
    return std::min(left, right);
}

template<typename T>
constexpr T Max(T left, T right) {
    return std::max(left, right);
}

template<typename T>
constexpr T Clamp(T value, T low, T high) {
    return std::clamp(value, low, high);
}
template <typename V>
concept Vector3_Like = requires(V vector) {
    vector.x;
    vector.y;
    vector.z;
};

template <Vector3_Like V>
using Vector3_Element = __remove_cvref(decltype(V::x));

template <typename T>
struct Vector3 {
    T x;
    T y;
    T z;

    static const Vector3 Zero;
    static const Vector3 One;
    static const Vector3 Up;
    static const Vector3 Right;
    static const Vector3 Forward;

    constexpr Vector3() = default;

    constexpr Vector3(T x_value, T y_value, T z_value) : x(x_value), y(y_value), z(z_value) {}

    template <Vector3_Like V>
        requires(!__is_same(__remove_cvref(V), Vector3))
    constexpr explicit(!__is_same(Vector3_Element<V>, T)) Vector3(V other) : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {}

    template <Vector3_Like V>
        requires(!__is_same(V, Vector3) && requires(Vector3_Element<V> component) { V{ component, component, component }; })
    constexpr explicit(!__is_same(Vector3_Element<V>, T)) operator V(this Vector3 self) {
        using Element = Vector3_Element<V>;
        return V{ static_cast<Element>(self.x), static_cast<Element>(self.y), static_cast<Element>(self.z) };
    }

    constexpr Vector3 operator+(this Vector3 self, Vector3 other) {
        return { static_cast<T>(self.x + other.x), static_cast<T>(self.y + other.y), static_cast<T>(self.z + other.z) };
    }

    constexpr Vector3 operator-(this Vector3 self, Vector3 other) {
        return { static_cast<T>(self.x - other.x), static_cast<T>(self.y - other.y), static_cast<T>(self.z - other.z) };
    }

    constexpr Vector3 operator-(this Vector3 self) {
        return { static_cast<T>(-self.x), static_cast<T>(-self.y), static_cast<T>(-self.z) };
    }

    constexpr Vector3 operator*(this Vector3 self, T scale) {
        return { static_cast<T>(self.x * scale), static_cast<T>(self.y * scale), static_cast<T>(self.z * scale) };
    }

    constexpr Vector3 operator/(this Vector3 self, T divisor) {
        return { static_cast<T>(self.x / divisor), static_cast<T>(self.y / divisor), static_cast<T>(self.z / divisor) };
    }

    constexpr Vector3& operator+=(Vector3 other) {
        *this = *this + other;
        return *this;
    }

    constexpr Vector3& operator-=(Vector3 other) {
        *this = *this - other;
        return *this;
    }

    constexpr Vector3& operator*=(T scale) {
        *this = *this * scale;
        return *this;
    }

    constexpr Vector3& operator/=(T divisor) {
        *this = *this / divisor;
        return *this;
    }

    constexpr bool operator==(this Vector3 self, Vector3 other) {
        return self.x == other.x && self.y == other.y && self.z == other.z;
    }

    constexpr Vector3 Scaled(this Vector3 self, Vector3 other) {
        return { static_cast<T>(self.x * other.x), static_cast<T>(self.y * other.y), static_cast<T>(self.z * other.z) };
    }

    constexpr T Dot(this Vector3 self, Vector3 other) {
        return self.x * other.x + self.y * other.y + self.z * other.z;
    }

    constexpr Vector3 Cross(this Vector3 self, Vector3 other) {
        return {
            static_cast<T>(self.y * other.z - self.z * other.y),
            static_cast<T>(self.z * other.x - self.x * other.z),
            static_cast<T>(self.x * other.y - self.y * other.x),
        };
    }

    constexpr T Length_Squared(this Vector3 self) {
        return self.Dot(self);
    }

    auto Length(this Vector3 self) {
        return std::hypot(self.x, self.y, self.z);
    }

    auto Distance(this Vector3 self, Vector3 other) {
        using Result = Vector3<decltype(self.Length())>;
        return (Result{self} - Result{other}).Length();
    }

    // A zero vector stays zero; integer vectors produce Vector3<f64>
    auto Normalized(this Vector3 self) {
        using Result = Vector3<decltype(self.Length())>;
        auto length = self.Length();
        if (length == 0) {
            return Result{0, 0, 0};
        }
        return Result{self} / length;
    }

    Vector3 Lerp(this Vector3 self, Vector3 to, f32 amount) {
        return {
            static_cast<T>(self.x + (to.x - self.x) * amount),
            static_cast<T>(self.y + (to.y - self.y) * amount),
            static_cast<T>(self.z + (to.z - self.z) * amount),
        };
    }
};

template <typename T>
constexpr Vector3<T> operator*(T scale, Vector3<T> vector) {
    return vector * scale;
}

template <typename T>
inline const Vector3<T> Vector3<T>::Zero = { 0, 0, 0 };
template <typename T>
inline const Vector3<T> Vector3<T>::One = { 1, 1, 1 };
template <typename T>
inline const Vector3<T> Vector3<T>::Up = { 0, 1, 0 };
template <typename T>
inline const Vector3<T> Vector3<T>::Right = { 1, 0, 0 };
template <typename T>
inline const Vector3<T> Vector3<T>::Forward = { 0, 0, 1 };

using Vector3f = Vector3<f32>;
using Vector3d = Vector3<f64>;
using Vector3s = Vector3<s16>;
using Vector3i = Vector3<s32>;

static_assert(sizeof(Vector3f) == 12 && sizeof(Vector3s) == 6, "vectors keep the games' layout");

} // namespace ModLoader::Math
