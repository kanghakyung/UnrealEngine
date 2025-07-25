// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Render/Viewport/Configuration/DisplayClusterViewportConfigurationHelpers_ICVFX.h"
#include "ShaderParameters/DisplayClusterShaderParameters_ICVFX.h"

class UDisplayClusterICVFXCameraComponent;
struct FDisplayClusterConfigurationICVFX_CameraSettings;
class FDisplayClusterViewport;
class FDisplayClusterViewportConfiguration;

/**
* ICVFX Configurator: InCamera instance
*/
class FDisplayClusterViewportConfiguration_ICVFXCamera
{
public:
	FDisplayClusterViewportConfiguration_ICVFXCamera(FDisplayClusterViewportConfiguration& InConfiguration, UDisplayClusterICVFXCameraComponent& InCameraComponent, UDisplayClusterICVFXCameraComponent& InConfigurationCameraComponent)
		: Configuration(InConfiguration), CameraComponent(InCameraComponent), ConfigurationCameraComponent(InConfigurationCameraComponent)
	{ }

	/** Initialize CameraContext. */
	bool Initialize();

	/** Creates camera and chromakey viewports and initializes their target OuterViewports. */
	void Update();

	/** Returns true if the camera frustum is visible on the TargetViewport geometry. */
	bool IsCameraProjectionVisibleOnViewport(FDisplayClusterViewport* TargetViewport);

	/** Gets a ICVFX camera settings. */
	const FDisplayClusterConfigurationICVFX_CameraSettings& GetCameraSettings() const;

	/** Gets the name of the ICVFX camera. */
	FString GetCameraUniqueId() const;

	// Performance: Rnder CK only when it is in use.
	bool EnableChromakeyRendering() const;

	// Performance: If all chromakey sources have a ‘FrameColor’ value for all viewports on the current cluster node, we can skip rendering the InnerFrustum
	bool EnableInnerFrustumRendering() const;

protected:
	/** Creating a inner camera viewport for the ICVFX rendering stack. */
	bool CreateAndSetupInnerCameraViewport();

	/** Creating and setup a inner camera chromakey viewport for the ICVFX rendering stack. */
	bool CreateAndSetupInnerCameraChromakey();

	/** Creating a camera chromakey viewport for the ICVFX rendering stack. */
	bool ImplCreateChromakeyViewport();

public:
	struct FICVFXCameraContext
	{
		//@todo: add stereo context support
		FRotator ViewRotation;
		FVector  ViewLocation;
		FMatrix  PrjMatrix;
	};

	// Camera context, used for visibility test vs outer
	FICVFXCameraContext CameraContext;

	// The inner camera viewport ref
	TSharedPtr<FDisplayClusterViewport, ESPMode::ThreadSafe> CameraViewport;

	// The inner camera chromakey viewport ref
	TSharedPtr<FDisplayClusterViewport, ESPMode::ThreadSafe> ChromakeyViewport;

	struct FTargetViewport
	{
		FTargetViewport(const TSharedRef<FDisplayClusterViewport, ESPMode::ThreadSafe>& InViewport)
			: Viewport(InViewport)
		{ }

		// The reference to the viewport
		const TSharedRef<FDisplayClusterViewport, ESPMode::ThreadSafe> Viewport;

		// The camera chromakey type from this viewport.
		EDisplayClusterShaderParametersICVFX_ChromakeySource ChromakeySource = EDisplayClusterShaderParametersICVFX_ChromakeySource::Disabled;
	};

	// List of OuterViewports for this camera
	TArray<FTargetViewport> TargetViewports;

private:
	FDisplayClusterViewportConfiguration& Configuration;

	// Camera component in scene DCRA
	UDisplayClusterICVFXCameraComponent& CameraComponent;

	// Camera component in configuration DCRA
	UDisplayClusterICVFXCameraComponent& ConfigurationCameraComponent;
};
