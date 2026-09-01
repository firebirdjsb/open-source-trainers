#pragma once
#include "../sdk/Structs.h"

namespace WorldToScreen
{
    bool Convert(const FVector& world, Vector2& screen, const FVector& camLoc, const FRotator& camRot,
                 float horizontalFovDegrees, int width, int height);
}
