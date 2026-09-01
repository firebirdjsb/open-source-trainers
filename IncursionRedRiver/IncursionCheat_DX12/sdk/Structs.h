#pragma once

#include <cmath>
#include <cstdint>

// UE5 uses Large World Coordinates in this build. FVector/FRotator are double precision.
struct FVector
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;

    FVector() = default;
    FVector(double x, double y, double z) : X(x), Y(y), Z(z) {}

    FVector operator+(const FVector& other) const { return { X + other.X, Y + other.Y, Z + other.Z }; }
    FVector operator-(const FVector& other) const { return { X - other.X, Y - other.Y, Z - other.Z }; }
    FVector operator*(double scalar) const { return { X * scalar, Y * scalar, Z * scalar }; }

    double Dot(const FVector& other) const { return X * other.X + Y * other.Y + Z * other.Z; }
    double Length() const { return std::sqrt(X * X + Y * Y + Z * Z); }
    double Distance(const FVector& other) const { return (*this - other).Length(); }
    bool IsFinite() const { return std::isfinite(X) && std::isfinite(Y) && std::isfinite(Z); }
};

struct FRotator
{
    double Pitch = 0.0;
    double Yaw = 0.0;
    double Roll = 0.0;

    bool IsFinite() const { return std::isfinite(Pitch) && std::isfinite(Yaw) && std::isfinite(Roll); }
};

// Engine.Transform in the supplied UE 5.6.1 dump:
// Rotation @ 0x00, Translation @ 0x20, Scale3D @ 0x40, sizeof == 0x60.
struct FQuat
{
    double X = 0.0;
    double Y = 0.0;
    double Z = 0.0;
    double W = 1.0;
};

struct FTransform
{
    FQuat Rotation{};            // 0x00
    FVector Translation{};       // 0x20
    uint64_t Pad38 = 0;          // 0x38 -> align Scale3D to 0x40
    FVector Scale3D{1.0, 1.0, 1.0}; // 0x40
    uint64_t Pad58 = 0;          // 0x58 -> sizeof 0x60
};
static_assert(sizeof(FTransform) == 0x60, "UE5 FTransform layout changed");

struct Vector2
{
    float x = 0.0f;
    float y = 0.0f;
};

struct FLinearColor
{
    float R = 0.0f;
    float G = 0.0f;
    float B = 0.0f;
    float A = 1.0f;
};

// UE5 FName is an eight-byte pair: comparison/name-pool index and instance number.
// Profile tools copy this token from a validated, already-loaded UObject instead of
// attempting to manufacture a name-pool entry from user input.
struct FName
{
    uint32_t ComparisonIndex = 0;
    uint32_t Number = 0;

    bool IsValid() const { return ComparisonIndex != 0; }
};
static_assert(sizeof(FName) == 0x8, "UE5 FName layout changed");

template <typename T>
struct TArray
{
    T* Data = nullptr;
    int32_t Count = 0;
    int32_t Max = 0;

    bool IsSane(int32_t hardLimit = 1000000) const
    {
        return Count >= 0 && Count <= Max && Max >= 0 && Max <= hardLimit;
    }
};

// Kept only for older code that may include this header. Do not use for UE FVector memory.
struct Vector3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};
