#pragma once
#include "../imgui/imgui.h"
#include "../sdk/Structs.h"

namespace Renderer
{
    inline void Begin()
    {
        ImGui::GetBackgroundDrawList()->PushClipRectFullScreen();
    }

    inline void DrawCircle(float x, float y, float radius, ImU32 color,
                           float thickness = 2.0f)
    {
        ImGui::GetBackgroundDrawList()->AddCircle(
            ImVec2(x, y),
            radius,
            color,
            0,  // segments
            thickness
        );
    }

    inline void DrawLine(float x1, float y1, float x2, float y2, ImU32 color, float thickness = 1.0f)
    {
        ImGui::GetBackgroundDrawList()->AddLine(
            ImVec2(x1, y1),
            ImVec2(x2, y2),
            color,
            thickness
        );
    }

    inline void DrawBox(const Vector2& topLeft, const Vector2& bottomRight, ImU32 color)
    {
        ImGui::GetBackgroundDrawList()->AddRect(
            ImVec2(topLeft.x, topLeft.y),
            ImVec2(bottomRight.x, bottomRight.y),
            color,
            0.0f, // rounding
            0,
            2.0f  // thickness
        );
    }

    inline void DrawBox(float x, float y, float width, float height, ImU32 color, float thickness = 1.0f)
    {
        ImGui::GetBackgroundDrawList()->AddRect(
            ImVec2(x, y),
            ImVec2(x + width, y + height),
            color,
            0.0f,
            0,
            thickness
        );
    }

    inline void DrawText(float x, float y, const char* text, ImU32 color)
    {
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(x, y), color, text);
    }

    inline void DrawText(const Vector2& pos, const char* text, ImU32 color)
    {
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(pos.x, pos.y), color, text);
    }

    inline void DrawHealthBar(float x, float y, float width, float height, float health, float maxHealth)
    {
        float healthPercent = health / maxHealth;
        ImU32 color = IM_COL32(255 * (1 - healthPercent), 255 * healthPercent, 0, 255);

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            ImVec2(x, y),
            ImVec2(x + (width * healthPercent), y + height),
            color
        );

        ImGui::GetBackgroundDrawList()->AddRect(
            ImVec2(x, y),
            ImVec2(x + width, y + height),
            IM_COL32(0, 0, 0, 255)
        );
    }

    // Overload with Vector2 position for backward compatibility
    inline void DrawHealthBar(const Vector2& pos, float health, float maxHealth)
    {
        DrawHealthBar(pos.x, pos.y, 50.0f, 5.0f, health, maxHealth);
    }
}
