// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Image.h"
#include "MuR/ImageTypes.h"

#include "MuR/SerialisationPrivate.h"
#include "MuR/MutableMath.h"

#include "MuR/BlockCompression/Miro/Miro.h"

namespace mu
{

    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
	inline EImageFormat GetMostGenericFormat(EImageFormat FormatA, EImageFormat FormatB)
    {
		if (FormatA == FormatB)
		{
			return FormatA;
		}

		if (FormatA == EImageFormat::None)
		{
			return FormatA;
		}

		if (FormatB == EImageFormat::None)
		{
			return FormatB;
		}

        if (GetImageFormatData(FormatA).Channels > GetImageFormatData(FormatB).Channels)
		{	
			return FormatA;
		}

        if (GetImageFormatData(FormatB).Channels > GetImageFormatData(FormatA).Channels)
		{
			return FormatB;
		}

        if (FormatA == EImageFormat::BC2 || FormatA == EImageFormat::BC3 
			|| FormatA == EImageFormat::ASTC_4x4_RGBA_LDR || FormatA == EImageFormat::ASTC_6x6_RGBA_LDR || FormatA == EImageFormat::ASTC_8x8_RGBA_LDR || FormatA == EImageFormat::ASTC_10x10_RGBA_LDR)
		{	
			return FormatA;
		}

        if (FormatB == EImageFormat::BC2 || FormatB == EImageFormat::BC3
			|| FormatB == EImageFormat::ASTC_4x4_RGBA_LDR || FormatB == EImageFormat::ASTC_6x6_RGBA_LDR || FormatB == EImageFormat::ASTC_8x8_RGBA_LDR || FormatB == EImageFormat::ASTC_10x10_RGBA_LDR)

		{
			return FormatB;
		}

        return FormatA;
    }


    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
	inline EImageFormat GetRGBOrRGBAFormat(EImageFormat InFormat)
    {
		InFormat = GetUncompressedFormat(InFormat);

		if (InFormat == EImageFormat::None)
		{
			return InFormat;
		}

		switch (InFormat)
		{
		case EImageFormat::L_UByte: 
		{
			return EImageFormat::RGB_UByte;
		}
		case EImageFormat::RGB_UByte:
		case EImageFormat::RGBA_UByte:
		case EImageFormat::BGRA_UByte:
		{
			return InFormat;
		}
		default:
		{
			unimplemented();
		}
		}

		return EImageFormat::None;
    }

    //---------------------------------------------------------------------------------------------
    //!
    //---------------------------------------------------------------------------------------------
    inline bool IsCompressedFormat(EImageFormat Format)
    {
        return Format != GetUncompressedFormat(Format);
    }


	inline bool IsBlockCompressedFormat(EImageFormat Format)
	{
		return GetImageFormatData(Format).PixelsPerBlockX > 1 && Format != GetUncompressedFormat(Format);
	}

	namespace Private
	{
		inline void DecompressionFuncNotFoundFunc(miro::FImageSize, miro::FImageSize, miro::FImageSize, const uint8*, uint8*)
		{
			check(false);
		}
	}

	inline miro::SubImageDecompression::FuncRefType SelectDecompressionFunction(EImageFormat DestFormat, EImageFormat SrcFormat)
	{
		using DecompressionFuncRefType = miro::SubImageDecompression::FuncRefType;

		switch(SrcFormat)
		{
			case EImageFormat::BC1:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC1_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC1_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::BC2:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC2_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC2_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::BC3:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC3_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC3_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::BC4:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC4_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC4_To_RGBSubImage);
				}
				if (DestFormat == EImageFormat::L_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC4_To_LSubImage);
				}
				break;
			}

			case EImageFormat::BC5:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC5_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::BC5_To_RGBSubImage);
				}
				break;
			}
	
			case EImageFormat::ASTC_4x4_RGBA_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC4x4RGBAL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC4x4RGBAL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_4x4_RGB_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC4x4RGBL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC4x4RGBL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_4x4_RG_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC4x4RGL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC4x4RGL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_6x6_RGBA_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC6x6RGBAL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC6x6RGBAL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_6x6_RGB_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC6x6RGBL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC6x6RGBL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_6x6_RG_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC6x6RGL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC6x6RGL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_8x8_RGBA_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC8x8RGBAL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC8x8RGBAL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_8x8_RGB_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC8x8RGBL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC8x8RGBL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_8x8_RG_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC8x8RGL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC8x8RGL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_10x10_RGBA_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC10x10RGBAL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC10x10RGBAL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_10x10_RGB_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC10x10RGBL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC10x10RGBL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_10x10_RG_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC10x10RGL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC10x10RGL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_12x12_RGBA_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC12x12RGBAL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC12x12RGBAL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_12x12_RGB_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC12x12RGBL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC12x12RGBL_To_RGBSubImage);
				}
				break;
			}

			case EImageFormat::ASTC_12x12_RG_LDR:
			{
				if (DestFormat == EImageFormat::RGBA_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC12x12RGL_To_RGBASubImage);
				}
				else if (DestFormat == EImageFormat::RGB_UByte)
				{
					return DecompressionFuncRefType(miro::SubImageDecompression::ASTC12x12RGL_To_RGBSubImage);
				}
				break;
			}
		}

		checkf(false, TEXT("Decompression not supported."));
		return DecompressionFuncRefType(Private::DecompressionFuncNotFoundFunc);
	}

}

