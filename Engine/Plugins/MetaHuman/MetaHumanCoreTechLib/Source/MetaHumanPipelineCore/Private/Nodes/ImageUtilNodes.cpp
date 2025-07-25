// Copyright Epic Games, Inc. All Rights Reserved.

#include "Nodes/ImageUtilNodes.h"
#include "CoreUtils.h"
#include "ImageSequenceUtils.h"
#include "TrackingPathUtils.h"
#include "IFramePathResolver.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Async/ParallelFor.h"


namespace UE::MetaHuman::Pipeline
{

FUEImageLoadNode::FUEImageLoadNode(const FString& InName) : FNode("UEImageLoad", InName)
{
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

FUEImageLoadNode::~FUEImageLoadNode() = default;

bool FUEImageLoadNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	check(FramePathResolver);

	bool bIsOK = false;
	const int32 FrameNumber = InPipelineData->GetFrameNumber();
	const FString ImagePath = FramePathResolver->ResolvePath(FrameNumber);

	if (FPaths::FileExists(ImagePath))
	{
		TArray<uint8> RawFileData;
		if (FFileHelper::LoadFileToArray(RawFileData, *ImagePath))
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

			for (EImageFormat ImageFormat : { EImageFormat::PNG, EImageFormat::JPEG })
			{
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

				if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
				{
					FUEImageDataType Output;
					if (ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, Output.Data))
					{
						Output.Width = ImageWrapper->GetWidth();
						Output.Height = ImageWrapper->GetHeight();

						InPipelineData->SetData<FUEImageDataType>(Pins[0], MoveTemp(Output));

						bIsOK = true;

						break;
					}
				}
			}
		}

		if (!bIsOK)
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::FailedToLoadFile);
			InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Failed to load file %s"), *ImagePath));
		}
	}
	else if (bFailOnMissingFile)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToFindFile);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Failed to find file %s"), *ImagePath));
	}

	return bIsOK;
}



FUEImageSaveNode::FUEImageSaveNode(const FString& InName) : FNode("UEImageSave", InName)
{
	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
}

bool FUEImageSaveNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	bool bIsOK = false;

	int32 FrameNumber = InPipelineData->GetFrameNumber();

	FString Filename = FTrackingPathUtils::ExpandFilePathFormat(FilePath, FrameNumber + FrameNumberOffset);
	if (Filename == FilePath)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::MissingFrameFormatSpecifier);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Missing frame number format specifier %s"), *FilePath));
		return false;
	}

	const FUEImageDataType &Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Input.Data.GetData(), Input.Data.Num(), Input.Width, Input.Height, ERGBFormat::BGRA, 8))
	{
		TArray64<uint8> RawData = ImageWrapper->GetCompressed();

		if (FFileHelper::SaveArrayToFile(RawData, *Filename))
		{
			bIsOK = true;
		}
		else
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::FailedToSaveFile);
			InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Failed to save file %s"), *Filename));
		}
	}
	else
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToCompressData);
		InPipelineData->SetErrorNodeMessage("Failed to compress data");
	}

	return bIsOK;
}



FUEImageResizeNode::FUEImageResizeNode(const FString& InName) : FNode("UEImageResize", InName)
{
	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
	Pins.Add(FPin("Scaling Out", EPinDirection::Output, EPinType::Scaling));
}

bool FUEImageResizeNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);

	float WidthScale = float(Input.Width) / MaxSize;
	float HeightScale = float(Input.Height) / MaxSize;
	float Scale = FMath::Max(WidthScale, HeightScale);

	int32 TempWidth = Input.Width / Scale;
	int32 TempHeight = Input.Height / Scale;

	TArray<FColor> TempOutput;
	TempOutput.SetNum(TempWidth * TempHeight);

	TArray<FColor>* InputData = static_cast<TArray<FColor>*>((void*)&Input.Data);

	FImageUtils::ImageResize(Input.Width, Input.Height, *InputData, TempWidth, TempHeight, TempOutput, true);

	FUEImageDataType OutputImage;
	OutputImage.Width = MaxSize;
	OutputImage.Height = MaxSize;
	OutputImage.Data.SetNum(OutputImage.Width * OutputImage.Height * 4);

	const FColor* TempData = TempOutput.GetData();

	for (int32 Y = 0; Y < TempHeight; ++Y)
	{
		FColor* OutputColor = static_cast<FColor*>((void*)&OutputImage.Data[(Y * OutputImage.Width) * 4]);

		for (int32 X = 0; X < TempWidth; ++X)
		{
			*OutputColor++ = *TempData++;
		}
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[1], MoveTemp(OutputImage));

	FScalingDataType OutputScaling;
	OutputScaling.Factor = Scale;
	InPipelineData->SetData<FScalingDataType>(Pins[2], MoveTemp(OutputScaling));

	return true;
}



