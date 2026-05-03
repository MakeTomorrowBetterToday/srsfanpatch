#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <unknwn.h>
#include <dinput.h>
#include <d3d9.h>
#include <Xinput.h>
#include <stdint.h>

static HMODULE g_realDinput8 = nullptr;
static HMODULE g_thisModule = nullptr;
static HWND g_ownedWindow = nullptr;
static HWND g_originalGameWindow = nullptr;
static HANDLE g_windowReady = nullptr;
static HANDLE g_windowThread = nullptr;
static HANDLE g_controllerThread = nullptr;
static HWND (WINAPI* g_realGetForegroundWindow)() = nullptr;
static HMODULE g_realD3d9 = nullptr;
static IDirect3D9* (WINAPI* g_realDirect3DCreate9)(UINT) = nullptr;
static volatile LONG g_iatPatched = 0;
static volatile LONG g_d3dIatPatched = 0;
static DWORD g_lastStoreLogTick = 0;
static DWORD g_lastAliveLogTick = 0;
static DWORD g_lastViewportLogTick = 0;
static DWORD g_lastPresentLogTick = 0;
static volatile LONG g_deviceCreated = 0;
static volatile LONG g_sizingWindow = 0;
static volatile LONG g_displayMode = 0;
static UINT g_backBufferWidth = 0;
static UINT g_backBufferHeight = 0;
static RECT g_fitRect = {};
static UINT g_forcedBackBufferWidth = 0;
static UINT g_forcedBackBufferHeight = 0;
static UINT g_srsDrawWidth = 0;
static UINT g_srsDrawHeight = 0;
static IDirect3DSurface9* g_scaleSurface = nullptr;
static UINT g_scaleSurfaceWidth = 0;
static UINT g_scaleSurfaceHeight = 0;
static D3DFORMAT g_scaleSurfaceFormat = D3DFMT_UNKNOWN;

using DevicePresent_t = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using DeviceReset_t = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using DeviceSetViewport_t = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const D3DVIEWPORT9*);
static DevicePresent_t g_realDevicePresent = nullptr;
static DeviceReset_t g_realDeviceReset = nullptr;
static DeviceSetViewport_t g_realDeviceSetViewport = nullptr;
static volatile LONG g_deviceVtablePatched = 0;

using DirectInput8Create_t = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DirectInput8CreateDevice_t = HRESULT (STDMETHODCALLTYPE*)(IDirectInput8A*, REFGUID, LPDIRECTINPUTDEVICE8A*, LPUNKNOWN);
using DirectInputDeviceSetCooperativeLevel_t = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8A*, HWND, DWORD);
static DirectInput8Create_t g_realDirectInput8Create = nullptr;
static DirectInput8CreateDevice_t g_realDirectInput8CreateDevice = nullptr;
static DirectInputDeviceSetCooperativeLevel_t g_realDirectInputDeviceSetCooperativeLevel = nullptr;
static volatile LONG g_directInputVtablePatched = 0;
static volatile LONG g_directInputDeviceVtablePatched = 0;
static volatile LONG g_controllerThreadStarted = 0;
static HMODULE g_realXInput = nullptr;

using XInputGetState_t = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
static XInputGetState_t g_realXInputGetState = nullptr;
using SetProcessDpiAwarenessContext_t = BOOL (WINAPI*)(HANDLE);
using SetProcessDPIAware_t = BOOL (WINAPI*)();

struct DisplayModePromptState
{
    LONG mode;
    bool done;
    HBITMAP letterboxBitmap;
    HBITMAP widescreenBitmap;
    int letterboxBitmapW;
    int letterboxBitmapH;
    int widescreenBitmapW;
    int widescreenBitmapH;
    RECT letterboxRect;
    RECT widescreenRect;
    int cursorShowBalance;
    bool hadCursorClip;
    RECT previousCursorClip;
};

static void EnsureOwnedWindow();
static void StoreOwnedWindowGlobal();
static void LogLine(const char* text);
static void LogHwnd(const char* label, HWND hwnd);
static void GetPatchAssetPath(char* path, DWORD pathSize, const char* fileName);
static RECT GetNearestMonitorRect(HWND hwnd);
static RECT GetTargetWindowRect(HWND hwnd);
static void FitWindowToNearestMonitor(HWND hwnd);
static void FitWindowToBackbuffer(HWND hwnd, UINT backBufferW, UINT backBufferH);
static LONG ResolveDisplayMode();
static LONG PromptDisplayMode(HWND owner);
static bool LoadPromptBitmap(const char* fileName, HBITMAP* bitmap, int* width, int* height);
static void DrawPromptBitmap(HDC dc, HBITMAP bitmap, const RECT& target);
static void EnterPromptCursorMode(DisplayModePromptState* state);
static void LeavePromptCursorMode(DisplayModePromptState* state);
static LRESULT CALLBACK DisplayModePromptWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void LoadPatchConfig();
static LRESULT CALLBACK OwnedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void PatchDirectInput8Vtable(void* directInput);
static void PatchDirectInputDevice8Vtable(void* device);
static void EnsureControllerThread();
static void PatchDevicePresentVtable(IDirect3DDevice9* device);
static void PatchDeviceViewportVtable(IDirect3DDevice9* device);
static void ApplyPatchPresentationParameters(D3DPRESENT_PARAMETERS* pp, HWND hwnd, const char* label, bool forceBackBuffer);
static void ReleasePatchDeviceResources();
static void MakeProcessDpiAware();

