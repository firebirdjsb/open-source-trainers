#pragma once

#include <functional>

bool InitializeHook();
void ShutdownHook();

bool IsHookInstalled();
bool IsRendererInitialized();
bool HasCapturedCommandQueue();

// Present executes on Unreal's render/RHI path. UObject mutations that can
// create UI render targets must instead be posted to the thread that owns the
// game window (the UE game thread in this build).
bool QueueGameThreadTask(std::function<void()> task);
unsigned long GetGameWindowThreadId();
unsigned long GetLastGameTaskThreadId();

void DebugLog(const char* format, ...);