FUEImageCropNode::FUEImageCropNode(const FString& InName) : FNode("UEImageCrop", InName)
{
	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

bool FUEImageCropNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);

	if (X < 0 || Y < 0 || Width <= 0 || Height <= 0 || X + Width > Input.Width || Y + Height > Input.Height)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::BadValues);
		InPipelineData->SetErrorNodeMessage(TEXT("Invalid cropping parameters"));
		return false;
	}

	FUEImageDataType Output;
	Output.Width = Width;
	Output.Height = Height;
	Output.Data.SetNumUninitialized(Output.Width * Output.Height * 4);

	const uint8* InputData = Input.Data.GetData();
	InputData += (Y * Input.Width + X) * 4;
	uint8* OutputData = Output.Data.GetData();

	const int32 InputLineSize = Input.Width * 4;
	const int32 OutputLineSize = Output.Width * 4;

	for (int32 Index = 0; Index < Height; ++Index, InputData += InputLineSize, OutputData += OutputLineSize)
	{
		FMemory::Memcpy(OutputData, InputData, OutputLineSize);
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[1], MoveTemp(Output));

	return true;
}



FUEImageRotateNode::FUEImageRotateNode(const FString& InName) : FNode("UEImageRotate", InName)
{
	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

bool FUEImageRotateNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);
	FUEImageDataType Output;

	float AngleCopy = GetAngle();

	auto CopyInputRow = [](const int32 InWidth, const uint32* InInputData, uint32* OutOutputData, const int32 InOutputStep)
	{
		for (int32 X = 0; X < InWidth; ++X, ++InInputData, OutOutputData += InOutputStep)
		{
			*OutOutputData = *InInputData;
		}
	};

	const uint32* InputData = (const uint32*) Input.Data.GetData(); // treat as 32-bit RGBA rather than 4 x 8-bit components

	if (FMath::IsNearlyEqual(AngleCopy, 0))
	{
		Output = Input;
	}
	else if (FMath::IsNearlyEqual(AngleCopy, 90))
	{
		Output.Width = Input.Height;
		Output.Height = Input.Width;
		Output.Data.SetNumUninitialized(Output.Width * Output.Height * 4);
		uint32* OutputData = (uint32*) Output.Data.GetData();

		ParallelFor(Input.Height, [&](int32 InY)
		{
			CopyInputRow(Input.Width, InputData + (InY * Input.Width), OutputData + ((Output.Height - 1) * Output.Width + InY), -Output.Width);
		});
	}
	else if (FMath::IsNearlyEqual(AngleCopy, 180))
	{
		Output.Width = Input.Width;
		Output.Height = Input.Height;
		Output.Data.SetNumUninitialized(Output.Width * Output.Height * 4);
		uint32* OutputData = (uint32*) Output.Data.GetData();

		ParallelFor(Input.Height, [&](int32 InY)
		{
			CopyInputRow(Input.Width, InputData + (InY * Input.Width), OutputData + ((Output.Height - 1 - InY) * Output.Width) + (Output.Width - 1), -1);
		});
	}
	else if (FMath::IsNearlyEqual(AngleCopy, 270))
	{
		Output.Width = Input.Height;
		Output.Height = Input.Width;
		Output.Data.SetNumUninitialized(Output.Width * Output.Height * 4);
		uint32* OutputData = (uint32*) Output.Data.GetData();

		ParallelFor(Input.Height, [&](int32 InY)
		{
			CopyInputRow(Input.Width, InputData + (InY * Input.Width), OutputData + (Output.Width - 1 - InY), Output.Width);
		});
	}
	else
	{
		// For now just need to support rotations in steps of 90 degree.
		// May support arbitrary rotation angles in future. 

		InPipelineData->SetErrorNodeCode(ErrorCode::UnsupportedAngle);
		InPipelineData->SetErrorNodeMessage("Unsupported angle");
		return false;
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[1], MoveTemp(Output));

	return true;
}

