// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IElectraDecoder.h"


class FElectraDecoderBitstreamProcessorVPx : public IElectraDecoderBitstreamProcessor
{
public:
	static TSharedPtr<IElectraDecoderBitstreamProcessor, ESPMode::ThreadSafe> Create(const TMap<FString, FVariant>& InDecoderParams, const TMap<FString, FVariant>& InFormatParams)
	{ return MakeShareable<>(new FElectraDecoderBitstreamProcessorVPx(InDecoderParams, InFormatParams)); }

	virtual ~FElectraDecoderBitstreamProcessorVPx();
	bool WillModifyBitstreamInPlace() override;
	void Clear() override;
	EProcessResult GetCSDFromConfigurationRecord(TArray<uint8>& OutCSD, const TMap<FString, FVariant>& InParamsWithDCRorCSD) override;
	EProcessResult ProcessInputForDecoding(TSharedPtr<IElectraDecoderBitstreamInfo, ESPMode::ThreadSafe>& OutBSI, FElectraDecoderInputAccessUnit& InOutAccessUnit, const TMap<FString, FVariant>& InAccessUnitSidebandData) override;
	void SetPropertiesOnOutput(TMap<FString, FVariant>& InOutProperties, TSharedPtr<IElectraDecoderBitstreamInfo, ESPMode::ThreadSafe> InBSI) override;
	FString GetLastError() override;

private:
	ELECTRADECODERS_API FElectraDecoderBitstreamProcessorVPx(const TMap<FString, FVariant>& InDecoderParams, const TMap<FString, FVariant>& InFormatParams);
	class FImpl;
	TPimplPtr<FImpl> Impl;
};
