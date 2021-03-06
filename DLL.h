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

#ifndef __LEGACY_DLL_H
#define __LEGACY_DLL_H

void DDrawJoin();
void DDrawForceDirty();
void DDrawShowFPS(bool on);
void DDrawShowFrameTime(bool on);
void DDrawAcquireGdiObjects();
void DDrawReleaseGdiObjects();
void DDrawSignalInitComplete();
HWND DDrawBltToHWND(HWND wnd);
void DDrawSetBltOffset(int x, int y);
bool DDrawInitHooks();
void DDrawDeInitHooks();

HWND Win32GetClientHWND();
POINT Win32LockClientSize();
void Win32UnlockClientSize();
bool Win32SetThreadCount(int nthreads);
bool Win32InitHooks();
void Win32DeInitHooks();

#endif