class SrsDirect3D9 final : public IDirect3D9
{
public:
    explicit SrsDirect3D9(IDirect3D9* inner) : m_ref(1), m_inner(inner) {}
    ~SrsDirect3D9() = default;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) override
    {
        if (!ppvObj) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDirect3D9) {
            *ppvObj = static_cast<IDirect3D9*>(this);
            AddRef();
            return S_OK;
        }
        return m_inner->QueryInterface(riid, ppvObj);
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return InterlockedIncrement(&m_ref);
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG ref = InterlockedDecrement(&m_ref);
        if (ref == 0) {
            m_inner->Release();
            delete this;
        }
        return ref;
    }

    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void* pInitializeFunction) override { return m_inner->RegisterSoftwareDevice(pInitializeFunction); }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return m_inner->GetAdapterCount(); }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9* pIdentifier) override { return m_inner->GetAdapterIdentifier(Adapter, Flags, pIdentifier); }
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override { return m_inner->GetAdapterModeCount(Adapter, Format); }
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE* pMode) override { return m_inner->EnumAdapterModes(Adapter, Format, Mode, pMode); }
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE* pMode) override { return m_inner->GetAdapterDisplayMode(Adapter, pMode); }
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) override { return m_inner->CheckDeviceType(Adapter, DevType, AdapterFormat, BackBufferFormat, bWindowed); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override { return m_inner->CheckDeviceFormat(Adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat); }
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD* pQualityLevels) override { return m_inner->CheckDeviceMultiSampleType(Adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels); }
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override { return m_inner->CheckDepthStencilMatch(Adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat); }
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) override { return m_inner->CheckDeviceFormatConversion(Adapter, DeviceType, SourceFormat, TargetFormat); }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9* pCaps) override { return m_inner->GetDeviceCaps(Adapter, DeviceType, pCaps); }
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT Adapter) override { return m_inner->GetAdapterMonitor(Adapter); }

    HRESULT STDMETHODCALLTYPE CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface) override
    {
        EnsureOwnedWindow();
        StoreOwnedWindowGlobal();
        LoadPatchConfig();
        HWND hwnd = (g_ownedWindow && IsWindow(g_ownedWindow)) ? g_ownedWindow : hFocusWindow;
        ResolveDisplayMode();
        if (pPresentationParameters && hwnd) {
            char before[192];
            wsprintfA(before, "CreateDevice pp before W=%u H=%u Windowed=%u Swap=%u HWND=0x%08X",
                pPresentationParameters->BackBufferWidth,
                pPresentationParameters->BackBufferHeight,
                (unsigned)pPresentationParameters->Windowed,
                (unsigned)pPresentationParameters->SwapEffect,
                (unsigned)(uintptr_t)pPresentationParameters->hDeviceWindow);
            LogLine(before);

            ApplyPatchPresentationParameters(pPresentationParameters, hwnd, "CreateDevice", true);
            RECT mr = GetNearestMonitorRect(hwnd);

            char after[192];
            wsprintfA(after, "CreateDevice pp after W=%u H=%u Windowed=%u Swap=%u HWND=0x%08X monitor=%ux%u",
                pPresentationParameters->BackBufferWidth,
                pPresentationParameters->BackBufferHeight,
                (unsigned)pPresentationParameters->Windowed,
                (unsigned)pPresentationParameters->SwapEffect,
                (unsigned)(uintptr_t)pPresentationParameters->hDeviceWindow,
                (UINT)(mr.right - mr.left),
                (UINT)(mr.bottom - mr.top));
            LogLine(after);
        }
        LogHwnd("IDirect3D9::CreateDevice using HWND", hwnd);
        if (hFocusWindow && hFocusWindow != hwnd && IsWindow(hFocusWindow)) {
            g_originalGameWindow = hFocusWindow;
            LogHwnd("captured original game focus HWND", g_originalGameWindow);
        }
        D3DPRESENT_PARAMETERS fallbackPresentation = {};
        bool haveFallbackPresentation = false;
        if (pPresentationParameters) {
            fallbackPresentation = *pPresentationParameters;
            haveFallbackPresentation = true;
        }

        HRESULT hr = m_inner->CreateDevice(Adapter, DeviceType, hwnd, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);
        if (FAILED(hr) && haveFallbackPresentation && g_forcedBackBufferWidth && g_forcedBackBufferHeight) {
            char retry[192];
            wsprintfA(retry, "CreateDevice failed 0x%08X with forced backbuffer; retrying forced size with discard swap", (unsigned)hr);
            LogLine(retry);

            fallbackPresentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
            hr = m_inner->CreateDevice(Adapter, DeviceType, hwnd, BehaviorFlags, &fallbackPresentation, ppReturnedDeviceInterface);
            if (SUCCEEDED(hr) && pPresentationParameters) {
                *pPresentationParameters = fallbackPresentation;
            }
        }
        if (FAILED(hr) && haveFallbackPresentation && g_forcedBackBufferWidth && g_forcedBackBufferHeight) {
            char retry[192];
            wsprintfA(retry, "CreateDevice failed 0x%08X with forced discard backbuffer; retrying game requested size", (unsigned)hr);
            LogLine(retry);

            fallbackPresentation.BackBufferWidth = g_srsDrawWidth ? g_srsDrawWidth : fallbackPresentation.BackBufferWidth;
            fallbackPresentation.BackBufferHeight = g_srsDrawHeight ? g_srsDrawHeight : fallbackPresentation.BackBufferHeight;
            ApplyPatchPresentationParameters(&fallbackPresentation, hwnd, "CreateDevice fallback", false);
            fallbackPresentation.SwapEffect = D3DSWAPEFFECT_DISCARD;

            hr = m_inner->CreateDevice(Adapter, DeviceType, hwnd, BehaviorFlags, &fallbackPresentation, ppReturnedDeviceInterface);
            if (SUCCEEDED(hr) && pPresentationParameters) {
                *pPresentationParameters = fallbackPresentation;
            }
        }
        char buf[128];
        wsprintfA(buf, "IDirect3D9::CreateDevice result 0x%08X", (unsigned)hr);
        LogLine(buf);
        if (SUCCEEDED(hr)) {
            g_backBufferWidth = pPresentationParameters ? pPresentationParameters->BackBufferWidth : 0;
            g_backBufferHeight = pPresentationParameters ? pPresentationParameters->BackBufferHeight : 0;
            PatchDevicePresentVtable(ppReturnedDeviceInterface ? *ppReturnedDeviceInterface : nullptr);
            if (g_originalGameWindow && hwnd && g_originalGameWindow != hwnd && IsWindow(g_originalGameWindow)) {
                ShowWindow(g_originalGameWindow, SW_HIDE);
                SetWindowPos(g_originalGameWindow, HWND_BOTTOM, 0, 0, 1, 1, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                LogHwnd("hid original game focus HWND", g_originalGameWindow);
            }
            InterlockedExchange(&g_deviceCreated, 1);
            EnsureControllerThread();
        }
        return hr;
    }

private:
    volatile LONG m_ref;
    IDirect3D9* m_inner;
};

static void LogLine(const char* text)
{
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    char* slash = path;
    for (char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') slash = p + 1;
    }
    *slash = 0;
    lstrcatA(path, "srs_owned_window.log");
    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, text, lstrlenA(text), &written, nullptr);
    WriteFile(h, "\r\n", 2, &written, nullptr);
    CloseHandle(h);
}

static void LogHwnd(const char* label, HWND hwnd)
{
    char buf[128];
    wsprintfA(buf, "%s 0x%08X", label, (unsigned)(uintptr_t)hwnd);
    LogLine(buf);
}

static void GetPatchIniPath(char* path, DWORD pathSize)
{
    DWORD n = GetModuleFileNameA(g_thisModule ? g_thisModule : GetModuleHandleA(nullptr), path, pathSize);
    if (!n || n >= pathSize) {
        if (pathSize) path[0] = 0;
        return;
    }
    char* slash = path;
    for (char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') slash = p + 1;
    }
    *slash = 0;
    lstrcatA(path, "srs-modern-patch.ini");
}

static void GetPatchAssetPath(char* path, DWORD pathSize, const char* fileName)
{
    DWORD n = GetModuleFileNameA(g_thisModule ? g_thisModule : GetModuleHandleA(nullptr), path, pathSize);
    if (!n || n >= pathSize) {
        if (pathSize) path[0] = 0;
        return;
    }
    char* slash = path;
    for (char* p = path; *p; ++p) {
        if (*p == '\\' || *p == '/') slash = p + 1;
    }
    *slash = 0;
    lstrcatA(path, "srs-modern-patch-assets\\");
    lstrcatA(path, fileName);
}

static LONG ResolveDisplayMode()
{
    LoadPatchConfig();
    LONG mode = InterlockedCompareExchange(&g_displayMode, 0, 0);
    if (mode != 0) return mode;

    char ini[MAX_PATH] = {};
    char value[32] = {};
    GetPatchIniPath(ini, MAX_PATH);
    if (ini[0]) {
        GetPrivateProfileStringA("Display", "Mode", "Prompt", value, sizeof(value), ini);
    } else {
        lstrcpyA(value, "Prompt");
    }

    if (lstrcmpiA(value, "Widescreen") == 0) {
        mode = 1;
    } else if (lstrcmpiA(value, "Letterbox") == 0) {
        mode = 2;
    } else {
        HWND owner = (g_ownedWindow && IsWindow(g_ownedWindow)) ? g_ownedWindow : nullptr;
        mode = PromptDisplayMode(owner);
    }

    InterlockedCompareExchange(&g_displayMode, mode, 0);
    LogLine(mode == 2 ? "display mode: letterbox" : "display mode: widescreen");
    return mode;
}

