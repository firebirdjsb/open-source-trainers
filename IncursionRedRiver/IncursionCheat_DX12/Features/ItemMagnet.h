#pragma once

namespace ItemMagnet
{
    extern bool enabled;
    extern int selectedIndex;

    void ProcessTick();
    void Shutdown();
    void RenderTab();
    void Render();
    void PullAllItems();
    void PullHighValueItems();
    void PullSelectedItem();
}
