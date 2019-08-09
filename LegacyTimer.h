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

#ifndef __LEGACY_TIMER_H
#define __LEGACY_TIMER_H

#include "LegacyWindow.h"

// ================================================================================================

class Timer
{
    LARGE_INTEGER m_frequency;
    LARGE_INTEGER m_startTime;
    LARGE_INTEGER m_accumulator;

public:
    Timer();

    void start();
    float end();
    float total() const;
};

// ================================================================================================

Timer::Timer()
    : m_frequency(), m_startTime(), m_accumulator()
{
    QueryPerformanceFrequency(&m_frequency);
}

// ================================================================================================

void Timer::start()
{
    QueryPerformanceCounter(&m_startTime);
}

// ================================================================================================

float Timer::end()
{
    LARGE_INTEGER endTime;
    QueryPerformanceCounter(&endTime);
    LONGLONG elapsed = endTime.QuadPart - m_startTime.QuadPart;
    m_accumulator.QuadPart += elapsed;
    m_startTime.QuadPart = 0;
    return (double)elapsed / m_frequency.QuadPart;
}

// ================================================================================================

float Timer::total() const
{
    return (double)m_accumulator.QuadPart / m_frequency.QuadPart;
}

#endif