void FUEImageRotateNode::SetAngle(float InAngle)
{
	FScopeLock Lock(&AngleMutex);

	Angle = InAngle;
}

float FUEImageRotateNode::GetAngle()
{
	FScopeLock Lock(&AngleMutex);

	return Angle;
}



FUEImageCompositeNode::FUEImageCompositeNode(const FString& InName) : FNode("UEImageComposite", InName)
{
	Pins.Add(FPin("UE Image In 1", EPinDirection::Input, EPinType::UE_Image, 0));
	Pins.Add(FPin("UE Image In 2", EPinDirection::Input, EPinType::UE_Image, 1));
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

bool FUEImageCompositeNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Image1 = InPipelineData->GetData<FUEImageDataType>(Pins[0]);
	const FUEImageDataType& Image2 = InPipelineData->GetData<FUEImageDataType>(Pins[1]);

	FUEImageDataType Output;
	Output.Width = Image1.Width + Image2.Width;
	Output.Height = FMath::Max(Image1.Height, Image2.Height);
	Output.Data.Init(255, Output.Width * Output.Height * 4);

	const uint8* Image1Data = Image1.Data.GetData();
	const uint8* Image2Data = Image2.Data.GetData();
	uint8* OutputData = Output.Data.GetData();

	const int32 Image1LineWidth = Image1.Width * 4;
	const int32 Image2LineWidth = Image2.Width * 4;

	for (int32 Y = 0; Y < Output.Height; ++Y)
	{
		if (Y < Image1.Height)
		{
			FMemory::Memcpy(OutputData, Image1Data, Image1LineWidth);
			Image1Data += Image1LineWidth;
		}

		OutputData += Image1LineWidth;

		if (Y < Image2.Height)
		{
			FMemory::Memcpy(OutputData, Image2Data, Image2LineWidth);
			Image2Data += Image2LineWidth;
		}

		OutputData += Image2LineWidth;
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[2], MoveTemp(Output));

	return true;
}



FUEImageToUEGrayImageNode::FUEImageToUEGrayImageNode(const FString& InName) : FNode("UEImageToUEGrayImage", InName)
{
	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
	Pins.Add(FPin("UE Gray Image Out", EPinDirection::Output, EPinType::UE_GrayImage));
}

bool FUEImageToUEGrayImageNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);

	FUEGrayImageDataType Output;
	Output.Width = Input.Width;
	Output.Height = Input.Height;
	Output.Data.SetNumUninitialized(Output.Width * Output.Height);

	const uint8* InputData = Input.Data.GetData();
	uint8* OutputData = Output.Data.GetData();

	int32 Size = Input.Height * Input.Width;

	for (int32 Index = 0; Index < Size; ++Index, InputData += 4, ++OutputData)
	{
		uint8 Blue = InputData[0];
		uint8 Green = InputData[1];
		uint8 Red = InputData[2];
		float Gray = (Red / 255.0f * 0.299f) + (Green / 255.0f * 0.587f) + (Blue / 255.0f * 0.114f);
		*OutputData = Gray * 255;
	}

	InPipelineData->SetData<FUEGrayImageDataType>(Pins[1], MoveTemp(Output));

	return true;
}



FUEGrayImageToUEImageNode::FUEGrayImageToUEImageNode(const FString& InName) : FNode("UEGrayImageToUEImage", InName)
{
	Pins.Add(FPin("UE Gray Image In", EPinDirection::Input, EPinType::UE_GrayImage));
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

bool FUEGrayImageToUEImageNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEGrayImageDataType& Input = InPipelineData->GetData<FUEGrayImageDataType>(Pins[0]);

	FUEImageDataType Output;
	Output.Width = Input.Width;
	Output.Height = Input.Height;
	Output.Data.SetNumUninitialized(Output.Width * Output.Height * 4);

	const uint8* InputData = Input.Data.GetData();
	uint8* OutputData = Output.Data.GetData();

	int32 Size = Input.Height * Input.Width;

	for (int32 Index = 0; Index < Size; ++Index, ++InputData, OutputData += 4)
	{
		OutputData[0] = *InputData;
		OutputData[1] = *InputData;
		OutputData[2] = *InputData;
		OutputData[3] = 255;
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[1], MoveTemp(Output));

	return true;
}



FUEImageToHSImageNode::FUEImageToHSImageNode(const FString& InName) : FNode("UEImageToHSImage", InName)
{
	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
	Pins.Add(FPin("HS Image Out", EPinDirection::Output, EPinType::HS_Image));
}

