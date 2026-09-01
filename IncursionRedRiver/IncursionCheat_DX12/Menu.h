#pragma once

namespace Menu
{
    void Toggle();
    void SetOpen(bool open);
    void MaintainInputPriority();
    void ReleaseInputPriority();
    void Render();
    inline bool bOpen = true;
}
