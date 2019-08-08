/* Copyright (C) 2019 Adam Johnson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "LegacyWindow.h"

#include <fstream>
#include <mutex>
#include <thread>

#include "DLL.h"
#include "LegacyTypedefs.h"
#include "MinHookpp.h"

// ================================================================================================

struct PrimarySurface
{
    LPDIRECTDRAWSURFACE m_proxySurface{ 0 };
    uint32_t m_flags{ 0 };
    std::mutex m_lock;
    HDC m_frameDC{ 0 };
    HBITMAP m_frameBitmap{ 0 };
    BITMAPINFO m_bitmapInfo{ 0 };
};

enum
{
    e_mainSurfaceDirty = (1<<0),
    e_wantQuit = (1<<1),
    e_ddrawSurfacesAcquired = (1<<2),
    e_gdiObjectsAcquired = (1<<3),
};

static void LegacyDrawThread();

// ================================================================================================

extern std::ofstream s_log;
static PrimarySurface s_primarySurface;
static std::thread s_drawThread;

static MHpp_Hook<FDirectDrawCreate>* s_ddrawCreateHook = nullptr;

static FDirectDrawSurfaceBlt s_ddrawSurfaceBlt = nullptr;
static FDirectDrawSurfaceBltBatch s_ddrawSurfaceBltBatch = nullptr;
static FDirectDrawSurfaceBltFast s_ddrawSurfaceBltFast = nullptr;
static FDirectDrawSurfaceLock s_ddrawSurfaceLock = nullptr;
static FDirectDrawSurfaceReleaseDC s_ddrawSurfaceReleaseDC = nullptr;
static FDirectDrawSurfaceUnlock s_ddrawSurfaceUnlock = nullptr;

static FDirectDrawCreateSurface s_ddrawCreateSurface = nullptr;
static FDirectDrawSetCooperativeLevel s_ddrawSetCooperativeLevel = nullptr;
static FDirectDrawSetDisplayMode s_ddrawSetDisplayMode = nullptr;

constexpr size_t PIXEL_COUNT = 640 * 480;

// ================================================================================================

template<typename Args>
static inline void SwapImplementation(LPVOID* vftable, size_t index, Args& original,
                                      LPVOID replacement)
{
    if (!original)
        original = (Args)vftable[index];
    vftable[index] = replacement;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyProxySurfaceBlt(LPDIRECTDRAWSURFACE self,
                                                       LPRECT lpDestRect,
                                                       LPDIRECTDRAWSURFACE lpDDSrcSurface,
                                                       LPRECT lpSrcRect,
                                                       DWORD dwFlags,
                                                       LPDDBLTFX lpDDBltFX)
{
    // appears to be unused
    HRESULT result = s_ddrawSurfaceBlt(self, lpDestRect, lpDDSrcSurface, lpSrcRect, dwFlags,
                                       lpDDBltFX);
    if (SUCCEEDED(result)) {
        std::lock_guard<std::mutex> _(s_primarySurface.m_lock);
        s_primarySurface.m_flags |= e_mainSurfaceDirty;
    }
    return result;
}


// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyProxySurfaceBltBatch(LPDIRECTDRAWSURFACE self,
                                                            LPDDBLTBATCH lpDDBltBatch,
                                                            DWORD dwCount,
                                                            DWORD dwFlags)
{
    // appears to be unused
    HRESULT result = s_ddrawSurfaceBltBatch(self, lpDDBltBatch, dwCount, dwFlags);
    if (SUCCEEDED(result)) {
        std::lock_guard<std::mutex> _(s_primarySurface.m_lock);
        s_primarySurface.m_flags |= e_mainSurfaceDirty;
    }
    return result;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyProxySurfaceBltFast(LPDIRECTDRAWSURFACE self,
                                                           DWORD dwX,
                                                           DWORD dwY,
                                                           LPDIRECTDRAWSURFACE lpDDSrcSurface,
                                                           LPRECT lpSrcRect,
                                                           DWORD dwTrans)
{
    // *IS* used... seems to blt graphics onto dialog boxes.
    HRESULT result = s_ddrawSurfaceBltFast(self, dwX, dwY, lpDDSrcSurface, lpSrcRect, dwTrans);
    if (SUCCEEDED(result)) {
        std::lock_guard<std::mutex> _(s_primarySurface.m_lock);
        s_primarySurface.m_flags |= e_mainSurfaceDirty;
    }
    return result;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyProxySurfaceLock(LPDIRECTDRAWSURFACE self,
                                                        LPRECT lpDestRect,
                                                        LPDDSURFACEDESC lpDDSurfaceDesc,
                                                        DWORD dwFlags,
                                                        HANDLE hEvent)
{
    HRESULT result = s_ddrawSurfaceLock(self, lpDestRect, lpDDSurfaceDesc, dwFlags, hEvent);
    if (FAILED(result)) {
        s_log << "IDirectDrawSurface::Lock: Proxy surface lock failed 0x" << std::hex << result << std::endl;
        return result;
    }
    return result;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyProxySurfaceReleaseDC(LPDIRECTDRAWSURFACE self, HDC hDC)
{
    HRESULT result = s_ddrawSurfaceReleaseDC(self, hDC);
    if (SUCCEEDED(result)) {
        std::lock_guard<std::mutex> _(s_primarySurface.m_lock);
        s_primarySurface.m_flags |= e_mainSurfaceDirty;
    }
    return DD_OK;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyProxySurfaceUnlock(LPDIRECTDRAWSURFACE self,
                                                          LPVOID lpSurfaceData)
{
    HRESULT result = s_ddrawSurfaceUnlock(self, lpSurfaceData);
    if (FAILED(result)) {
        s_log << "IDirectDrawSurface::Unlock: Proxy surface unlock failed 0x" << std::hex
              << result << std::endl;
        return result;
    }

    std::lock_guard<std::mutex> _(s_primarySurface.m_lock);
    s_primarySurface.m_flags |= e_mainSurfaceDirty;
    return DD_OK;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyDDrawCreateSurface(LPDIRECTDRAW self, LPDDSURFACEDESC lpDDSurfaceDesc,
                                                          LPDIRECTDRAWSURFACE FAR* lplpDDSurface,
                                                          IUnknown FAR* pUnkOuter)
{
    // Multiple surfaces are created, sadly...
    s_log << "IDirectDraw::CreateSurface: flags: 0x" << std::hex << lpDDSurfaceDesc->dwFlags
        << " width: " << std::dec << lpDDSurfaceDesc->dwWidth << " height: " << lpDDSurfaceDesc->dwHeight
        << " caps: 0x" << std::hex << lpDDSurfaceDesc->ddsCaps.dwCaps << std::endl;

    bool want_primary = (lpDDSurfaceDesc->dwFlags & DDSD_CAPS) &&
                        (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE);

    // Legacy.exe AFAICT never sends down the pixel format, but expects 16bpp surfaces. Unfortunately,
    // there is a 100% chance that DirectDraw is going to give us 32bpp surfaces unless otherwise
    // prompted, seeing as how we ate the 16bpp exclusive mode request. Ah well.
    if (!(lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT)) {
        s_log << "IDirectDraw::CreateSurface: hacking in pixel format for 16bpp" << std::endl;
        lpDDSurfaceDesc->dwFlags |= DDSD_PIXELFORMAT;

        lpDDSurfaceDesc->ddpfPixelFormat.dwSize = sizeof(lpDDSurfaceDesc->ddpfPixelFormat);
        lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_RGB;
        lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = 16;
        lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0x0000F800;
        lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x000007e0;
        lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x0000001f;
    }

    // Bad news, old bean... We're not running in 16bpp mode. Everything returned from here, JMP3
    // assumes is a 16bpp surface. The workaround above works great EXCEPT for when we request the
    // primary surface... Yes, JMP3 appears to blit directly to the screen... Anyway, if we pass
    // the primary surface request through the 16bpp hack below, it fails, causing WM_ACTIVATEAPP
    // to dereference a null pointer.
    if (want_primary) {
        s_log << "IDirectDraw::CreateSurface: overriding primary surface request" << std::endl;
        lpDDSurfaceDesc->ddsCaps.dwCaps &= ~DDSCAPS_PRIMARYSURFACE;
        lpDDSurfaceDesc->dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;
        lpDDSurfaceDesc->dwWidth = 640;
        lpDDSurfaceDesc->dwHeight = 480;
    }

    HRESULT result = s_ddrawCreateSurface(self, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);
    if (FAILED(result)) {
        s_log << "ERROR: 0x" << std::hex << result << std::endl;
        return result;
    }

    // Setup hooking for the primary surface (may Gawd have mercy on us)
    if (want_primary) {
        std::lock_guard<std::mutex> _(s_primarySurface.m_lock);
        if (s_primarySurface.m_flags & e_ddrawSurfacesAcquired) {
            s_log << "IDirectDraw::CreateSurface: trying to create the primary surface multiple times..."
                  << "Time to crash..." << std::endl;
            return DDERR_PRIMARYSURFACEALREADYEXISTS;
        }

        s_primarySurface.m_flags |= e_ddrawSurfacesAcquired;
        s_primarySurface.m_proxySurface = *lplpDDSurface;

        // Offloaded drawing to a thread due to how slow it is...
        s_drawThread = std::thread{ LegacyDrawThread };

        // IDirectDrawSurface VFTable
        LPVOID* vftable = (LPVOID*)((int*)*lplpDDSurface)[0];
        // 00: QueryInterface
        // 01: AddRef
        // 02: Release
        // 03: AddAttachedSurface
        // 04: AddOverlayDirtyRect
        SwapImplementation(vftable, 5, s_ddrawSurfaceBlt, LegacyProxySurfaceBlt);
        SwapImplementation(vftable, 6, s_ddrawSurfaceBltBatch, LegacyProxySurfaceBltBatch);
        SwapImplementation(vftable, 7, s_ddrawSurfaceBltFast, LegacyProxySurfaceBltFast);
        // 08: DeleteAttachedSurface
        // 09: EnumAttachedSurface
        // 10: EnumOverlayZOrders
        // 11: Flip: never used AFAICT
        // 12: GetAttachedSurface
        // 13: GetBltStatus
        // 14: GetCaps
        // 15: GetClipper
        // 16: GetColorKey
        // 17: GetDC
        // 18: GetFlipStatus
        // 19: GetOverlayPosition
        // 20: GetPalette
        // 21: GetPixelFormat
        // 22: GetSurfaceDesc
        // 23: Initialize
        // 24: IsLost
        SwapImplementation(vftable, 25, s_ddrawSurfaceLock, LegacyProxySurfaceLock);
        SwapImplementation(vftable, 26, s_ddrawSurfaceReleaseDC, LegacyProxySurfaceReleaseDC);
        // 27: Restore
        // 28: SetClipper
        // 29: SetColorKey
        // 30: SetOverlayPosition
        // 31: SetPalette
        SwapImplementation(vftable, 32, s_ddrawSurfaceUnlock, LegacyProxySurfaceUnlock);
        // 33: UpdateOverlay
        // 34: UpdateOverlayDisplay
        // 35: UpdateOverlayZOrder
    }

    return result;
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyDDrawSetCooperativeLevel(LPDIRECTDRAW self, HWND wnd, DWORD flags)
{
    s_log << "IDirectDraw::SetCooperativeLevel: requested flags 0x" << std::hex << flags << std::endl;

    // Generally, we expect the flags to be: DDSCL_FULLSCREEN | DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE
    // Define DDRAW_EXCLUSIVE to debug in 16bpp mode
#ifndef DDRAW_EXCLUSIVE
    flags &= ~DDSCL_FULLSCREEN;
    flags &= ~DDSCL_EXCLUSIVE;
    flags |= DDSCL_NORMAL;
#endif

    s_log << "IDirectDraw::SetCooperativeLevel: using flags 0x" << std::hex << flags << std::endl;
    return s_ddrawSetCooperativeLevel(self, wnd, flags);
}

// ================================================================================================

static HRESULT STDMETHODCALLTYPE LegacyDDrawSetDisplayMode(LPDIRECTDRAW self, DWORD width,
                                                           DWORD height, DWORD bpp)
{
    s_log << "IDirectDraw::SetDisplayMode: requested " << std::dec << width << "x" << height << std::endl;

#ifdef DDRAW_EXCLUSIVE
    s_log << "IDirectDraw::SetDisplayMode: DEBUG!!! using the above request" << std::endl;
    return s_ddrawSetDisplayMode(self, width, height, bpp);
#else
    s_log << "IDirectDraw::SetDisplayMode: dropping request like hot potato" << std::endl;
    return DD_OK;
#endif
}

// ================================================================================================

static HRESULT WINAPI LegacyDDrawCreate(GUID FAR* lpGUID, LPDIRECTDRAW FAR* lplpDD, IUnknown FAR* pUnkOuter)
{
    s_log << "DirectDrawCreate: handling futz request" << std::endl;
    HRESULT result = s_ddrawCreateHook->original()(lpGUID, lplpDD, pUnkOuter);
    if (FAILED(result)) {
        s_log << "ERROR: DirectDrawCreate failed... skipping..." << std::endl;
        return result;
    }

    // Rerouting robots^H^H^H^H^H^Hsome functions.
    LPDIRECTDRAW ddraw = *lplpDD;
    LPVOID* vftable = (LPVOID*)((int*)ddraw)[0];

    // IDirectDraw VFTable
    // 00: QueryInterface
    // 01: AddRef
    // 02: Release
    // 03: Compact
    // 04: CreateClipper
    // 05: CreatePalette
    SwapImplementation(vftable, 6, s_ddrawCreateSurface, &LegacyDDrawCreateSurface);
    // 07: Duplicate Surface
    // 08: EnumDisplayModes
    // 09: EnumSurfaces
    // 10: FlipToGDISurface: unused
    // 11: GetCaps
    // 12: GetDisplayMode
    // 13: GetFourCCCodes
    // 14: GetGDISurface: used, but seems to always return DDERR_NOTFOUND
    // 15: GetMonitorFrequency
    // 16: GetScanLine
    // 17: GetVerticalBlankStatus
    // 18: Initialize
    // 19: RestoreDisplayMode
    SwapImplementation(vftable, 20, s_ddrawSetCooperativeLevel, &LegacyDDrawSetCooperativeLevel);
    SwapImplementation(vftable, 21, s_ddrawSetDisplayMode, &LegacyDDrawSetDisplayMode);
    // 22: WaitForVerticalBlink

    return result;
}

// ================================================================================================

static void LegacyDrawThread()
{
    s_log << "LegacyDrawThread: in the saddle..." << std::endl;

    // So, here's the story... The proxy surface in s_primarySurface is 16bpp -- which is required
    // by Legacy.exe. In the main game, this surface represents the screen, so no flipping or
    // anything else is required. In our case, the screen is 32bpp. We can blit the 16bpp proxy
    // surface to the 32bpp primary surface just fine using IDirectDrawSurface::BltFast. Unfortunately,
    // in this case, BltFast is a misnomer. The conversion from 16bpp to 32bpp is incredibly slow.
    // Using any of the blitting methods locks both the main thread and the surfaces involed. So,
    // if we try to get clever and blit directly here, we've not helped anything because the main
    // thread just deadlocks waiting for our lock to release.
    //
    // So, here's the best solution I can find. We'll allow the main thread use our fake proxy
    // IDirectDrawSurface. Because there's no way to know when a "frame" is done (indeed, there
    // is no such thing as a frame), this thread will lock the surface and copy the data out
    // when it's detected as dirty. We'll unlock it and do the 16bpp->32bpp conversion here to
    // prevent the main thread from stalling. We'll then use GDI to blit the resulting 32-bit
    // bitmap onto the active window's DC.
    uint16_t* rgb555buf = new uint16_t[PIXEL_COUNT];
    uint32_t* rgba8888buf = new uint32_t[PIXEL_COUNT];
    DDSURFACEDESC desc = { 0 };
    desc.dwSize = sizeof(desc);

    do {
        s_primarySurface.m_lock.lock();
        bool dirty = (s_primarySurface.m_flags & e_mainSurfaceDirty);
        bool haveDdraw = (s_primarySurface.m_flags & e_ddrawSurfacesAcquired);
        bool haveGdi = (s_primarySurface.m_flags & e_gdiObjectsAcquired);
        bool quit = (s_primarySurface.m_flags & e_wantQuit);
        s_primarySurface.m_lock.unlock();

        if (quit)
            break;

        if (dirty && haveDdraw && haveGdi) {
            HRESULT result = s_ddrawSurfaceLock(s_primarySurface.m_proxySurface, nullptr,
                                                &desc, DDLOCK_WAIT, nullptr);
            if (FAILED(result)) {
                s_log << "LegacyDrawThread: ERROR failed to lock proxy surface 0x"
                    << std::hex << result << std::endl;
                Sleep(5);
                continue;
            }

            memcpy(rgb555buf, desc.lpSurface, PIXEL_COUNT * sizeof(uint16_t));
            s_ddrawSurfaceUnlock(s_primarySurface.m_proxySurface, desc.lpSurface);

            s_primarySurface.m_lock.lock();
            s_primarySurface.m_flags &= ~e_mainSurfaceDirty;
            s_primarySurface.m_lock.unlock();

            // Maybe at some point this should be vectorized?
            for (size_t i = 0; i < PIXEL_COUNT; ++i) {
                uint32_t pixel = rgb555buf[i];
                uint32_t r = ((((pixel) & 0x1F) * 527) + 23) >> 6;
                uint32_t g = ((((pixel >> 5) & 0x3F) * 259) + 33) >> 6;
                uint32_t b = ((((pixel >> 11) & 0x1F) * 527) + 23) >> 6;
                rgba8888buf[i] = ((r) | (g << 8) | (b << 16));
            }

            SetDIBits(s_primarySurface.m_frameDC, s_primarySurface.m_frameBitmap, 0, 480,
                      rgba8888buf, &s_primarySurface.m_bitmapInfo, DIB_RGB_COLORS);

            HWND wnd = Win32GetClientHWND();
            HDC wndDC = GetDC(wnd);
            // TODO: scaling?
            if (BitBlt(wndDC, 0, 0, 640, 480, s_primarySurface.m_frameDC, 0, 0, SRCCOPY) == FALSE) {
                s_log << "LegacyDrawThread: ERROR: BitBlt failed!" << std::endl;
            }
            ReleaseDC(wnd, wndDC);
        }

        // Don't saturate the CPU
        Sleep(10);
    } while(1);
}

// ================================================================================================

void DDrawForceDirty()
{
    s_primarySurface.m_lock.lock();
    s_primarySurface.m_flags |= e_mainSurfaceDirty;
    s_primarySurface.m_lock.unlock();
}

// ================================================================================================

void DDrawAcquireGdiObjects()
{
    s_primarySurface.m_frameDC = CreateCompatibleDC(nullptr);
    s_primarySurface.m_frameBitmap = CreateBitmap(640, 480, 1, 32, nullptr);
    SelectObject(s_primarySurface.m_frameDC, s_primarySurface.m_frameBitmap);

    s_primarySurface.m_bitmapInfo.bmiHeader.biSize = sizeof(s_primarySurface.m_bitmapInfo.bmiHeader);
    s_primarySurface.m_bitmapInfo.bmiHeader.biWidth = 640;
    s_primarySurface.m_bitmapInfo.bmiHeader.biHeight = -480; // indicates top-down
    s_primarySurface.m_bitmapInfo.bmiHeader.biPlanes = 1;
    s_primarySurface.m_bitmapInfo.bmiHeader.biCompression = BI_RGB;
    s_primarySurface.m_bitmapInfo.bmiHeader.biSizeImage = PIXEL_COUNT * sizeof(uint32_t);
    s_primarySurface.m_bitmapInfo.bmiHeader.biBitCount = 32;

    s_primarySurface.m_lock.lock();
    s_primarySurface.m_flags |= e_gdiObjectsAcquired;
    s_primarySurface.m_lock.unlock();
}

// ================================================================================================

void DDrawReleaseGdiObjects()
{
    std::lock_guard<std::mutex> _(s_primarySurface.m_lock);
    if (s_primarySurface.m_flags & e_gdiObjectsAcquired) {
        s_primarySurface.m_flags &= ~e_gdiObjectsAcquired;
        DeleteObject(s_primarySurface.m_frameBitmap);
        DeleteDC(s_primarySurface.m_frameDC);
    }
}

// ================================================================================================

void DDrawJoin()
{
    s_primarySurface.m_lock.lock();
    s_primarySurface.m_flags |= e_wantQuit;
    s_primarySurface.m_lock.unlock();
    s_drawThread.join();
}

// ================================================================================================

bool DDrawInitHooks()
{
    MAKE_HOOK(L"ddraw.dll", "DirectDrawCreate", LegacyDDrawCreate, s_ddrawCreateHook);
    return true;
}

// ================================================================================================

void DDrawDeInitHooks()
{
    delete s_ddrawCreateHook;
}