bool FUEImageToHSImageNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Input = InPipelineData->GetData<FUEImageDataType>(Pins[0]);

	FHSImageDataType Output;
	Output.Width = Input.Width;
	Output.Height = Input.Height;
	Output.Data.SetNumUninitialized(Output.Width * Output.Height * 3);

	const int32 FullSize = Input.Height * Input.Width;
	const int32 TwiceFullSize = 2 * FullSize;
	int32 Index = 0;
	const FColor* PixelColor = static_cast<const FColor*>((void*)Input.Data.GetData());
	const float Sqrt2 = FMath::Sqrt(2.f);

	for (int32 Y = 0; Y < Input.Height; ++Y)
	{
		for (int32 X = 0; X < Input.Width; ++X, ++Index, ++PixelColor)
		{
			// Normalizing pixels with nchw format (RRRRGGGGBBBB)
			Output.Data[Index] = (((PixelColor->R / 255.f) - 0.5) * Sqrt2);
			Output.Data[FullSize + Index] = (((PixelColor->G / 255.f) - 0.5) * Sqrt2);
			Output.Data[TwiceFullSize + Index] = (((PixelColor->B / 255.f) - 0.5) * Sqrt2);
		}
	}

	InPipelineData->SetData<FHSImageDataType>(Pins[1], MoveTemp(Output));

	return true;
}



FBurnContoursNode::FBurnContoursNode(const FString& InName) : FNode("BurnContours", InName)
{
	Pins.Add(FPin("UE Image In", EPinDirection::Input, EPinType::UE_Image));
	Pins.Add(FPin("Contours In", EPinDirection::Input, EPinType::Contours));
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

bool FBurnContoursNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FUEImageDataType& Image = InPipelineData->GetData<FUEImageDataType>(Pins[0]);
	const FFrameTrackingContourData& Contours = InPipelineData->GetData<FFrameTrackingContourData>(Pins[1]);

	FUEImageDataType Output = Image;

	for (const auto& Contour : Contours.TrackingContours)
	{
		FString Hash = FMD5::HashAnsiString(*Contour.Key);

		int32 Red = FCString::Atoi(*Hash.Mid(0, 2));
		int32 Green = FCString::Atoi(*Hash.Mid(2, 2));
		int32 Blue = FCString::Atoi(*Hash.Mid(4, 2));

		Red = FMath::Min(Red + 140, 255);
		Green = FMath::Min(Green + 140, 255);
		Blue = FMath::Min(Blue + 140, 255);

		epic::core::BurnPointsIntoImage(Contour.Value.DensePoints, Output.Width, Output.Height, Output.Data, Red, Green, Blue, Size);
		if (LineWidth > 0 && Contour.Value.DensePoints.Num() > 1)
		{
			for (int32 Pt = 0; Pt < Contour.Value.DensePoints.Num() - 1; ++Pt)
			{
				epic::core::BurnLineIntoImage(Contour.Value.DensePoints[Pt], Contour.Value.DensePoints[Pt + 1], Output.Width, Output.Height, Output.Data, Red, Green, Blue, LineWidth);
			}
		}
	}

	InPipelineData->SetData<FUEImageDataType>(Pins[2], MoveTemp(Output));

	return true;
}

FDepthLoadNode::FDepthLoadNode(const FString& InName) : FNode("DepthLoad", InName)
{
	Pins.Add(FPin("Depth Out", EPinDirection::Output, EPinType::Depth));
}

FDepthLoadNode::~FDepthLoadNode() = default;

