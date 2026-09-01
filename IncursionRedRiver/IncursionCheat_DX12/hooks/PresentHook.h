#pragma once

bool InitializeHook();
void ShutdownHook();

bool IsHookInstalled();
bool IsRendererInitialized();
bool HasCapturedCommandQueue();

void DebugLog(const char* format, ...);
