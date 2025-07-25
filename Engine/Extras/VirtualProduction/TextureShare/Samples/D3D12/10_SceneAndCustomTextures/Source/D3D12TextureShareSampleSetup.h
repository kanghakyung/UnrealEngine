// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ITextureShareObject.h"
#include "D3D12AppSetup.h"

namespace TextureShareSample
{
	static FTextureShareObjectDesc ObjectDesc(ETextureShareDeviceType::D3D12);

	namespace Receive
	{
		// The scene texture must be obtained before the custom textures.
		namespace Texture1
		{
			// Request to read a resource #2 to a remote process
			static FTextureShareResourceDesc Desc(UE::TextureShareStrings::SceneTextures::FinalColor, ETextureShareTextureOp::Read);

			// Container for receive: Texture size are not defined on the user side (values on the UE side are used)
			static FTextureShareResourceD3D12 Resource(EResourceSRV::texture1);
		}

		namespace Texture2
		{
			static constexpr auto Name = TEXT("Texture1");

			// Request to read a resource #1 to a remote process
			static FTextureShareResourceDesc Desc(Name, ETextureShareTextureOp::Read);

			// Container for receive: Texture size are not defined on the user side (values on the UE side are used)
			static FTextureShareResourceD3D12 Resource(EResourceSRV::texture2);
		}
	}

	namespace Send
	{
		namespace Backbuffer
		{
			static constexpr auto Name = TEXT("RTT_TextureShare");

			// Request for write to FinalColor resource
			static FTextureShareResourceDesc Desc(Name, ETextureShareTextureOp::Write);
		}
	}
};
