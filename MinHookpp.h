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

#ifndef __MINHOOKPP_H
#define __MINHOOKPP_H

#include "LegacyWindow.h"

#include <MinHook.h>
#include <tuple>

// ================================================================================================

template<typename Args>
class MHpp_Hook
{
    Args m_original;
    Args m_replacement;
    Args m_target;

    MHpp_Hook(Args original, Args replacement, Args target);

public:
    ~MHpp_Hook();

    MHpp_Hook(const MHpp_Hook&) = delete;
    MHpp_Hook& operator=(const MHpp_Hook&) = delete;

    Args original() const { return m_original; }
    Args replacement() const { return m_replacement; }

public:
    static std::tuple<MH_STATUS, MHpp_Hook*> Create(LPCWSTR module, LPCSTR function, Args replacement);
};

// ================================================================================================

template<typename Args>
MHpp_Hook<Args>::MHpp_Hook(Args original, Args replacement, Args target)
    : m_original(original), m_replacement(replacement), m_target(target)
{

}

// ================================================================================================

template<typename Args>
MHpp_Hook<Args>::~MHpp_Hook()
{
    MH_DisableHook((LPVOID)m_target);
    MH_RemoveHook((LPVOID)m_target);
}

// ================================================================================================

template<typename Args>
std::tuple<MH_STATUS, MHpp_Hook<Args>*> MHpp_Hook<Args>::Create(LPCWSTR module, LPCSTR function,
                                                                Args replacement)
{
    LPVOID original;
    LPVOID target;
    MH_STATUS status = MH_CreateHookApiEx(module, function, (LPVOID)replacement, &original, &target);
    if (status != MH_OK)
        return std::make_tuple(status, nullptr);
    status = MH_EnableHook((LPVOID)target);
    return std::make_tuple(status, new MHpp_Hook<Args>((Args)original, replacement, (Args)target));
}

// ================================================================================================

template<typename Args>
inline bool MakeHook(LPCWSTR module, LPCSTR func, Args hook, MHpp_Hook<Args>*& out)
{
    // Crap... std::ofstream won't print out wchar strings.
    char module_temp[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, module, -1, module_temp, sizeof(module_temp), nullptr, nullptr);

    s_log << "Attempting to hook " << module_temp << "!" << func << std::endl;
    auto result = MHpp_Hook<Args>::Create(module, func, hook);
    if (std::get<0>(result) == MH_OK) {
        out = std::get<1>(result);
        return true;
    } else {
        s_log << "ERROR: " << MH_StatusToString(std::get<0>(result)) << std::endl;
        return false;
    }
}

// ================================================================================================

#define MAKE_HOOK(m, f, r, t) \
    if (!MakeHook(m, f, r, t)) { \
        ExitProcess(1); \
        return false; \
    }

#endif
