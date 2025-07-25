// Copyright Epic Games, Inc. All Rights Reserved.

#include "aac/AAC_AudioDecoder_Android.h"

#include "ElectraDecodersUtils.h"

#include "AudioDecoderAAC_Android.h"

class FAACAudioDecoderFactoryAndroid : public IElectraCodecFactory
{
public:
	virtual ~FAACAudioDecoderFactoryAndroid()
	{}

	int32 SupportsFormat(TMap<FString, FVariant>& OutFormatInfo, const FString& InCodecFormat, bool bInEncoder, const TMap<FString, FVariant>& InOptions) const override
	{
		// Encoder? Not supported here!
		if (bInEncoder)
		{
			return 0;
		}

		ElectraDecodersUtil::FMimeTypeAudioCodecInfo ci;
		// Codec?
		if (ElectraDecodersUtil::ParseCodecMP4A(ci, InCodecFormat))
		{
			// Ok.
		}
		// Mime type?
		else if (ElectraDecodersUtil::ParseMimeTypeWithCodec(ci, InCodecFormat))
		{
		}
		else
		{
			return 0;
		}

		// Check for the correct object type. Realistically this should be set.
		if (ci.ObjectType != 0x40)
		{
			return 0;
		}

		// AAC-LC, AAC-HE (SBR), AAC-HEv2 (PS) ?
		if (ci.Profile != 2 && ci.Profile != 5 && ci.Profile != 29)
		{
			return 0;
		}

		ci.ChannelConfiguration = (uint32) ElectraDecodersUtil::GetVariantValueSafeI64(InOptions, TEXT("channel_configuration"), 0);
		ci.NumberOfChannels = (int32) ElectraDecodersUtil::GetVariantValueSafeI64(InOptions, TEXT("num_channels"), 0);

		// At most 8 channels. Configurations 1-12 are supported.
		if (ci.NumberOfChannels > 8 || ci.ChannelConfiguration > 12)
		{
			return 0;
		}

		return 1;
	}

	void GetConfigurationOptions(TMap<FString, FVariant>& OutOptions) const override
	{
		IElectraAudioDecoderAAC_Android::GetConfigurationOptions(OutOptions);
	}

	TSharedPtr<IElectraDecoder, ESPMode::ThreadSafe> CreateDecoderForFormat(const FString& InCodecFormat, const TMap<FString, FVariant>& InOptions, TSharedPtr<IElectraDecoderResourceDelegate, ESPMode::ThreadSafe> InResourceDelegate) override
	{
		return IElectraAudioDecoderAAC_Android::Create(InOptions, InResourceDelegate);
	}

};


TSharedPtr<IElectraCodecFactory, ESPMode::ThreadSafe> FAACAudioDecoderAndroid::CreateFactory()
{
	return MakeShared<FAACAudioDecoderFactoryAndroid, ESPMode::ThreadSafe>();
}
