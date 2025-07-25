// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <carbon/Common.h>

#include <cstring>

CARBON_NAMESPACE_BEGIN(TITAN_NAMESPACE)

//! Pixel formats that are accepted by VulkanAllInOneTexture
enum class PixelFormat
{
    MONOCHROME,
    BGR,
    RGB,
    BGRA,
    RGBA
};

inline std::string PixelFormatName(PixelFormat pixelFormat)
{
    switch (pixelFormat)
    {
        case PixelFormat::MONOCHROME: return "MONOCHROME";
        case PixelFormat::BGR: return "BGR";
        case PixelFormat::RGB: return "RGB";
        case PixelFormat::BGRA: return "BGRA";
        case PixelFormat::RGBA:  return "RGBA";
        default: CARBON_CRITICAL("pixel format is not supported");
    }
}

inline int NumChannels(PixelFormat pixelFormat)
{
    switch (pixelFormat)
    {
        case PixelFormat::MONOCHROME: return 1;
        case PixelFormat::BGR:
        case PixelFormat::RGB: return 3;
        case PixelFormat::BGRA:
        case PixelFormat::RGBA: return 4;
        default: CARBON_CRITICAL("pixel format is not supported");
    }
}

/**
 * Converts one pixel format to another format, src and target need to have the appropriate sizes.
 */

template <class T> constexpr T SaturatedValue();
template <> constexpr float SaturatedValue<float>() { return 1.0f; }
template <> constexpr uint8_t SaturatedValue<uint8_t>() { return 255; }
template <> constexpr uint16_t SaturatedValue<uint16_t>() { return 65535; }

template <class T>
void Convert(const T* src, T* target, int width, int height, PixelFormat srcPixelFormat, PixelFormat targetPixelFormat)
{
    if (srcPixelFormat == targetPixelFormat)
    {
        memcpy(target, src, sizeof(T) * NumChannels(srcPixelFormat) * width * height);
        return;
    }

    if ((srcPixelFormat == PixelFormat::BGR) && (targetPixelFormat == PixelFormat::RGBA))
    {
        for (int i = 0; i < width * height; ++i)
        {
            target[4 * i + 0] = src[3 * i + 2]; // switch BGR to RGB
            target[4 * i + 1] = src[3 * i + 1];
            target[4 * i + 2] = src[3 * i + 0];
            target[4 * i + 3] = SaturatedValue<T>();
        }
    }
    else if ((srcPixelFormat == PixelFormat::BGR) && (targetPixelFormat == PixelFormat::BGRA))
    {
        for (int i = 0; i < width * height; ++i)
        {
            target[4 * i + 0] = src[3 * i + 0];
            target[4 * i + 1] = src[3 * i + 1];
            target[4 * i + 2] = src[3 * i + 2];
            target[4 * i + 3] = SaturatedValue<T>();
        }
    }
    else if ((srcPixelFormat == PixelFormat::BGRA) && (targetPixelFormat == PixelFormat::RGBA))
    {
        for (int i = 0; i < width * height; ++i)
        {
            target[4 * i + 0] = src[4 * i + 2]; // switch BGR to RGB
            target[4 * i + 1] = src[4 * i + 1];
            target[4 * i + 2] = src[4 * i + 0];
            target[4 * i + 3] = src[4 * i + 3];
        }
    }
    else if ((srcPixelFormat == PixelFormat::MONOCHROME) && (targetPixelFormat == PixelFormat::RGBA))
    {
        for (int i = 0; i < width * height; ++i)
        {
            target[4 * i + 0] = src[i];
            target[4 * i + 1] = src[i];
            target[4 * i + 2] = src[i];
            target[4 * i + 3] = SaturatedValue<T>();
        }
    }
    else
    {
        CARBON_CRITICAL("unsupported conversion from {} to {}", PixelFormatName(srcPixelFormat), PixelFormatName(targetPixelFormat));
    }
}

CARBON_NAMESPACE_END(TITAN_NAMESPACE)
