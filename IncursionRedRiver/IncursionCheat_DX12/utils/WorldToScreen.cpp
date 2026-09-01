#include "WorldToScreen.h"

#include <cmath>

namespace
{
    constexpr double Pi = 3.14159265358979323846;
}

bool WorldToScreen::Convert(const FVector& world, Vector2& screen, const FVector& camLoc,
                            const FRotator& camRot, float horizontalFovDegrees, int width, int height)
{
    if (width <= 0 || height <= 0 || horizontalFovDegrees <= 1.0f || horizontalFovDegrees >= 179.0f)
        return false;

    const FVector delta = world - camLoc;
    const double pitch = camRot.Pitch * Pi / 180.0;
    const double yaw = camRot.Yaw * Pi / 180.0;
    const double roll = camRot.Roll * Pi / 180.0;

    const double sp = std::sin(pitch), cp = std::cos(pitch);
    const double sy = std::sin(yaw),   cy = std::cos(yaw);
    const double sr = std::sin(roll),  cr = std::cos(roll);

    // Unreal rotation basis (X forward, Y right, Z up), including roll.
    const FVector forward{ cp * cy, cp * sy, sp };
    const FVector right{ sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp };
    const FVector up{ -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp };

    const double viewX = delta.Dot(right);
    const double viewY = delta.Dot(up);
    const double viewZ = delta.Dot(forward);
    if (viewZ <= 0.01)
        return false;

    const double centerX = static_cast<double>(width) * 0.5;
    const double centerY = static_cast<double>(height) * 0.5;
    const double focal = centerX / std::tan(static_cast<double>(horizontalFovDegrees) * Pi / 360.0);

    screen.x = static_cast<float>(centerX + viewX * focal / viewZ);
    screen.y = static_cast<float>(centerY - viewY * focal / viewZ);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}
