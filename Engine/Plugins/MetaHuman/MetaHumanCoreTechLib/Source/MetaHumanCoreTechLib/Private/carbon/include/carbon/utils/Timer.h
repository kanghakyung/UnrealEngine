// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>

#include <chrono>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

class Timer
{
public:
    Timer()
    {
        Restart();
    }

    void Restart() { m_start = std::chrono::high_resolution_clock::now(); }

    double Current()
    {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - m_start).count() / 1000000.0;
    }

private:
    std::chrono::high_resolution_clock::time_point m_start;
};

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
