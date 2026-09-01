#include "PresentHook.h"

#include "../CheatManager.h"
#include "../Menu.h"
#include "../Memory/Memory.h"
#include "../imgui/imgui.h"
#include "../imgui/backends/imgui_impl_dx12.h"
#include "../imgui/backends/imgui_impl_win32.h"

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

// Dear ImGui intentionally does not expose this declaration from
// imgui_impl_win32.h unless the application copies it locally.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    using PresentFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT);
    using Present1Fn = HRESULT(__stdcall*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    using ResizeBuffersFn = HRESULT(__stdcall*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    using ResizeBuffers1Fn = HRESULT(__stdcall*)(IDXGISwapChain3*, UINT, UINT, UINT, DXGI_FORMAT, UINT, const UINT*, IUnknown* const*);
    using ExecuteCommandListsFn = void(__stdcall*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

    struct HookSlot
    {
        void** Slot = nullptr;
        void* Original = nullptr;
        bool Installed = false;
    };

    struct FrameContext
    {
        ID3D12CommandAllocator* Allocator = nullptr;
        ID3D12Resource* BackBuffer = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE Rtv{};
        UINT64 FenceValue = 0;
    };

    struct SrvDescriptorAllocator
    {
        ID3D12DescriptorHeap* Heap = nullptr;
        UINT Increment = 0;
        UINT Capacity = 0;
        std::vector<bool> Used;
        std::mutex Mutex;

        void Reset(ID3D12DescriptorHeap* heap, UINT increment, UINT capacity)
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Heap = heap;
            Increment = increment;
            Capacity = capacity;
            Used.assign(capacity, false);
        }

        bool Alloc(D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu)
        {
            std::lock_guard<std::mutex> lock(Mutex);
            if (!Heap || !Increment)
                return false;
            for (UINT i = 0; i < Capacity; ++i)
            {
                if (Used[i])
                    continue;
                Used[i] = true;
                cpu = Heap->GetCPUDescriptorHandleForHeapStart();
                gpu = Heap->GetGPUDescriptorHandleForHeapStart();
                cpu.ptr += static_cast<SIZE_T>(i) * Increment;
                gpu.ptr += static_cast<UINT64>(i) * Increment;
                return true;
            }
            return false;
        }

        void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpu)
        {
            std::lock_guard<std::mutex> lock(Mutex);
            if (!Heap || !Increment || !cpu.ptr)
                return;
            const SIZE_T start = Heap->GetCPUDescriptorHandleForHeapStart().ptr;
            if (cpu.ptr < start)
                return;
            const SIZE_T diff = cpu.ptr - start;
            const UINT index = static_cast<UINT>(diff / Increment);
            if (index < Used.size())
                Used[index] = false;
        }

        void Clear()
        {
            std::lock_guard<std::mutex> lock(Mutex);
            Heap = nullptr;
            Increment = 0;
            Capacity = 0;
            Used.clear();
        }
    };

    std::atomic<bool> g_hookInstalled{ false };
    std::atomic<bool> g_rendererInitialized{ false };
    std::atomic<bool> g_shuttingDown{ false };
    std::atomic<bool> g_presentObserved{ false };
    std::atomic<bool> g_waitingQueueLogged{ false };
    std::atomic<ID3D12CommandQueue*> g_gameQueue{ nullptr };
    std::mutex g_queueMutex;
    std::vector<ID3D12CommandQueue*> g_directQueueCandidates;

    HookSlot g_presentHook{};
    HookSlot g_present1Hook{};
    HookSlot g_resizeHook{};
    HookSlot g_resize1Hook{};
    HookSlot g_executeHook{};

    PresentFn g_originalPresent = nullptr;
    Present1Fn g_originalPresent1 = nullptr;
    ResizeBuffersFn g_originalResize = nullptr;
    ResizeBuffers1Fn g_originalResize1 = nullptr;
    ExecuteCommandListsFn g_originalExecute = nullptr;

    std::mutex g_initMutex;
    std::mutex g_renderMutex;
    thread_local bool g_inPresentHook = false;
    IDXGISwapChain3* g_swapChain = nullptr;
    ID3D12Device* g_device = nullptr;
    ID3D12DescriptorHeap* g_rtvHeap = nullptr;
    ID3D12DescriptorHeap* g_srvHeap = nullptr;
    ID3D12GraphicsCommandList* g_commandList = nullptr;
    ID3D12Fence* g_fence = nullptr;
    HANDLE g_fenceEvent = nullptr;
    UINT64 g_nextFenceValue = 1;
    DXGI_FORMAT g_rtvFormat = DXGI_FORMAT_UNKNOWN;
    std::vector<FrameContext> g_frames;
    SrvDescriptorAllocator g_srvAllocator;
    HWND g_hwnd = nullptr;
    WNDPROC g_originalWndProc = nullptr;
    bool g_imguiContext = false;
    bool g_imguiWin32 = false;
    bool g_imguiDx12 = false;
    bool g_menuOpenLastFrame = false;

    template <typename T>
    void SafeRelease(T*& ptr)
    {
        if (ptr)
        {
            ptr->Release();
            ptr = nullptr;
        }
    }


    bool SameDevice(ID3D12CommandQueue* queue, ID3D12Device* device)
    {
        if (!queue || !device)
            return false;
        ID3D12Device* queueDevice = nullptr;
        if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) || !queueDevice)
            return false;

        IUnknown* a = nullptr;
        IUnknown* b = nullptr;
        queueDevice->QueryInterface(IID_PPV_ARGS(&a));
        device->QueryInterface(IID_PPV_ARGS(&b));
        const bool same = a && b && a == b;
        SafeRelease(a);
        SafeRelease(b);
        SafeRelease(queueDevice);
        return same;
    }

    ID3D12CommandQueue* FindQueueForDevice(ID3D12Device* device)
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        for (ID3D12CommandQueue* queue : g_directQueueCandidates)
            if (SameDevice(queue, device))
                return queue;
        return nullptr;
    }

    void RegisterDirectQueue(ID3D12CommandQueue* queue)
    {
        if (!queue)
            return;
        std::lock_guard<std::mutex> lock(g_queueMutex);
        if (std::find(g_directQueueCandidates.begin(), g_directQueueCandidates.end(), queue) != g_directQueueCandidates.end())
            return;
        if (g_directQueueCandidates.size() >= 16)
            return;
        queue->AddRef();
        g_directQueueCandidates.push_back(queue);
        DebugLog("[Hook] Observed DIRECT command queue candidate: %p\n", queue);
    }

    bool PatchVtableSlot(void** slot, void* replacement, HookSlot& out)
    {
        if (!slot || !replacement || out.Installed)
            return false;

        DWORD oldProtect = 0;
        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
            return false;

        out.Slot = slot;
        out.Original = *slot;
        *slot = replacement;
        FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));

        DWORD ignored = 0;
        VirtualProtect(slot, sizeof(void*), oldProtect, &ignored);
        out.Installed = true;
        return true;
    }

    void RestoreVtableSlot(HookSlot& hook)
    {
        if (!hook.Installed || !hook.Slot)
            return;
        DWORD oldProtect = 0;
        if (VirtualProtect(hook.Slot, sizeof(void*), PAGE_READWRITE, &oldProtect))
        {
            *hook.Slot = hook.Original;
            FlushInstructionCache(GetCurrentProcess(), hook.Slot, sizeof(void*));
            DWORD ignored = 0;
            VirtualProtect(hook.Slot, sizeof(void*), oldProtect, &ignored);
        }
        hook = {};
    }

    void SrvAllocCallback(ImGui_ImplDX12_InitInfo* info,
                          D3D12_CPU_DESCRIPTOR_HANDLE* outCpu,
                          D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
    {
        if (!info || !outCpu || !outGpu)
            return;
        auto* allocator = static_cast<SrvDescriptorAllocator*>(info->UserData);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        if (allocator && allocator->Alloc(cpu, gpu))
        {
            *outCpu = cpu;
            *outGpu = gpu;
        }
    }

    void SrvFreeCallback(ImGui_ImplDX12_InitInfo* info,
                         D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                         D3D12_GPU_DESCRIPTOR_HANDLE)
    {
        if (!info)
            return;
        auto* allocator = static_cast<SrvDescriptorAllocator*>(info->UserData);
        if (allocator)
            allocator->Free(cpu);
    }

    void WaitForFenceValue(UINT64 value)
    {
        if (!g_fence || !g_fenceEvent || value == 0 || g_fence->GetCompletedValue() >= value)
            return;
        if (SUCCEEDED(g_fence->SetEventOnCompletion(value, g_fenceEvent)))
            WaitForSingleObject(g_fenceEvent, INFINITE);
    }

    void WaitForGpu()
    {
        ID3D12CommandQueue* queue = g_gameQueue.load();
        if (!queue || !g_fence || !g_fenceEvent)
            return;
        const UINT64 value = g_nextFenceValue++;
        if (SUCCEEDED(queue->Signal(g_fence, value)))
            WaitForFenceValue(value);
    }

    LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Keep the menu hotkeys from leaking through to the game even when the menu
        // is currently closed. Toggle detection itself uses GetAsyncKeyState().
        if ((msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) &&
            (wParam == VK_HOME || wParam == VK_INSERT || wParam == VK_DELETE))
            return 1;

        if (g_imguiContext && Menu::bOpen)
        {
            ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);

            // Menu-open means UI-only input. Do not depend on ImGui WantCapture*:
            // Unreal commonly consumes WM_INPUT directly and can otherwise fire,
            // interact or move while a user is clicking the overlay.
            const bool mouseMsg = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
            const bool keyMsg = msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
            if (msg == WM_INPUT)
            {
                // WM_INPUT with RIM_INPUT requires DefWindowProc cleanup. Bypass the
                // game's WndProc but still let Windows release the raw-input packet.
                DefWindowProcW(hwnd, msg, wParam, lParam);
                return 0;
            }
            if (mouseMsg || keyMsg)
                return 1;

            if (msg == WM_SETCURSOR && LOWORD(lParam) == HTCLIENT)
                return TRUE; // ImGui backend already selected the software cursor.
        }

        return g_originalWndProc ? CallWindowProcW(g_originalWndProc, hwnd, msg, wParam, lParam)
                                 : DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    void ReleaseRendererLocked()
    {
        g_rendererInitialized.store(false);

        if (g_fence && g_gameQueue.load())
            WaitForGpu();

        Menu::ReleaseInputPriority();
        g_CheatManager.Shutdown();

        if (g_imguiDx12)
        {
            ImGui_ImplDX12_Shutdown();
            g_imguiDx12 = false;
        }
        if (g_imguiWin32)
        {
            ImGui_ImplWin32_Shutdown();
            g_imguiWin32 = false;
        }

        if (g_hwnd && g_originalWndProc)
        {
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalWndProc));
            g_originalWndProc = nullptr;
        }
        g_hwnd = nullptr;

        if (g_imguiContext && ImGui::GetCurrentContext())
        {
            ImGui::DestroyContext();
            g_imguiContext = false;
        }

        for (auto& frame : g_frames)
        {
            SafeRelease(frame.BackBuffer);
            SafeRelease(frame.Allocator);
            frame.FenceValue = 0;
        }
        g_frames.clear();

        SafeRelease(g_commandList);
        SafeRelease(g_rtvHeap);
        g_srvAllocator.Clear();
        SafeRelease(g_srvHeap);
        SafeRelease(g_fence);
        if (g_fenceEvent)
        {
            CloseHandle(g_fenceEvent);
            g_fenceEvent = nullptr;
        }
        SafeRelease(g_swapChain);
        SafeRelease(g_device);
        g_nextFenceValue = 1;
        g_rtvFormat = DXGI_FORMAT_UNKNOWN;
        g_menuOpenLastFrame = false;
    }

    bool InitializeRendererLocked(IDXGISwapChain* inputSwapChain)
    {
        if (g_rendererInitialized.load())
            return true;
        if (!inputSwapChain)
            return false;

        if (FAILED(inputSwapChain->QueryInterface(IID_PPV_ARGS(&g_swapChain))) || !g_swapChain)
            return false;
        if (FAILED(inputSwapChain->GetDevice(IID_PPV_ARGS(&g_device))) || !g_device)
        {
            ReleaseRendererLocked();
            return false;
        }

        ID3D12CommandQueue* queue = FindQueueForDevice(g_device);
        if (!queue)
        {
            if (!g_waitingQueueLogged.exchange(true))
                DebugLog("[Render] Present observed, but no DIRECT command queue matching the swapchain device has executed yet. Waiting...\n");
            ReleaseRendererLocked();
            return false;
        }
        g_waitingQueueLogged.store(false);
        g_gameQueue.store(queue);

        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(inputSwapChain->GetDesc(&desc)) || desc.BufferCount == 0)
        {
            ReleaseRendererLocked();
            return false;
        }

        g_hwnd = desc.OutputWindow;
        if (!g_hwnd)
            g_hwnd = GetForegroundWindow();
        if (!g_hwnd)
        {
            ReleaseRendererLocked();
            return false;
        }

        g_rtvFormat = desc.BufferDesc.Format;
        if (g_rtvFormat == DXGI_FORMAT_UNKNOWN)
        {
            ID3D12Resource* firstBuffer = nullptr;
            if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&firstBuffer))) && firstBuffer)
            {
                g_rtvFormat = firstBuffer->GetDesc().Format;
                firstBuffer->Release();
            }
        }
        if (g_rtvFormat == DXGI_FORMAT_UNKNOWN)
        {
            ReleaseRendererLocked();
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = desc.BufferCount;
        if (FAILED(g_device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap))))
        {
            ReleaseRendererLocked();
            return false;
        }

        constexpr UINT SrvCapacity = 64;
        D3D12_DESCRIPTOR_HEAP_DESC srvDesc{};
        srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvDesc.NumDescriptors = SrvCapacity;
        srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(g_device->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap))))
        {
            ReleaseRendererLocked();
            return false;
        }

        g_srvAllocator.Reset(g_srvHeap,
                             g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV),
                             SrvCapacity);

        if (FAILED(g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence))))
        {
            ReleaseRendererLocked();
            return false;
        }
        g_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!g_fenceEvent)
        {
            ReleaseRendererLocked();
            return false;
        }

        const UINT rtvIncrement = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        g_frames.resize(desc.BufferCount);
        for (UINT i = 0; i < desc.BufferCount; ++i)
        {
            auto& frame = g_frames[i];
            if (FAILED(g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.Allocator))) ||
                FAILED(g_swapChain->GetBuffer(i, IID_PPV_ARGS(&frame.BackBuffer))))
            {
                ReleaseRendererLocked();
                return false;
            }
            frame.Rtv = rtv;
            g_device->CreateRenderTargetView(frame.BackBuffer, nullptr, frame.Rtv);
            rtv.ptr += rtvIncrement;
        }

        if (FAILED(g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_frames[0].Allocator,
                                                nullptr, IID_PPV_ARGS(&g_commandList))) ||
            FAILED(g_commandList->Close()))
        {
            ReleaseRendererLocked();
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        g_imguiContext = true;
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        ImGui::StyleColorsDark();

        if (!ImGui_ImplWin32_Init(g_hwnd))
        {
            ReleaseRendererLocked();
            return false;
        }
        g_imguiWin32 = true;

        ImGui_ImplDX12_InitInfo initInfo{};
        initInfo.Device = g_device;
        initInfo.CommandQueue = queue;
        initInfo.NumFramesInFlight = static_cast<int>(desc.BufferCount);
        initInfo.RTVFormat = g_rtvFormat;
        initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
        initInfo.UserData = &g_srvAllocator;
        initInfo.SrvDescriptorHeap = g_srvHeap;
        initInfo.SrvDescriptorAllocFn = SrvAllocCallback;
        initInfo.SrvDescriptorFreeFn = SrvFreeCallback;
        if (!ImGui_ImplDX12_Init(&initInfo))
        {
            ReleaseRendererLocked();
            return false;
        }
        g_imguiDx12 = true;

        SetLastError(0);
        const LONG_PTR oldWndProc = SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(HookWndProc));
        if (!oldWndProc && GetLastError() != 0)
        {
            ReleaseRendererLocked();
            return false;
        }
        g_originalWndProc = reinterpret_cast<WNDPROC>(oldWndProc);

        if (!g_CheatManager.Initialize())
        {
            ReleaseRendererLocked();
            return false;
        }

        g_rendererInitialized.store(true);
        DebugLog("[Render] DX12 overlay initialized. Buffers=%u Format=%u HWND=%p Queue=%p\n",
                 desc.BufferCount, static_cast<unsigned>(g_rtvFormat), g_hwnd, queue);
        return true;
    }

    void ToggleMenuKeys()
    {
        if ((GetAsyncKeyState(VK_HOME) & 1) || (GetAsyncKeyState(VK_INSERT) & 1) || (GetAsyncKeyState(VK_DELETE) & 1))
            Menu::Toggle();
    }

    void RenderOverlay(IDXGISwapChain* inputSwapChain)
    {
        if (g_shuttingDown.load())
            return;

        std::lock_guard<std::mutex> lock(g_renderMutex);
        if (!InitializeRendererLocked(inputSwapChain))
            return;

        if (!g_swapChain || !g_device || !g_commandList || g_frames.empty())
            return;

        ToggleMenuKeys();

        // Give the overlay immediate OS-level cursor priority whenever it opens.
        // Releasing capture/clipping every frame also counters games that re-capture
        // the mouse during their own input tick.
        if (Menu::bOpen)
        {
            if (!g_menuOpenLastFrame && g_hwnd)
            {
                SetForegroundWindow(g_hwnd);
                SetFocus(g_hwnd);
            }
            ReleaseCapture();
            ClipCursor(nullptr);
        }
        g_menuOpenLastFrame = Menu::bOpen;

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGui::GetIO().MouseDrawCursor = Menu::bOpen;

        g_CheatManager.Tick();
        Menu::MaintainInputPriority();
        Menu::Render();

        ImGui::Render();
        ImDrawData* drawData = ImGui::GetDrawData();
        if (!drawData)
            return;

        const UINT frameIndex = g_swapChain->GetCurrentBackBufferIndex();
        if (frameIndex >= g_frames.size())
            return;
        FrameContext& frame = g_frames[frameIndex];

        WaitForFenceValue(frame.FenceValue);

        if (FAILED(frame.Allocator->Reset()) || FAILED(g_commandList->Reset(frame.Allocator, nullptr)))
            return;

        D3D12_RESOURCE_BARRIER toRender{};
        toRender.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRender.Transition.pResource = frame.BackBuffer;
        toRender.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toRender.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        toRender.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_commandList->ResourceBarrier(1, &toRender);

        g_commandList->OMSetRenderTargets(1, &frame.Rtv, FALSE, nullptr);
        ID3D12DescriptorHeap* heaps[] = { g_srvHeap };
        g_commandList->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(drawData, g_commandList);

        std::swap(toRender.Transition.StateBefore, toRender.Transition.StateAfter);
        g_commandList->ResourceBarrier(1, &toRender);
        if (FAILED(g_commandList->Close()))
            return;

        ID3D12CommandQueue* queue = g_gameQueue.load();
        if (!queue)
            return;
        ID3D12CommandList* lists[] = { g_commandList };
        queue->ExecuteCommandLists(1, lists);

        const UINT64 fenceValue = g_nextFenceValue++;
        if (SUCCEEDED(queue->Signal(g_fence, fenceValue)))
            frame.FenceValue = fenceValue;
    }

    HRESULT __stdcall HookPresent(IDXGISwapChain* swapChain, UINT syncInterval, UINT flags)
    {
        if (!g_presentObserved.exchange(true))
            DebugLog("[Hook] First IDXGISwapChain::Present observed: %p\n", swapChain);
        if (!g_inPresentHook)
        {
            g_inPresentHook = true;
            RenderOverlay(swapChain);
            g_inPresentHook = false;
        }
        return g_originalPresent ? g_originalPresent(swapChain, syncInterval, flags) : E_FAIL;
    }

    HRESULT __stdcall HookPresent1(IDXGISwapChain1* swapChain, UINT syncInterval, UINT flags,
                                   const DXGI_PRESENT_PARAMETERS* params)
    {
        if (!g_presentObserved.exchange(true))
            DebugLog("[Hook] First IDXGISwapChain1::Present1 observed: %p\n", swapChain);
        if (!g_inPresentHook)
        {
            g_inPresentHook = true;
            RenderOverlay(swapChain);
            g_inPresentHook = false;
        }
        return g_originalPresent1 ? g_originalPresent1(swapChain, syncInterval, flags, params) : E_FAIL;
    }

    HRESULT __stdcall HookResizeBuffers(IDXGISwapChain* swapChain, UINT bufferCount, UINT width, UINT height,
                                        DXGI_FORMAT format, UINT flags)
    {
        {
            std::lock_guard<std::mutex> lock(g_renderMutex);
            if (g_rendererInitialized.load())
            {
                DebugLog("[Render] ResizeBuffers: %ux%u buffers=%u\n", width, height, bufferCount);
                ReleaseRendererLocked();
            }
        }
        return g_originalResize ? g_originalResize(swapChain, bufferCount, width, height, format, flags) : E_FAIL;
    }

    HRESULT __stdcall HookResizeBuffers1(IDXGISwapChain3* swapChain, UINT bufferCount, UINT width, UINT height,
                                         DXGI_FORMAT format, UINT flags, const UINT* nodeMasks, IUnknown* const* presentQueues)
    {
        {
            std::lock_guard<std::mutex> lock(g_renderMutex);
            if (g_rendererInitialized.load())
            {
                DebugLog("[Render] ResizeBuffers1: %ux%u buffers=%u\n", width, height, bufferCount);
                ReleaseRendererLocked();
            }
        }
        return g_originalResize1 ? g_originalResize1(swapChain, bufferCount, width, height, format, flags, nodeMasks, presentQueues) : E_FAIL;
    }

    void __stdcall HookExecuteCommandLists(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
    {
        if (queue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            RegisterDirectQueue(queue);
        if (g_originalExecute)
            g_originalExecute(queue, count, lists);
    }

    bool CreateBootstrapObjects(IDXGISwapChain1** outSwapChain, ID3D12CommandQueue** outQueue,
                                ID3D12Device** outDevice, HWND& outWindow, ATOM& outClass)
    {
        if (!outSwapChain || !outQueue || !outDevice)
            return false;
        *outSwapChain = nullptr;
        *outQueue = nullptr;
        *outDevice = nullptr;
        outWindow = nullptr;
        outClass = 0;

        wchar_t className[96]{};
        std::swprintf(className, sizeof(className) / sizeof(wchar_t), L"IncursionHookBootstrap_%lu", GetCurrentProcessId());

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = className;
        outClass = RegisterClassExW(&wc);
        if (!outClass)
            return false;

        outWindow = CreateWindowExW(0, className, L"", WS_OVERLAPPEDWINDOW,
                                    0, 0, 2, 2, nullptr, nullptr, wc.hInstance, nullptr);
        if (!outWindow)
            return false;

        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(outDevice))))
            return false;

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED((*outDevice)->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(outQueue))))
            return false;

        IDXGIFactory2* factory = nullptr;
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) || !factory)
            return false;

        DXGI_SWAP_CHAIN_DESC1 swapDesc{};
        swapDesc.Width = 2;
        swapDesc.Height = 2;
        swapDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapDesc.SampleDesc.Count = 1;
        swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapDesc.BufferCount = 2;
        swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        const HRESULT hr = factory->CreateSwapChainForHwnd(*outQueue, outWindow, &swapDesc, nullptr, nullptr, outSwapChain);
        factory->Release();
        return SUCCEEDED(hr) && *outSwapChain;
    }

    bool InstallComVtableHooks()
    {
        IDXGISwapChain1* tempSwapChain = nullptr;
        ID3D12CommandQueue* tempQueue = nullptr;
        ID3D12Device* tempDevice = nullptr;
        HWND tempWindow = nullptr;
        ATOM tempClass = 0;

        if (!CreateBootstrapObjects(&tempSwapChain, &tempQueue, &tempDevice, tempWindow, tempClass))
        {
            DebugLog("[Hook] Failed to create bootstrap D3D12 objects.\n");
            SafeRelease(tempSwapChain);
            SafeRelease(tempQueue);
            SafeRelease(tempDevice);
            if (tempWindow) DestroyWindow(tempWindow);
            if (tempClass) UnregisterClassW(MAKEINTATOM(tempClass), GetModuleHandleW(nullptr));
            return false;
        }

        IDXGISwapChain3* tempSwapChain3 = nullptr;
        if (FAILED(tempSwapChain->QueryInterface(IID_PPV_ARGS(&tempSwapChain3))) || !tempSwapChain3)
        {
            DebugLog("[Hook] Bootstrap swapchain does not expose IDXGISwapChain3.\n");
            SafeRelease(tempSwapChain);
            SafeRelease(tempQueue);
            SafeRelease(tempDevice);
            if (tempWindow) DestroyWindow(tempWindow);
            if (tempClass) UnregisterClassW(MAKEINTATOM(tempClass), GetModuleHandleW(nullptr));
            return false;
        }

        void** swapVtable = *reinterpret_cast<void***>(tempSwapChain);
        void** swapVtable3 = *reinterpret_cast<void***>(tempSwapChain3);
        void** queueVtable = *reinterpret_cast<void***>(tempQueue);

        bool ok = PatchVtableSlot(&queueVtable[10], reinterpret_cast<void*>(HookExecuteCommandLists), g_executeHook);
        if (ok)
        {
            g_originalExecute = reinterpret_cast<ExecuteCommandListsFn>(g_executeHook.Original);
            ok = PatchVtableSlot(&swapVtable[8], reinterpret_cast<void*>(HookPresent), g_presentHook);
        }
        if (ok)
        {
            g_originalPresent = reinterpret_cast<PresentFn>(g_presentHook.Original);
            ok = PatchVtableSlot(&swapVtable[13], reinterpret_cast<void*>(HookResizeBuffers), g_resizeHook);
        }
        if (ok)
        {
            g_originalResize = reinterpret_cast<ResizeBuffersFn>(g_resizeHook.Original);
            // IDXGISwapChain1::Present1 is vtable slot 22 (18 base methods + 4th SwapChain1 method).
            ok = PatchVtableSlot(&swapVtable[22], reinterpret_cast<void*>(HookPresent1), g_present1Hook);
        }
        if (ok)
        {
            g_originalPresent1 = reinterpret_cast<Present1Fn>(g_present1Hook.Original);
            // IDXGISwapChain3::ResizeBuffers1 is vtable slot 39.
            ok = PatchVtableSlot(&swapVtable3[39], reinterpret_cast<void*>(HookResizeBuffers1), g_resize1Hook);
        }
        if (ok)
            g_originalResize1 = reinterpret_cast<ResizeBuffers1Fn>(g_resize1Hook.Original);

        SafeRelease(tempSwapChain3);
        SafeRelease(tempSwapChain);
        SafeRelease(tempQueue);
        SafeRelease(tempDevice);
        if (tempWindow) DestroyWindow(tempWindow);
        if (tempClass) UnregisterClassW(MAKEINTATOM(tempClass), GetModuleHandleW(nullptr));

        if (!ok)
        {
            RestoreVtableSlot(g_resize1Hook);
            RestoreVtableSlot(g_present1Hook);
            RestoreVtableSlot(g_resizeHook);
            RestoreVtableSlot(g_presentHook);
            RestoreVtableSlot(g_executeHook);
            g_originalPresent = nullptr;
            g_originalPresent1 = nullptr;
            g_originalResize = nullptr;
            g_originalResize1 = nullptr;
            g_originalExecute = nullptr;
            return false;
        }
        return true;
    }
}