bool FDepthLoadNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	check(FramePathResolver);

	bool bIsOK = false;
	const int32 FrameNumber = InPipelineData->GetFrameNumber();
	const FString ImagePath = FramePathResolver->ResolvePath(FrameNumber);

	if (FPaths::FileExists(ImagePath))
	{
		TArray<uint8> RawFileData;
		if (FFileHelper::LoadFileToArray(RawFileData, *ImagePath))
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

			for (EImageFormat ImageFormat : { EImageFormat::EXR })
			{
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

				if (ImageWrapper.IsValid() && ImageWrapper->SetCompressed(RawFileData.GetData(), RawFileData.Num()))
				{
					if (ImageWrapper->GetBitDepth() == 32 && ImageWrapper->GetFormat() == ERGBFormat::GrayF)
					{
						FDepthDataType Output;

						Output.Width = ImageWrapper->GetWidth();
						Output.Height = ImageWrapper->GetHeight();
						Output.Data.SetNumUninitialized(Output.Width * Output.Height);

						TArray<uint8> IntData;
						if (ImageWrapper->GetRaw(ERGBFormat::GrayF, 32, IntData))
						{
							const float* FloatData = static_cast<const float*>((void*)IntData.GetData());
							FMemory::Memcpy(Output.Data.GetData(), FloatData, Output.Width * Output.Height * sizeof(float));

							InPipelineData->SetData<FDepthDataType>(Pins[0], MoveTemp(Output));

							bIsOK = true;

							break;
						}
					}
				}
			}
		}

		if (!bIsOK)
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::FailedToLoadFile);
			InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Failed to load file %s"), *ImagePath));
		}
	} 
	else if (bFailOnMissingFile)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToFindFile);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Failed to find file %s"), *ImagePath));
	}

	return bIsOK;
}



FDepthSaveNode::FDepthSaveNode(const FString& InName) : FNode("DepthSave", InName)
{
	Pins.Add(FPin("Depth In", EPinDirection::Input, EPinType::Depth));
}

bool FDepthSaveNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	bool bIsOK = false;

	int32 FrameNumber = InPipelineData->GetFrameNumber();

	FString Filename = FTrackingPathUtils::ExpandFilePathFormat(FilePath, FrameNumber + FrameNumberOffset);
	if (Filename == FilePath)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::MissingFrameFormatSpecifier);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Missing frame number format specifier %s"), *FilePath));
		return false;
	}

	const FDepthDataType& Input = InPipelineData->GetData<FDepthDataType>(Pins[0]);

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::EXR);

	if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Input.Data.GetData(), Input.Data.Num() * 4, Input.Width, Input.Height, ERGBFormat::GrayF, 32))
	{
		EImageCompressionQuality Compression = bShouldCompressFiles ? EImageCompressionQuality::Default : EImageCompressionQuality::Uncompressed;

		TArray64<uint8> RawData = ImageWrapper->GetCompressed((int32) Compression);

		if (FFileHelper::SaveArrayToFile(RawData, *Filename))
		{
			bIsOK = true;
		}
		else
		{
			InPipelineData->SetErrorNodeCode(ErrorCode::FailedToSaveFile);
			InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Failed to save file %s"), *Filename));
		}
	}
	else
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToCompressData);
		InPipelineData->SetErrorNodeMessage("Failed to compress data");
	}

	return bIsOK;
}



FDepthQuantizeNode::FDepthQuantizeNode(const FString& InName) : FNode("DepthQuantize", InName)
{
	Pins.Add(FPin("Depth In", EPinDirection::Input, EPinType::Depth));
	Pins.Add(FPin("Depth Out", EPinDirection::Output, EPinType::Depth));
}

bool FDepthQuantizeNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FDepthDataType& Input = InPipelineData->GetData<FDepthDataType>(Pins[0]);

	FDepthDataType Output;
	Output.Width = Input.Width;
	Output.Height = Input.Height;;

	const int32 Size = Output.Width * Output.Height;

	Output.Data.SetNumUninitialized(Size);

	const float* InputData = Input.Data.GetData();
	float* OutputData = Output.Data.GetData();

	for (int32 Index = 0; Index < Size; ++Index, ++InputData, ++OutputData)
	{
		*OutputData = int32(*InputData * Factor) / Factor;
	}

	InPipelineData->SetData<FDepthDataType>(Pins[1], MoveTemp(Output));

	return true;
}



FDepthResizeNode::FDepthResizeNode(const FString& InName) : FNode("DepthResize", InName)
{
	Pins.Add(FPin("Depth In", EPinDirection::Input, EPinType::Depth));
	Pins.Add(FPin("Depth Out", EPinDirection::Output, EPinType::Depth));
}

