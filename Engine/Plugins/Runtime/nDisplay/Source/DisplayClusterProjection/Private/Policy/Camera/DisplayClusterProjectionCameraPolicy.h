// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Policy/DisplayClusterProjectionPolicyBase.h"
#include "Containers/DisplayClusterProjectionCameraPolicySettings.h"
#include "Misc/DisplayClusterObjectRef.h"

class UCameraComponent;


/**
 * Camera projection policy implementation
 */
class FDisplayClusterProjectionCameraPolicy
	: public FDisplayClusterProjectionPolicyBase
{
public:
	FDisplayClusterProjectionCameraPolicy(const FString& ProjectionPolicyId, const FDisplayClusterConfigurationProjection* InConfigurationProjectionPolicy);

public:
	//~ Begin IDisplayClusterProjectionPolicy
	virtual const FString& GetType() const override;

	virtual bool HandleStartScene(IDisplayClusterViewport* InViewport) override;
	virtual void HandleEndScene(IDisplayClusterViewport* InViewport) override;

	virtual bool CalculateView(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FVector& InOutViewLocation, FRotator& InOutViewRotation, const FVector& ViewOffset, const float WorldToMeters, const float NCP, const float FCP) override;
	virtual bool GetProjectionMatrix(IDisplayClusterViewport* InViewport, const uint32 InContextNum, FMatrix& OutPrjMatrix) override;
	virtual void SetupProjectionViewPoint(IDisplayClusterViewport* InViewport, const float InDeltaTime, FMinimalViewInfo& InOutViewInfo, float* OutCustomNearClippingPlane = nullptr) override;

	virtual void UpdatePostProcessSettings(IDisplayClusterViewport* InViewport) override;

	virtual bool ShouldUseSourceTextureWithMips(const IDisplayClusterViewport* InViewport) const override
	{
		return true;
	}

	virtual bool ShouldUseViewPointComponentPostProcesses(IDisplayClusterViewport* InViewport) const override
	{
		// This projection policy uses its own camera with postprocessing.
		// Disable the use of PP settings from the ViewPoint component.
		return false;
	}
	//~ End IDisplayClusterProjectionPolicy

public:
	void SetCamera(UCameraComponent* const NewCamera, const FDisplayClusterProjectionCameraPolicySettings& InCameraSettings);

	/** Returns the camera component that is currently in use. */
	UCameraComponent* GetCameraComponent() const;

private:
	bool ImplSetupProjectionViewPoint(IDisplayClusterViewport* InViewport, const float InDeltaTime, FMinimalViewInfo& InOutViewInfo, float* OutCustomNearClippingPlane = nullptr) const;

protected:

private:
	// Camera to use for rendering
	FDisplayClusterSceneComponentRef CameraRef;

	// Camera settings
	FDisplayClusterProjectionCameraPolicySettings CameraSettings;

	// Values of the clipping planes (saved from CalculateView())
	float ZNear = 1.f;
	float ZFar = 1.f;

	// camera FOV value in degrees (saved from the SetupProjectionViewPoint() function)
	float CameraFOVDegrees = 90;

	// camera aspect ratio value (saved from the SetupProjectionViewPoint() function)
	float CameraAspectRatio = 1;
};