void DebugLog(const char* format, ...)
{
    if (!format)
        return;
    char buffer[4096]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    OutputDebugStringA(buffer);

    FILE* file = nullptr;
    if (fopen_s(&file, "incursion_cheat_fixed.log", "a") == 0 && file)
    {
        std::fputs(buffer, file);
        std::fclose(file);
    }
}

bool InitializeHook()
{
    std::lock_guard<std::mutex> initLock(g_initMutex);
    if (g_hookInstalled.load())
        return true;

    g_shuttingDown.store(false);
    g_presentObserved.store(false);
    g_waitingQueueLogged.store(false);
    if (!Memory::Initialize())
    {
        DebugLog("[Hook] Failed to initialize internal module memory.\n");
        return false;
    }

    DebugLog("[Hook] IncursionCheat fixed core starting. EXE base=%p size=0x%zx\n",
             reinterpret_cast<void*>(Memory::GetBase()), Memory::GetModuleSize());

    while (!g_shuttingDown.load())
    {
        if (GetModuleHandleW(L"dxgi.dll") && GetModuleHandleW(L"d3d12.dll"))
            break;
        Sleep(100);
    }
    if (g_shuttingDown.load())
        return false;

    if (!InstallComVtableHooks())
    {
        DebugLog("[Hook] COM vtable hook installation failed.\n");
        return false;
    }

    g_hookInstalled.store(true);
    DebugLog("[Hook] Present, Present1, ResizeBuffers, ResizeBuffers1 and ExecuteCommandLists hooks installed.\n");
    return true;
}

void ShutdownHook()
{
    if (g_shuttingDown.exchange(true))
        return;

    g_hookInstalled.store(false);

    RestoreVtableSlot(g_resize1Hook);
    RestoreVtableSlot(g_present1Hook);
    RestoreVtableSlot(g_resizeHook);
    RestoreVtableSlot(g_presentHook);
    RestoreVtableSlot(g_executeHook);

    {
        std::lock_guard<std::mutex> lock(g_renderMutex);
        ReleaseRendererLocked();
    }

    g_gameQueue.store(nullptr);
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        for (ID3D12CommandQueue* queue : g_directQueueCandidates)
            if (queue) queue->Release();
        g_directQueueCandidates.clear();
    }

    g_originalPresent = nullptr;
    g_originalPresent1 = nullptr;
    g_originalResize = nullptr;
    g_originalResize1 = nullptr;
    g_originalExecute = nullptr;
    DebugLog("[Hook] Shutdown complete.\n");
}

bool IsHookInstalled() { return g_hookInstalled.load(); }
bool IsRendererInitialized() { return g_rendererInitialized.load(); }
bool HasCapturedCommandQueue() { return g_gameQueue.load() != nullptr; }
