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

#ifndef __LEGACY_TYPEDEFS_H
#define __LEGACY_TYPEDEFS_H

#include "LegacyWindow.h"

// ================================================================================================

typedef HRESULT(WINAPI* FDirectDrawCreate)(GUID FAR*, LPDIRECTDRAW FAR*, IUnknown FAR*);

typedef ULONG(STDMETHODCALLTYPE* FUnknownAddRef)(LPUNKNOWN);
typedef ULONG(STDMETHODCALLTYPE* FUnknownRelease)(LPUNKNOWN);

typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSurfaceBlt)(LPDIRECTDRAWSURFACE, LPRECT,
                                                          LPDIRECTDRAWSURFACE, LPRECT, DWORD,
                                                          LPDDBLTFX);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSurfaceBltBatch)(LPDIRECTDRAWSURFACE, LPDDBLTBATCH,
                                                               DWORD, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSurfaceBltFast)(LPDIRECTDRAWSURFACE, DWORD, DWORD,
                                                              LPDIRECTDRAWSURFACE, LPRECT, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSurfaceGetDC)(LPDIRECTDRAWSURFACE, HDC FAR*);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSurfaceLock)(LPDIRECTDRAWSURFACE, LPRECT,
                                                           LPDDSURFACEDESC, DWORD, HANDLE);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSurfaceReleaseDC)(LPDIRECTDRAWSURFACE, HDC);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSurfaceUnlock)(LPDIRECTDRAWSURFACE, LPVOID);

typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawCreateSurface)(LPDIRECTDRAW, LPDDSURFACEDESC,
                                                             LPDIRECTDRAWSURFACE FAR*, IUnknown FAR*);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawFlipToGDISurface)(LPDIRECTDRAW);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawGetGDISurface)(LPDIRECTDRAW, LPDIRECTDRAWSURFACE FAR*);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSetCooperativeLevel)(LPDIRECTDRAW, HWND, DWORD);
typedef HRESULT(STDMETHODCALLTYPE* FDirectDrawSetDisplayMode)(LPDIRECTDRAW, DWORD, DWORD, DWORD);

typedef ATOM(WINAPI* FRegisterClassExA)(_In_ CONST WNDCLASSEXA*);
typedef HWND(WINAPI* FCreateWindowExA)(_In_ DWORD, _In_opt_ LPCSTR, _In_opt_ LPCSTR, _In_ DWORD,
                                       _In_ int, _In_ int, _In_ int, _In_ int, _In_opt_ HWND,
                                       _In_opt_ HMENU, _In_opt_ HINSTANCE, _In_opt_ LPVOID);
typedef BOOL(WINAPI* FGetWindowRect)(_In_ HWND, _Out_ LPRECT);
typedef BOOL(WINAPI* FGetClientRect)(_In_ HWND, _Out_ LPRECT);
typedef BOOL(WINAPI* FAdjustWindowRect)(_Inout_ LPRECT, _In_ DWORD, _In_ BOOL);
typedef VOID(WINAPI* FOutputDebugStringA)(_In_opt_ LPCSTR);
typedef BOOL(WINAPI* FSetProcessAffinityMask)(_In_ HANDLE, _In_ DWORD_PTR);
typedef BOOL(WINAPI* FGetCursorPos)(_Out_ LPPOINT);
typedef BOOL(WINAPI* FSetCursorPos)(_In_ int, _In_ int);
typedef BOOL(WINAPI* FClientToScreen)(_In_ HWND, _Inout_ LPPOINT);
typedef BOOL(WINAPI* FScreenToClient)(_In_ HWND, _Inout_ LPPOINT);
typedef BOOL(WINAPI* FSetMenu)(_In_ HWND, _In_opt_ HMENU);
typedef HMENU(WINAPI* FLoadMenu)(_In_opt_ HINSTANCE, _In_ LPCSTR);
typedef int(WINAPI* FGetSystemMetrics)(_In_ int);
typedef BOOL(WINAPI* FPeekMessage)(_Out_ LPMSG, _In_opt_ HWND, _In_ UINT, _In_ UINT, _In_ UINT);

#endif
