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

#include "DLL.h"
#include "LegacyTypedefs.h"
#include "MinHookpp.h"

// ================================================================================================

extern std::ofstream s_log;

static MHpp_Hook<FDirectDrawCreate>* s_ddrawCreateHook = nullptr;

static FDirectDrawCreateSurface s_ddrawCreateSurface = nullptr;
static FDirectDrawSetCooperativeLevel s_ddrawSetCooperativeLevel = nullptr;
static FDirectDrawSetDisplayMode s_ddrawSetDisplayMode = nullptr;

// ================================================================================================

template<typename Args>
static inline void SwapImplementation(LPVOID* vftable, size_t index, Args& original,
                                      LPVOID replacement)
{
    original = (Args)vftable[index];
    vftable[index] = replacement;
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

  // Bad news, old bean... We're not running in 16bpp mode. Everything returned from here, JMP3
  // assumes is a 16bpp surface. The workaround below works great EXCEPT for when we request the
  // primary surface... Yes, JMP3 appears to blit directly to the screen... Anyway, if we pass
  // the primary surface request through the 16bpp hack below, it fails, causing WM_ACTIVATEAPP
  // to dereference a null pointer.
    if ((lpDDSurfaceDesc->dwFlags & DDSD_CAPS) && (lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE)) {
        // TODO!!!
        s_log << "IDirectDraw::CreateSurface: requested primary surface" << std::endl;
    } else if (!(lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT)) {
        s_log << "IDirectDraw::CreateSurface: hacking in pixel format for 16bpp" << std::endl;
        lpDDSurfaceDesc->dwFlags |= DDSD_PIXELFORMAT;

        lpDDSurfaceDesc->ddpfPixelFormat.dwSize = sizeof(lpDDSurfaceDesc->ddpfPixelFormat);
        lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_RGB;
        lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = 16;
        lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0x0000F800;
        lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x000007e0;
        lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x0000001f;
    }

    HRESULT result = s_ddrawCreateSurface(self, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter);
    if (FAILED(result))
        s_log << "ERROR: 0x" << std::hex << result << std::endl;
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
    // 14: GetGDISurface: used, but always returns DDERR_NOTFOUND
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

bool InitDDrawHooks()
{
    MAKE_HOOK(L"ddraw.dll", "DirectDrawCreate", LegacyDDrawCreate, s_ddrawCreateHook);
}

// ================================================================================================

void DeInitDDrawHooks()
{
    delete s_ddrawCreateHook;
}