static LONG PromptDisplayMode(HWND owner)
{
    DisplayModePromptState state = {};
    state.mode = 2;
    state.done = false;
    if (!LoadPromptBitmap("letterbox.bmp", &state.letterboxBitmap, &state.letterboxBitmapW, &state.letterboxBitmapH) ||
        !LoadPromptBitmap("widescreen.bmp", &state.widescreenBitmap, &state.widescreenBitmapW, &state.widescreenBitmapH)) {
        if (state.letterboxBitmap) DeleteObject(state.letterboxBitmap);
        if (state.widescreenBitmap) DeleteObject(state.widescreenBitmap);
        LogLine("display mode image prompt assets missing; defaulting to letterbox");
        return state.mode;
    }

    HINSTANCE inst = reinterpret_cast<HINSTANCE>(g_thisModule ? g_thisModule : GetModuleHandleA(nullptr));
    const char* className = "SRSDisplayModePrompt";

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DisplayModePromptWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_HAND);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = className;
    RegisterClassExA(&wc);
    RECT mr = GetNearestMonitorRect(owner);

    int monitorW = mr.right - mr.left;
    int monitorH = mr.bottom - mr.top;
    int pad = 8;
    int gap = 12;
    int contentW = state.letterboxBitmapW + state.widescreenBitmapW;
    int contentH = state.letterboxBitmapH > state.widescreenBitmapH ? state.letterboxBitmapH : state.widescreenBitmapH;
    int maxW = monitorW > 0 ? MulDiv(monitorW, 80, 100) : contentW + gap + (pad * 2);
    int maxH = monitorH > 0 ? MulDiv(monitorH, 75, 100) : contentH + (pad * 2);
    int availableW = maxW - gap - (pad * 2);
    int availableH = maxH - (pad * 2);
    int scaledLetterboxW = state.letterboxBitmapW;
    int scaledLetterboxH = state.letterboxBitmapH;
    int scaledWidescreenW = state.widescreenBitmapW;
    int scaledWidescreenH = state.widescreenBitmapH;

    if (availableW > 0 && availableH > 0) {
        int scaleByW = MulDiv(1000, availableW, contentW);
        int scaleByH = MulDiv(1000, availableH, contentH);
        int scaleFactor = scaleByW < scaleByH ? scaleByW : scaleByH;
        if (scaleFactor > 1000) scaleFactor = 1000;
        if (scaleFactor <= 0) scaleFactor = 1000;
        scaledLetterboxW = MulDiv(state.letterboxBitmapW, scaleFactor, 1000);
        scaledLetterboxH = MulDiv(state.letterboxBitmapH, scaleFactor, 1000);
        scaledWidescreenW = MulDiv(state.widescreenBitmapW, scaleFactor, 1000);
        scaledWidescreenH = MulDiv(state.widescreenBitmapH, scaleFactor, 1000);
    }

    if (scaledLetterboxW < 1) scaledLetterboxW = 1;
    if (scaledLetterboxH < 1) scaledLetterboxH = 1;
    if (scaledWidescreenW < 1) scaledWidescreenW = 1;
    if (scaledWidescreenH < 1) scaledWidescreenH = 1;

    int width = pad + scaledLetterboxW + gap + scaledWidescreenW + pad;
    int innerH = scaledLetterboxH > scaledWidescreenH ? scaledLetterboxH : scaledWidescreenH;
    int height = pad + innerH + pad;
    state.letterboxRect.left = pad;
    state.letterboxRect.top = pad + ((innerH - scaledLetterboxH) / 2);
    state.letterboxRect.right = state.letterboxRect.left + scaledLetterboxW;
    state.letterboxRect.bottom = state.letterboxRect.top + scaledLetterboxH;
    state.widescreenRect.left = state.letterboxRect.right + gap;
    state.widescreenRect.top = pad + ((innerH - scaledWidescreenH) / 2);
    state.widescreenRect.right = state.widescreenRect.left + scaledWidescreenW;
    state.widescreenRect.bottom = state.widescreenRect.top + scaledWidescreenH;

    int x = mr.left + ((mr.right - mr.left) - width) / 2;
    int y = mr.top + ((mr.bottom - mr.top) - height) / 2;
    if (owner && IsWindow(owner)) {
        RECT ownerRect = {};
        if (GetWindowRect(owner, &ownerRect)) {
            x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
            y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
        }
    }

    HWND dialog = CreateWindowExA(
        WS_EX_TOPMOST,
        className,
        "SRS Modern Patch",
        WS_POPUP,
        x, y, width, height,
        owner, nullptr, inst, &state);

    if (!dialog) {
        DeleteObject(state.letterboxBitmap);
        DeleteObject(state.widescreenBitmap);
        LogLine("display mode prompt window failed; defaulting to letterbox");
        return state.mode;
    }

    BOOL ownerWasEnabled = FALSE;
    if (owner && IsWindow(owner)) {
        ownerWasEnabled = IsWindowEnabled(owner);
        EnableWindow(owner, FALSE);
    }

    ShowWindow(dialog, SW_SHOWNORMAL);
    SetWindowPos(dialog, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    EnterPromptCursorMode(&state);
    SetForegroundWindow(dialog);
    SetFocus(dialog);

    MSG msg = {};
    while (!state.done) {
        BOOL got = GetMessageA(&msg, nullptr, 0, 0);
        if (got <= 0) {
            state.done = true;
            if (got == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        if (!IsDialogMessageA(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    if (owner && IsWindow(owner) && ownerWasEnabled) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    LeavePromptCursorMode(&state);
    DeleteObject(state.letterboxBitmap);
    DeleteObject(state.widescreenBitmap);
    LogLine(state.mode == 2 ? "display mode prompt selected letterbox" : "display mode prompt selected widescreen");
    return state.mode;
}

static bool LoadPromptBitmap(const char* fileName, HBITMAP* bitmap, int* width, int* height)
{
    if (!bitmap || !width || !height) return false;
    *bitmap = nullptr;
    *width = 0;
    *height = 0;
    char path[MAX_PATH] = {};
    GetPatchAssetPath(path, MAX_PATH, fileName);
    if (!path[0]) return false;
    HBITMAP loaded = (HBITMAP)LoadImageA(nullptr, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!loaded) {
        char buf[192];
        wsprintfA(buf, "failed to load display mode bitmap %s", fileName);
        LogLine(buf);
        return false;
    }
    BITMAP info = {};
    if (!GetObjectA(loaded, sizeof(info), &info)) {
        DeleteObject(loaded);
        return false;
    }
    *bitmap = loaded;
    *width = info.bmWidth;
    *height = info.bmHeight;
    return true;
}

static void DrawPromptBitmap(HDC dc, HBITMAP bitmap, const RECT& target)
{
    if (!dc || !bitmap || target.right <= target.left || target.bottom <= target.top) return;
    BITMAP info = {};
    if (!GetObjectA(bitmap, sizeof(info), &info)) return;
    HDC mem = CreateCompatibleDC(dc);
    if (!mem) return;
    HGDIOBJ old = SelectObject(mem, bitmap);
    SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchBlt(dc,
        target.left,
        target.top,
        target.right - target.left,
        target.bottom - target.top,
        mem,
        0,
        0,
        info.bmWidth,
        info.bmHeight,
        SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void EnterPromptCursorMode(DisplayModePromptState* state)
{
    if (!state) return;
    state->cursorShowBalance = 0;
    state->hadCursorClip = false;
    state->previousCursorClip = {};

    if (GetClipCursor(&state->previousCursorClip)) {
        state->hadCursorClip = true;
        ClipCursor(nullptr);
    }

    HCURSOR cursor = LoadCursor(nullptr, IDC_HAND);
    if (cursor) SetCursor(cursor);

    for (int i = 0; i < 64; ++i) {
        int count = ShowCursor(TRUE);
        ++state->cursorShowBalance;
        if (count >= 0) break;
    }

    LogLine("display mode prompt cursor forced visible");
}

static void LeavePromptCursorMode(DisplayModePromptState* state)
{
    if (!state) return;

    for (int i = 0; i < state->cursorShowBalance; ++i) {
        ShowCursor(FALSE);
    }
    state->cursorShowBalance = 0;

    if (state->hadCursorClip) {
        ClipCursor(&state->previousCursorClip);
        state->hadCursorClip = false;
    }
}

static LRESULT CALLBACK DisplayModePromptWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    DisplayModePromptState* state = reinterpret_cast<DisplayModePromptState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTA*)lp)->lpCreateParams);
        return TRUE;
    case WM_PAINT:
        if (state) {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT client = {};
            GetClientRect(hwnd, &client);
            FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
            DrawPromptBitmap(dc, state->letterboxBitmap, state->letterboxRect);
            DrawPromptBitmap(dc, state->widescreenBitmap, state->widescreenRect);
            EndPaint(hwnd, &ps);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    case WM_LBUTTONUP:
        if (state) {
            POINT pt = { (SHORT)LOWORD(lp), (SHORT)HIWORD(lp) };
            if (PtInRect(&state->widescreenRect, pt)) {
                state->mode = 1;
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (PtInRect(&state->letterboxRect, pt)) {
                state->mode = 2;
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_KEYDOWN:
        if (state) {
            if (wp == 'W') {
                state->mode = 1;
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            if (wp == VK_RETURN || wp == VK_ESCAPE || wp == 'L') {
                state->mode = 2;
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
        }
        break;
    case WM_CLOSE:
        if (state) {
            state->mode = 2;
            state->done = true;
        }
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (state && !state->done) {
            state->done = true;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void LoadPatchConfig()
{
    static volatile LONG loaded = 0;
    if (InterlockedCompareExchange(&loaded, 1, 0) != 0) return;

    char ini[MAX_PATH] = {};
    GetPatchIniPath(ini, MAX_PATH);
    if (!ini[0]) return;

    UINT w = GetPrivateProfileIntA("Render", "BackBufferWidth", 0, ini);
    UINT h = GetPrivateProfileIntA("Render", "BackBufferHeight", 0, ini);
    if (w >= 640 && h >= 480) {
        g_forcedBackBufferWidth = w;
        g_forcedBackBufferHeight = h;
    }

    char buf[160];
    wsprintfA(buf, "patch config forced backbuffer %ux%u", g_forcedBackBufferWidth, g_forcedBackBufferHeight);
    LogLine(buf);
}

static void LogUserObjectName(const char* label, HANDLE h)
{
    char name[256] = {};
    DWORD needed = 0;
    if (GetUserObjectInformationA(h, UOI_NAME, name, sizeof(name), &needed)) {
        char buf[320];
        wsprintfA(buf, "%s %s", label, name);
        LogLine(buf);
    } else {
        char buf[160];
        wsprintfA(buf, "%s <failed %lu>", label, GetLastError());
        LogLine(buf);
    }
}

static void LogWindowContext(const char* label)
{
    LogLine(label);
    LogUserObjectName("window station", GetProcessWindowStation());
    LogUserObjectName("thread desktop", GetThreadDesktop(GetCurrentThreadId()));
}

static void MakeProcessDpiAware()
{
    static volatile LONG done = 0;
    if (InterlockedCompareExchange(&done, 1, 0) != 0) return;

    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (user32) {
        auto setContext = reinterpret_cast<SetProcessDpiAwarenessContext_t>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext) {
            if (setContext(reinterpret_cast<HANDLE>(-4))) {
                LogLine("DPI awareness set to per-monitor v2");
                return;
            }
            if (setContext(reinterpret_cast<HANDLE>(-3))) {
                LogLine("DPI awareness set to per-monitor");
                return;
            }
        }

        auto setAware = reinterpret_cast<SetProcessDPIAware_t>(GetProcAddress(user32, "SetProcessDPIAware"));
        if (setAware && setAware()) {
            LogLine("DPI awareness set to system-aware");
            return;
        }
    }

    LogLine("failed to set DPI awareness");
}

static RECT GetNearestMonitorRect(HWND hwnd)
{
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (monitor && GetMonitorInfoA(monitor, &mi)) {
        return mi.rcMonitor;
    }

    RECT r = {};
    r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right = r.left + GetSystemMetrics(SM_CXSCREEN);
    r.bottom = r.top + GetSystemMetrics(SM_CYSCREEN);
    if (r.right <= r.left) r.right = r.left + 1024;
    if (r.bottom <= r.top) r.bottom = r.top + 768;
    return r;
}

static RECT GetTargetWindowRect(HWND hwnd)
{
    RECT r = GetNearestMonitorRect(hwnd);
    if (ResolveDisplayMode() != 2) {
        return r;
    }

    int monitorW = r.right - r.left;
    int monitorH = r.bottom - r.top;
    if (monitorW <= 0 || monitorH <= 0) return r;

    int targetW = monitorW;
    int targetH = (targetW * 3) / 4;
    if (targetH > monitorH) {
        targetH = monitorH;
        targetW = (targetH * 4) / 3;
    }

    RECT out = {};
    out.left = r.left + (monitorW - targetW) / 2;
    out.top = r.top + (monitorH - targetH) / 2;
    out.right = out.left + targetW;
    out.bottom = out.top + targetH;
    return out;
}

static void FitWindowToNearestMonitor(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd)) return;
    if (InterlockedCompareExchange(&g_sizingWindow, 1, 0) != 0) return;
    RECT r = GetNearestMonitorRect(hwnd);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0) w = 1024;
    if (h <= 0) h = 768;
    SetWindowPos(hwnd, HWND_TOP, r.left, r.top, w, h, SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    RECT actual = {};
    GetWindowRect(hwnd, &actual);
    char buf[192];
    wsprintfA(buf, "fit window to monitor requested %dx%d actual %ldx%ld at %ld,%ld",
        w, h, actual.right - actual.left, actual.bottom - actual.top, actual.left, actual.top);
    LogLine(buf);
    InterlockedExchange(&g_sizingWindow, 0);
}

static void FitWindowToBackbuffer(HWND hwnd, UINT backBufferW, UINT backBufferH)
{
    if (!hwnd || !IsWindow(hwnd) || backBufferW < 640 || backBufferH < 480) return;
    if (InterlockedCompareExchange(&g_sizingWindow, 1, 0) != 0) return;

    RECT m = GetNearestMonitorRect(hwnd);
    int monitorW = m.right - m.left;
    int monitorH = m.bottom - m.top;
    int targetW = (int)backBufferW;
    int targetH = (int)backBufferH;

    if (monitorW > 0 && monitorH > 0 && (targetW > monitorW || targetH > monitorH)) {
        int byWidthH = (targetH * monitorW) / targetW;
        if (byWidthH <= monitorH) {
            targetH = byWidthH;
            targetW = monitorW;
        } else {
            targetW = (targetW * monitorH) / targetH;
            targetH = monitorH;
        }
    }

    int x = m.left + ((monitorW - targetW) / 2);
    int y = m.top + ((monitorH - targetH) / 2);
    SetWindowPos(hwnd, HWND_TOP, x, y, targetW, targetH, SWP_NOOWNERZORDER | SWP_NOACTIVATE);

    RECT actual = {};
    GetWindowRect(hwnd, &actual);
    char buf[160];
    wsprintfA(buf, "fit window to backbuffer %ux%u -> requested %dx%d actual %ldx%ld at %ld,%ld",
        backBufferW,
        backBufferH,
        targetW,
        targetH,
        actual.right - actual.left,
        actual.bottom - actual.top,
        actual.left,
        actual.top);
    LogLine(buf);
    InterlockedExchange(&g_sizingWindow, 0);
}

static HRESULT STDMETHODCALLTYPE SrsDirectInputDeviceSetCooperativeLevel(IDirectInputDevice8A* device, HWND hwnd, DWORD flags)
{
    HWND target = (g_ownedWindow && IsWindow(g_ownedWindow)) ? g_ownedWindow : hwnd;
    DWORD adjustedFlags = flags & ~DISCL_NOWINKEY;
    char buf[192];
    wsprintfA(buf, "DirectInput SetCooperativeLevel HWND 0x%08X -> 0x%08X flags=0x%08X -> 0x%08X",
        (unsigned)(uintptr_t)hwnd,
        (unsigned)(uintptr_t)target,
        (unsigned)flags,
        (unsigned)adjustedFlags);
    LogLine(buf);
    return g_realDirectInputDeviceSetCooperativeLevel(device, target, adjustedFlags);
}

static void PatchDirectInputDevice8Vtable(void* device)
{
    if (!device) return;
    if (InterlockedCompareExchange(&g_directInputDeviceVtablePatched, 1, 1) == 1) return;

    void*** object = reinterpret_cast<void***>(device);
    void** vtbl = object ? *object : nullptr;
    if (!vtbl) return;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtbl[13], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for IDirectInputDevice8 SetCooperativeLevel");
        return;
    }

    g_realDirectInputDeviceSetCooperativeLevel = reinterpret_cast<DirectInputDeviceSetCooperativeLevel_t>(vtbl[13]);
    vtbl[13] = reinterpret_cast<void*>(&SrsDirectInputDeviceSetCooperativeLevel);

    DWORD ignored = 0;
    VirtualProtect(&vtbl[13], sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtbl[13], sizeof(void*));
    InterlockedExchange(&g_directInputDeviceVtablePatched, 1);
    LogLine("patched IDirectInputDevice8 SetCooperativeLevel vtable entry");
}

static HRESULT STDMETHODCALLTYPE SrsDirectInput8CreateDevice(IDirectInput8A* directInput, REFGUID rguid, LPDIRECTINPUTDEVICE8A* device, LPUNKNOWN outer)
{
    HRESULT hr = g_realDirectInput8CreateDevice(directInput, rguid, device, outer);
    char buf[128];
    wsprintfA(buf, "DirectInput8 CreateDevice result 0x%08X", (unsigned)hr);
    LogLine(buf);
    if (SUCCEEDED(hr) && device && *device) {
        PatchDirectInputDevice8Vtable(*device);
    }
    return hr;
}

static void PatchDirectInput8Vtable(void* directInput)
{
    if (!directInput) return;
    if (InterlockedCompareExchange(&g_directInputVtablePatched, 1, 1) == 1) return;

    void*** object = reinterpret_cast<void***>(directInput);
    void** vtbl = object ? *object : nullptr;
    if (!vtbl) return;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtbl[3], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for IDirectInput8 CreateDevice");
        return;
    }

    g_realDirectInput8CreateDevice = reinterpret_cast<DirectInput8CreateDevice_t>(vtbl[3]);
    vtbl[3] = reinterpret_cast<void*>(&SrsDirectInput8CreateDevice);

    DWORD ignored = 0;
    VirtualProtect(&vtbl[3], sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtbl[3], sizeof(void*));
    InterlockedExchange(&g_directInputVtablePatched, 1);
    LogLine("patched IDirectInput8 CreateDevice vtable entry");
}

static HRESULT STDMETHODCALLTYPE SrsDevicePresent(IDirect3DDevice9* device, const RECT* sourceRect, const RECT* destRect, HWND destWindowOverride, const RGNDATA* dirtyRegion)
{
    HWND hwnd = (g_ownedWindow && IsWindow(g_ownedWindow)) ? g_ownedWindow : destWindowOverride;
    RECT client = {};
    if (hwnd && IsWindow(hwnd)) {
        GetClientRect(hwnd, &client);
    }

    RECT source = {};
    source.left = 0;
    source.top = 0;
    source.right = (LONG)g_backBufferWidth;
    source.bottom = (LONG)g_backBufferHeight;

    RECT target = {};
    target.left = 0;
    target.top = 0;
    target.right = client.right - client.left;
    target.bottom = client.bottom - client.top;

    LONG clientW = target.right - target.left;
    LONG clientH = target.bottom - target.top;
    LONG sourceW = source.right - source.left;
    LONG sourceH = source.bottom - source.top;
    LONG mode = ResolveDisplayMode();
    bool letterbox = (mode == 2 && clientW > 0 && clientH > 0 && sourceW > 0 && sourceH > 0);
    if (letterbox) {
        LONG fittedW = clientW;
        LONG fittedH = MulDiv(fittedW, sourceH, sourceW);
        if (fittedH > clientH) {
            fittedH = clientH;
            fittedW = MulDiv(fittedH, sourceW, sourceH);
        }
        target.left = (clientW - fittedW) / 2;
        target.top = (clientH - fittedH) / 2;
        target.right = target.left + fittedW;
        target.bottom = target.top + fittedH;

        static LONG lastClientW = -1;
        static LONG lastClientH = -1;
        static RECT lastTarget = { -1, -1, -1, -1 };
        bool fillBars =
            lastClientW != clientW ||
            lastClientH != clientH ||
            lastTarget.left != target.left ||
            lastTarget.top != target.top ||
            lastTarget.right != target.right ||
            lastTarget.bottom != target.bottom;

        if (fillBars) {
            HDC dc = GetDC(hwnd);
            if (dc) {
                RECT bars[4] = {
                    { 0, 0, clientW, target.top },
                    { 0, target.bottom, clientW, clientH },
                    { 0, target.top, target.left, target.bottom },
                    { target.right, target.top, clientW, target.bottom }
                };
                for (int i = 0; i < 4; ++i) {
                    if (bars[i].right > bars[i].left && bars[i].bottom > bars[i].top) {
                        FillRect(dc, &bars[i], (HBRUSH)GetStockObject(BLACK_BRUSH));
                    }
                }
                ReleaseDC(hwnd, dc);
            }
            lastClientW = clientW;
            lastClientH = clientH;
            lastTarget = target;
        }
    }

    const RECT* sourceOut = (source.right > 0 && source.bottom > 0) ? &source : nullptr;
    const RECT* targetOut = (target.right > 0 && target.bottom > 0) ? &target : nullptr;

    DWORD now = GetTickCount();
    if (now - g_lastPresentLogTick > 3000) {
        g_lastPresentLogTick = now;
        char buf[192];
        wsprintfA(buf, "Present %s source %ldx%ld to dest %ld,%ld,%ld,%ld in client %ldx%ld",
            letterbox ? "letterbox" : "mapping",
            sourceW,
            sourceH,
            target.left,
            target.top,
            target.right,
            target.bottom,
            clientW,
            clientH);
        LogLine(buf);
    }
    return g_realDevicePresent(device, sourceOut, targetOut, hwnd, dirtyRegion);
}

static void ApplyPatchPresentationParameters(D3DPRESENT_PARAMETERS* pp, HWND hwnd, const char* label, bool forceBackBuffer)
{
    if (!pp || !hwnd) return;

    g_srsDrawWidth = pp->BackBufferWidth;
    g_srsDrawHeight = pp->BackBufferHeight;

    FitWindowToNearestMonitor(hwnd);
    pp->hDeviceWindow = hwnd;
    pp->Windowed = TRUE;
    if (forceBackBuffer && g_forcedBackBufferWidth && g_forcedBackBufferHeight) {
        pp->BackBufferWidth = g_forcedBackBufferWidth;
        pp->BackBufferHeight = g_forcedBackBufferHeight;
        char forceBuf[160];
        wsprintfA(forceBuf, "%s forced SRS backbuffer to %ux%u", label ? label : "Reset", g_forcedBackBufferWidth, g_forcedBackBufferHeight);
        LogLine(forceBuf);
    }
    pp->FullScreen_RefreshRateInHz = 0;
    pp->SwapEffect = D3DSWAPEFFECT_COPY;
}

static void ReleasePatchDeviceResources()
{
    if (g_scaleSurface) {
        g_scaleSurface->Release();
        g_scaleSurface = nullptr;
        g_scaleSurfaceWidth = 0;
        g_scaleSurfaceHeight = 0;
        g_scaleSurfaceFormat = D3DFMT_UNKNOWN;
        LogLine("released patch D3D resources before reset");
    }
}

static HRESULT STDMETHODCALLTYPE SrsDeviceReset(IDirect3DDevice9* device, D3DPRESENT_PARAMETERS* pp)
{
    EnsureOwnedWindow();
    StoreOwnedWindowGlobal();
    LoadPatchConfig();
    HWND hwnd = (g_ownedWindow && IsWindow(g_ownedWindow)) ? g_ownedWindow : nullptr;

    if (pp) {
        UINT gameRequestedW = pp->BackBufferWidth;
        UINT gameRequestedH = pp->BackBufferHeight;
        char before[192];
        wsprintfA(before, "Reset pp before W=%u H=%u Windowed=%u Swap=%u HWND=0x%08X",
            pp->BackBufferWidth,
            pp->BackBufferHeight,
            (unsigned)pp->Windowed,
            (unsigned)pp->SwapEffect,
            (unsigned)(uintptr_t)pp->hDeviceWindow);
        LogLine(before);
        if (!hwnd && pp->hDeviceWindow && IsWindow(pp->hDeviceWindow)) {
            hwnd = pp->hDeviceWindow;
        }
        g_srsDrawWidth = gameRequestedW;
        g_srsDrawHeight = gameRequestedH;
        ApplyPatchPresentationParameters(pp, hwnd, "Reset", true);
    }

    ReleasePatchDeviceResources();
    HRESULT hr = g_realDeviceReset ? g_realDeviceReset(device, pp) : D3DERR_INVALIDCALL;
    if (FAILED(hr) && pp && g_forcedBackBufferWidth && g_forcedBackBufferHeight) {
        char retry[192];
        wsprintfA(retry, "Reset failed 0x%08X with forced backbuffer; retrying forced size with discard swap", (unsigned)hr);
        LogLine(retry);

        pp->BackBufferWidth = g_forcedBackBufferWidth;
        pp->BackBufferHeight = g_forcedBackBufferHeight;
        pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
        ReleasePatchDeviceResources();
        hr = g_realDeviceReset ? g_realDeviceReset(device, pp) : D3DERR_INVALIDCALL;
    }
    if (FAILED(hr) && pp && g_forcedBackBufferWidth && g_forcedBackBufferHeight) {
        char retry[192];
        wsprintfA(retry, "Reset failed 0x%08X with forced discard backbuffer; retrying game requested size", (unsigned)hr);
        LogLine(retry);

        pp->BackBufferWidth = g_srsDrawWidth ? g_srsDrawWidth : pp->BackBufferWidth;
        pp->BackBufferHeight = g_srsDrawHeight ? g_srsDrawHeight : pp->BackBufferHeight;
        ApplyPatchPresentationParameters(pp, hwnd, "Reset fallback", false);
        pp->SwapEffect = D3DSWAPEFFECT_DISCARD;
        ReleasePatchDeviceResources();
        hr = g_realDeviceReset ? g_realDeviceReset(device, pp) : D3DERR_INVALIDCALL;
    }

    char result[192];
    wsprintfA(result, "IDirect3DDevice9::Reset result 0x%08X", (unsigned)hr);
    LogLine(result);

    if (SUCCEEDED(hr) && pp) {
        g_backBufferWidth = pp->BackBufferWidth;
        g_backBufferHeight = pp->BackBufferHeight;
        if (hwnd && IsWindow(hwnd)) {
            FitWindowToNearestMonitor(hwnd);
        }
        char after[192];
        wsprintfA(after, "Reset pp after W=%u H=%u Windowed=%u Swap=%u HWND=0x%08X",
            pp->BackBufferWidth,
            pp->BackBufferHeight,
            (unsigned)pp->Windowed,
            (unsigned)pp->SwapEffect,
            (unsigned)(uintptr_t)pp->hDeviceWindow);
        LogLine(after);
    }

    return hr;
}

static HRESULT STDMETHODCALLTYPE SrsDeviceSetViewport(IDirect3DDevice9* device, const D3DVIEWPORT9* viewport)
{
    D3DVIEWPORT9 forced = {};
    if (viewport) forced = *viewport;
    if (g_backBufferWidth >= 640 && g_backBufferHeight >= 480) {
        forced.X = 0;
        forced.Y = 0;
        forced.Width = g_backBufferWidth;
        forced.Height = g_backBufferHeight;
    } else if (g_fitRect.right > g_fitRect.left && g_fitRect.bottom > g_fitRect.top) {
        forced.X = (DWORD)g_fitRect.left;
        forced.Y = (DWORD)g_fitRect.top;
        forced.Width = (DWORD)(g_fitRect.right - g_fitRect.left);
        forced.Height = (DWORD)(g_fitRect.bottom - g_fitRect.top);
    }
    forced.MinZ = viewport ? viewport->MinZ : 0.0f;
    forced.MaxZ = viewport ? viewport->MaxZ : 1.0f;

    DWORD now = GetTickCount();
    if (now - g_lastViewportLogTick > 3000) {
        g_lastViewportLogTick = now;
        char buf[224];
        if (viewport) {
            wsprintfA(buf, "SetViewport forced from X=%lu Y=%lu W=%lu H=%lu to X=%lu Y=%lu W=%lu H=%lu active=%ux%u",
                viewport->X, viewport->Y, viewport->Width, viewport->Height, forced.X, forced.Y, forced.Width, forced.Height,
                g_backBufferWidth, g_backBufferHeight);
        } else {
            wsprintfA(buf, "SetViewport got null, forcing X=%lu Y=%lu W=%lu H=%lu active=%ux%u",
                forced.X, forced.Y, forced.Width, forced.Height, g_backBufferWidth, g_backBufferHeight);
        }
        LogLine(buf);
    }
    return g_realDeviceSetViewport(device, &forced);
}

static void PatchDeviceVtable(IDirect3DDevice9* device)
{
    if (!device) return;
    if (InterlockedCompareExchange(&g_deviceVtablePatched, 1, 1) == 1) return;

    void*** object = reinterpret_cast<void***>(device);
    void** vtbl = object ? *object : nullptr;
    if (!vtbl) return;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtbl[16], sizeof(void*) * 32, PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for IDirect3DDevice9 vtable");
        return;
    }

    g_realDeviceReset = reinterpret_cast<DeviceReset_t>(vtbl[16]);
    g_realDevicePresent = reinterpret_cast<DevicePresent_t>(vtbl[17]);
    g_realDeviceSetViewport = reinterpret_cast<DeviceSetViewport_t>(vtbl[47]);
    vtbl[16] = reinterpret_cast<void*>(&SrsDeviceReset);
    vtbl[17] = reinterpret_cast<void*>(&SrsDevicePresent);
    vtbl[47] = reinterpret_cast<void*>(&SrsDeviceSetViewport);

    DWORD ignored = 0;
    VirtualProtect(&vtbl[16], sizeof(void*) * 32, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtbl[16], sizeof(void*) * 32);
    InterlockedExchange(&g_deviceVtablePatched, 1);
    LogLine("patched IDirect3DDevice9 Reset, Present and SetViewport vtable entries");
}

static void PatchDevicePresentVtable(IDirect3DDevice9* device)
{
    if (!device) return;
    if (InterlockedCompareExchange(&g_deviceVtablePatched, 1, 1) == 1) return;

    void*** object = reinterpret_cast<void***>(device);
    void** vtbl = object ? *object : nullptr;
    if (!vtbl) return;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtbl[16], sizeof(void*) * 32, PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for IDirect3DDevice9 Reset/Present vtable");
        return;
    }

    g_realDeviceReset = reinterpret_cast<DeviceReset_t>(vtbl[16]);
    g_realDevicePresent = reinterpret_cast<DevicePresent_t>(vtbl[17]);
    g_realDeviceSetViewport = reinterpret_cast<DeviceSetViewport_t>(vtbl[47]);
    vtbl[16] = reinterpret_cast<void*>(&SrsDeviceReset);
    vtbl[17] = reinterpret_cast<void*>(&SrsDevicePresent);
    vtbl[47] = reinterpret_cast<void*>(&SrsDeviceSetViewport);

    DWORD ignored = 0;
    VirtualProtect(&vtbl[16], sizeof(void*) * 32, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtbl[16], sizeof(void*) * 32);
    InterlockedExchange(&g_deviceVtablePatched, 1);
    LogLine("patched IDirect3DDevice9 Reset, Present and SetViewport vtable entries");
}

static void PatchDeviceViewportVtable(IDirect3DDevice9* device)
{
    if (!device) return;

    void*** object = reinterpret_cast<void***>(device);
    void** vtbl = object ? *object : nullptr;
    if (!vtbl) return;

    DWORD oldProtect = 0;
    if (!VirtualProtect(&vtbl[47], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for IDirect3DDevice9 SetViewport vtable");
        return;
    }

    g_realDeviceSetViewport = reinterpret_cast<DeviceSetViewport_t>(vtbl[47]);
    vtbl[47] = reinterpret_cast<void*>(&SrsDeviceSetViewport);

    DWORD ignored = 0;
    VirtualProtect(&vtbl[47], sizeof(void*), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &vtbl[47], sizeof(void*));
    LogLine("patched IDirect3DDevice9 SetViewport vtable entry");
}

static void LoadRealDinput8()
{
    if (g_realDinput8) return;
    char sys[MAX_PATH];
    UINT n = GetSystemDirectoryA(sys, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    lstrcatA(sys, "\\dinput8.dll");
    g_realDinput8 = LoadLibraryA(sys);
    if (!g_realDinput8) {
        LogLine("failed to load system dinput8.dll");
        return;
    }
    g_realDirectInput8Create = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(g_realDinput8, "DirectInput8Create"));
}

static void LoadRealD3d9()
{
    if (g_realD3d9) return;
    char sys[MAX_PATH];
    UINT n = GetSystemDirectoryA(sys, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    lstrcatA(sys, "\\d3d9.dll");
    g_realD3d9 = LoadLibraryA(sys);
    if (!g_realD3d9) {
        LogLine("failed to load system d3d9.dll");
        return;
    }
    g_realDirect3DCreate9 = reinterpret_cast<IDirect3D9* (WINAPI*)(UINT)>(GetProcAddress(g_realD3d9, "Direct3DCreate9"));
    if (g_realDirect3DCreate9) LogLine("loaded system d3d9 Direct3DCreate9");
    else LogLine("failed to resolve system Direct3DCreate9");
}

static void LoadRealXInput()
{
    if (g_realXInputGetState) return;
    const char* names[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    for (int i = 0; i < 3 && !g_realXInput; ++i) {
        g_realXInput = LoadLibraryA(names[i]);
        if (g_realXInput) {
            char buf[96];
            wsprintfA(buf, "loaded %s", names[i]);
            LogLine(buf);
        }
    }
    if (!g_realXInput) {
        LogLine("failed to load XInput");
        return;
    }
    g_realXInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(g_realXInput, "XInputGetState"));
    if (!g_realXInputGetState) LogLine("failed to resolve XInputGetState");
}

extern "C" IDirect3D9* WINAPI SrsDirect3DCreate9(UINT sdkVersion)
{
    LoadRealD3d9();
    EnsureOwnedWindow();
    StoreOwnedWindowGlobal();
    if (!g_realDirect3DCreate9) return nullptr;
    IDirect3D9* real = g_realDirect3DCreate9(sdkVersion);
    if (!real) {
        LogLine("Direct3DCreate9 returned null");
        return nullptr;
    }
    LogLine("Direct3DCreate9 wrapped");
    return new SrsDirect3D9(real);
}

static LRESULT CALLBACK OwnedWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_MOUSEACTIVATE:
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        return MA_ACTIVATE;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE) {
            StoreOwnedWindowGlobal();
        }
        break;
    case WM_DISPLAYCHANGE:
        if (InterlockedCompareExchange(&g_deviceCreated, 1, 1) == 0) {
            FitWindowToNearestMonitor(hwnd);
        }
        return 0;
    case WM_ERASEBKGND:
        {
            RECT client = {};
            GetClientRect(hwnd, &client);
            FillRect((HDC)wp, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
        }
        return 1;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI WindowThreadProc(void*)
{
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(g_thisModule ? g_thisModule : GetModuleHandleA(nullptr));
    LogWindowContext("window thread context");
    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OwnedWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "SRSOwnedRenderWindow";
    RegisterClassExA(&wc);
    LogLine("window thread started");

    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    if (w <= 0) w = 1024;
    if (h <= 0) h = 768;

    g_ownedWindow = CreateWindowExA(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        "Street Racing Syndicate",
        WS_POPUP | WS_VISIBLE,
        x, y, w, h,
        nullptr, nullptr, inst, nullptr);

    if (g_ownedWindow) {
        FitWindowToNearestMonitor(g_ownedWindow);
        ShowWindow(g_ownedWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_ownedWindow);
        SetEvent(g_windowReady);
        LogLine("owned HWND created");
        LogHwnd("owned HWND", g_ownedWindow);
    } else {
        SetEvent(g_windowReady);
        LogLine("failed to create owned HWND");
    }

    MSG msg;
    BOOL got = 0;
    while ((got = GetMessageA(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (got == 0) LogLine("window thread got WM_QUIT");
    else LogLine("window thread GetMessage failed");
    g_ownedWindow = nullptr;
    g_windowThread = nullptr;
    return 0;
}

static void CreateOwnedWindowInline()
{
    if (g_ownedWindow && IsWindow(g_ownedWindow)) return;

    MakeProcessDpiAware();
    HINSTANCE inst = reinterpret_cast<HINSTANCE>(g_thisModule ? g_thisModule : GetModuleHandleA(nullptr));
    LogWindowContext("inline window context");

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = OwnedWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = "SRSOwnedRenderWindow";
    RegisterClassExA(&wc);

    RECT r = GetNearestMonitorRect(nullptr);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 0) w = 1024;
    if (h <= 0) h = 768;

    g_ownedWindow = CreateWindowExA(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        "Street Racing Syndicate",
        WS_POPUP | WS_VISIBLE,
        r.left, r.top, w, h,
        nullptr, nullptr, inst, nullptr);

    if (g_ownedWindow) {
        FitWindowToNearestMonitor(g_ownedWindow);
        ShowWindow(g_ownedWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_ownedWindow);
        LogLine("owned HWND created inline");
        LogHwnd("owned HWND", g_ownedWindow);
    } else {
        LogLine("failed to create inline owned HWND");
    }
}

static void EnsureOwnedWindow()
{
    if (g_ownedWindow && IsWindow(g_ownedWindow)) return;
    LogLine("EnsureOwnedWindow creating inline window");
    CreateOwnedWindowInline();
}

static void StoreOwnedWindowGlobal()
{
    if (!g_ownedWindow || !IsWindow(g_ownedWindow)) {
        LogHwnd("StoreOwnedWindowGlobal skipped dead HWND", g_ownedWindow);
        return;
    }
    uintptr_t* hwndGlobal = reinterpret_cast<uintptr_t*>(0x0082CF4C);
    DWORD oldProtect = 0;
    if (!VirtualProtect(hwndGlobal, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for hwnd global 0x0082CF4C");
        return;
    }
    *hwndGlobal = reinterpret_cast<uintptr_t>(g_ownedWindow);
    DWORD ignored = 0;
    VirtualProtect(hwndGlobal, sizeof(uintptr_t), oldProtect, &ignored);
    DWORD now = GetTickCount();
    if (now - g_lastStoreLogTick > 3000) {
        g_lastStoreLogTick = now;
        LogHwnd("stored owned HWND into 0x0082CF4C", g_ownedWindow);
    }
}

static void SendKey(WORD vk, bool down)
{
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    if (!down) in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(in));
}

static void UpdateKey(WORD vk, bool wantDown, bool* isDown)
{
    if (wantDown == *isDown) return;
    SendKey(vk, wantDown);
    *isDown = wantDown;
}

static DWORD WINAPI ControllerThreadProc(void*)
{
    LogLine("controller bridge thread start");
    LoadRealXInput();
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool space = false;
    bool enter = false;
    bool escape = false;
    DWORD connectedSlot = XUSER_MAX_COUNT;
    DWORD lastStatusLogTick = 0;

    for (;;) {
        bool active = g_ownedWindow && IsWindow(g_ownedWindow) && GetForegroundWindow() == g_ownedWindow;
        XINPUT_STATE state = {};
        DWORD status[XUSER_MAX_COUNT] = {};
        bool connected = false;
        DWORD slot = XUSER_MAX_COUNT;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            status[i] = g_realXInputGetState ? g_realXInputGetState(i, &state) : ERROR_DEVICE_NOT_CONNECTED;
            if (status[i] == ERROR_SUCCESS) {
                connected = true;
                slot = i;
                break;
            }
        }
        if (connected && connectedSlot != slot) {
            connectedSlot = slot;
            char buf[96];
            wsprintfA(buf, "controller bridge detected XInput controller %lu", slot);
            LogLine(buf);
        }
        if (!connected) {
            connectedSlot = XUSER_MAX_COUNT;
            DWORD now = GetTickCount();
            if (now - lastStatusLogTick > 5000) {
                lastStatusLogTick = now;
                char buf[160];
                wsprintfA(buf, "controller bridge no XInput controller status=%lu/%lu/%lu/%lu",
                    status[0], status[1], status[2], status[3]);
                LogLine(buf);
            }
        }

        bool wantUp = false;
        bool wantDown = false;
        bool wantLeft = false;
        bool wantRight = false;
        bool wantSpace = false;
        bool wantEnter = false;
        bool wantEscape = false;

        if (active && connected) {
            WORD buttons = state.Gamepad.wButtons;
            wantUp = state.Gamepad.bRightTrigger > 32 || (buttons & XINPUT_GAMEPAD_DPAD_UP) != 0;
            wantDown = state.Gamepad.bLeftTrigger > 32 || (buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
            wantLeft = state.Gamepad.sThumbLX < -9000 || (buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
            wantRight = state.Gamepad.sThumbLX > 9000 || (buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
            wantSpace = (buttons & XINPUT_GAMEPAD_X) != 0 || (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
            wantEnter = (buttons & XINPUT_GAMEPAD_A) != 0 || (buttons & XINPUT_GAMEPAD_START) != 0;
            wantEscape = (buttons & XINPUT_GAMEPAD_B) != 0 || (buttons & XINPUT_GAMEPAD_BACK) != 0;
        }

        UpdateKey(VK_UP, wantUp, &up);
        UpdateKey(VK_DOWN, wantDown, &down);
        UpdateKey(VK_LEFT, wantLeft, &left);
        UpdateKey(VK_RIGHT, wantRight, &right);
        UpdateKey(VK_SPACE, wantSpace, &space);
        UpdateKey(VK_RETURN, wantEnter, &enter);
        UpdateKey(VK_ESCAPE, wantEscape, &escape);
        Sleep(16);
    }
    return 0;
}

static void EnsureControllerThread()
{
    if (InterlockedCompareExchange(&g_controllerThreadStarted, 1, 0) != 0) return;
    g_controllerThread = CreateThread(nullptr, 0, ControllerThreadProc, nullptr, 0, nullptr);
    if (g_controllerThread) LogLine("controller bridge thread created");
    else LogLine("failed to create controller bridge thread");
}

extern "C" __declspec(dllexport) HWND WINAPI SrsGetForegroundWindow()
{
    EnsureOwnedWindow();
    if (g_ownedWindow && IsWindow(g_ownedWindow)) {
        ShowWindow(g_ownedWindow, SW_SHOWNORMAL);
        StoreOwnedWindowGlobal();
        LogHwnd("returning owned HWND", g_ownedWindow);
        return g_ownedWindow;
    }
    if (g_realGetForegroundWindow) return g_realGetForegroundWindow();
    return nullptr;
}

static bool PatchMainExeIat()
{
    LogLine("PatchMainExeIat start");
    if (InterlockedCompareExchange(&g_iatPatched, 1, 1) == 1) return true;
    uintptr_t* slot = reinterpret_cast<uintptr_t*>(0x007115D0);
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for GetForegroundWindow IAT slot");
        return false;
    }
    g_realGetForegroundWindow = reinterpret_cast<HWND (WINAPI*)()>(*slot);
    *slot = reinterpret_cast<uintptr_t>(&SrsGetForegroundWindow);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(uintptr_t));
    InterlockedExchange(&g_iatPatched, 1);
    LogLine("patched GetForegroundWindow IAT direct slot 0x007115D0");
    return true;
}

static bool PatchD3dIat()
{
    LogLine("PatchD3dIat start");
    if (InterlockedCompareExchange(&g_d3dIatPatched, 1, 1) == 1) return true;
    LoadRealD3d9();
    uintptr_t* slot = reinterpret_cast<uintptr_t*>(0x007116C4);
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(uintptr_t), PAGE_READWRITE, &oldProtect)) {
        LogLine("VirtualProtect failed for Direct3DCreate9 IAT slot");
        return false;
    }
    *slot = reinterpret_cast<uintptr_t>(&SrsDirect3DCreate9);
    DWORD ignored = 0;
    VirtualProtect(slot, sizeof(uintptr_t), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(uintptr_t));
    InterlockedExchange(&g_d3dIatPatched, 1);
    LogLine("patched Direct3DCreate9 IAT direct slot 0x007116C4");
    return true;
}

static DWORD WINAPI InitThreadProc(void*)
{
    LogLine("init thread start");
    LogWindowContext("init thread context");
    LoadRealDinput8();
    PatchMainExeIat();
    PatchD3dIat();
    for (;;) {
        if (!g_ownedWindow || !IsWindow(g_ownedWindow)) {
            LogHwnd("supervisor sees missing/dead HWND", g_ownedWindow);
        } else {
            StoreOwnedWindowGlobal();
            if (g_originalGameWindow && g_originalGameWindow != g_ownedWindow && IsWindow(g_originalGameWindow) && IsWindowVisible(g_originalGameWindow)) {
                ShowWindow(g_originalGameWindow, SW_HIDE);
                SetWindowPos(g_originalGameWindow, HWND_BOTTOM, 0, 0, 1, 1, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
                LogHwnd("supervisor hid original game HWND", g_originalGameWindow);
            }
            DWORD now = GetTickCount();
            if (now - g_lastAliveLogTick > 3000) {
                g_lastAliveLogTick = now;
                LogHwnd("supervisor sees live HWND", g_ownedWindow);
            }
        }
        Sleep(500);
    }
    return 0;
}

HRESULT WINAPI DirectInput8Create(HINSTANCE hinst, DWORD version, REFIID riid, LPVOID* out, LPUNKNOWN outer)
{
    LoadRealDinput8();
    PatchMainExeIat();
    PatchD3dIat();
    if (!g_realDirectInput8Create) return E_FAIL;
    HRESULT hr = g_realDirectInput8Create(hinst, version, riid, out, outer);
    char buf[128];
    wsprintfA(buf, "DirectInput8Create result 0x%08X", (unsigned)hr);
    LogLine(buf);
    if (SUCCEEDED(hr) && out && *out) {
        PatchDirectInput8Vtable(*out);
    }
    return hr;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_thisModule = module;
        DisableThreadLibraryCalls(module);
        MakeProcessDpiAware();
        CreateThread(nullptr, 0, InitThreadProc, nullptr, 0, nullptr);
    }
    return TRUE;
}