bool FDepthResizeNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	const FDepthDataType& Input = InPipelineData->GetData<FDepthDataType>(Pins[0]);

	FDepthDataType Output;
	Output.Width = Input.Width / Factor;
	Output.Height = Input.Height / Factor;
	Output.Data.SetNumUninitialized(Output.Width * Output.Height);

	float* OutputData = Output.Data.GetData();

	for (int32 Y = 0; Y < Output.Height; ++Y)
	{
		for (int32 X = 0; X < Output.Width; ++X, ++OutputData)
		{
			float SampleValue = 0;
			int32 SampleCount = 0;

			const float* InputRowData = Input.Data.GetData() + ((Y * Factor) * Input.Width) + (X * Factor);

			for (int32 YSample = 0; YSample < Factor; ++YSample, InputRowData += Input.Width)
			{
				const float* InputData = InputRowData;

				for (int32 XSample = 0; XSample < Factor; ++XSample, ++InputData)
				{
					if (*InputData > 0)
					{
						SampleValue += *InputData;
						SampleCount++;
					}
				}
			}

			*OutputData = SampleCount == 0 ? 0 : SampleValue / SampleCount;
		}
	}

	InPipelineData->SetData<FDepthDataType>(Pins[1], MoveTemp(Output));

	return true;
}



FDepthToUEImageNode::FDepthToUEImageNode(const FString& InName) : FNode("DepthToUEImage", InName)
{
	Pins.Add(FPin("Depth In", EPinDirection::Input, EPinType::Depth));
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

bool FDepthToUEImageNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	bool bIsOK = false;

	const FDepthDataType& Input = InPipelineData->GetData<FDepthDataType>(Pins[0]);

	float Scale = Max - Min;

	if (Scale > 0.01f)
	{
		FUEImageDataType Output;
		Output.Width = Input.Width;
		Output.Height = Input.Height;
		Output.Data.Reserve(Output.Width * Output.Height * 4);

		const float* DepthData = Input.Data.GetData();

		for (int32 Y = 0; Y < Input.Height; ++Y)
		{
			for (int32 X = 0; X < Input.Width; ++X)
			{
				float Depth = *DepthData++;
				uint8 IntDepth = 0;

				if (Depth >= Min && Depth <= Max)
				{
					IntDepth = (Depth - Min) / Scale * 255;
				}

				Output.Data.Add(IntDepth);
				Output.Data.Add(IntDepth);
				Output.Data.Add(IntDepth);
				Output.Data.Add(255);
			}
		}

		InPipelineData->SetData<FUEImageDataType>(Pins[1], MoveTemp(Output));

		bIsOK = true;
	}
	else
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::BadRange);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Bad range %f %f"), Min, Max));
	}

	return bIsOK;
}

FFColorToUEImageNode::FFColorToUEImageNode(const FString& InName) : FNode("RenderTargetNode", InName)
{
	Pins.Add(FPin("UE Image Out", EPinDirection::Output, EPinType::UE_Image));
}

bool FFColorToUEImageNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	bool bSuccess = false;

	if (!Samples.IsEmpty())
	{
		FUEImageDataType Output;
		Output.Width = Width;
		Output.Height = Height;
		
		Output.Data.SetNumUninitialized(4 * Width * Height);
		uint8* OutputData = Output.Data.GetData();
		FMemory::Memcpy(OutputData, Samples.GetData(), 4 * Width * Height);
		
		InPipelineData->SetData<FUEImageDataType>(Pins[0], MoveTemp(Output));

		bSuccess = true;
	}
	else
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::NoInputImage);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Image for processing has not been set")));
	}

	return bSuccess;
}

FCopyImagesNode::FCopyImagesNode(const FString& InName) : FNode("CopyImages", InName)
{
}

bool FCopyImagesNode::Process(const TSharedPtr<FPipelineData>& InPipelineData)
{
	bool bSuccess = false;

	int32 FrameNumber = InPipelineData->GetFrameNumber();

	FString Filename = FTrackingPathUtils::ExpandFilePathFormat(InputFilePath, FrameNumber + FrameNumberOffset);
	if (Filename == InputFilePath)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::MissingFrameFormatSpecifier);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("Missing frame number format specifier %s"), *InputFilePath));
		return false;
	}

	if (!FPaths::FileExists(Filename))
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToFindFile);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("File couldn't be found %s"), *Filename));
		return false;
	}
	
	const FString OutputFilePath = OutputDirectoryPath / FPaths::GetCleanFilename(Filename);
	uint32 Result = IFileManager::Get().Copy(*OutputFilePath, *Filename, true, true);

	if (Result != COPY_OK)
	{
		InPipelineData->SetErrorNodeCode(ErrorCode::FailedToCopyFile);
		InPipelineData->SetErrorNodeMessage(FString::Printf(TEXT("File couldn't be copied %s"), *Filename));
		return false;
	}

	return true;
}

}
