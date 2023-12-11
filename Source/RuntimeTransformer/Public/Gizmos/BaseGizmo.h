// Copyright 2020 Juan Marcelo Portillo. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RuntimeTransformer.h"
#include "BaseGizmo.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FGizmoStateChangedDelegate, ETransformationType, GizmoType, bool, bTransformInProgress, ETransformationDomain, CurrentDomain);

UCLASS()
class RUNTIMETRANSFORMER_API ABaseGizmo : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ABaseGizmo();

	virtual void Tick(float DeltaSeconds) override;

	virtual ETransformationType GetGizmoType() const { return ETransformationType::TT_NoTransform; }

	virtual void UpdateGizmoSpace(ESpaceType SpaceType);

	//Base Gizmo does not affect anything and returns No Delta Transform.
	// This func is overriden by each Transform Gizmo
	virtual FTransform GetDeltaTransform(const FVector& LookingVector, const FVector& RayStartPoint
		, const FVector& RayEvndPoint, ETransformationDomain Domain);

	/**
	 * Scales the Gizmo Scene depending on a Reference Point
	 * The scale depends on the Gizmo Screen Space Radius specified,
	 * and the Gizmo Scene Scale Factor.
	 * @param Reference Location - The Location of where the Gizmo is seen (i.e. Camera Location)
	 * @param Reference Look Direction - the direction the reference is looking (i.e. Camera Look Direction)
	 * @param FieldOfView - Field of View of Camera, in Degrees
	*/
	void ScaleGizmoScene(const FVector& ReferenceLocation, const FVector& ReferenceLookDirection, float FieldOfView = 90.f);

	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	ETransformationDomain GetTransformationDomain(class USceneComponent* ComponentHit) const;

	// Returns a Snapped Transform based on how much has been accumulated, the Delta Transform and Snapping Value
	// Also changes the Accumulated Transform based on how much was snapped
	virtual FTransform GetSnappedTransform(FTransform& outCurrentAccumulatedTransform
		, const FTransform& DeltaTransform
		, ETransformationDomain Domain
		, float SnappingValue) const ;

	// Snapped Transform per Component is used when we need Absolute Snapping
	// For Scaling, Absolute Snapping is needed and not delta ones 
	// for example, Object Scale (1) and Snapping of (5). Snapping sequence should be 5, 10... and not 6, 11...
	virtual FTransform GetSnappedTransformPerComponent(const FTransform& OldComponentTransform
		, const FTransform& NewComponentTransform
		, ETransformationDomain Domain
		, float SnappingValue) const {	return NewComponentTransform; }

protected:

	// Calculates the Gizmo Scene Scale. This can be overriden (e.g. by Rotation Gizmo)
	// for additional/optional scaling properties.
	virtual FVector CalculateGizmoSceneScale(const FVector& ReferenceLocation, const FVector& ReferenceLookDirection, float FieldOfView);

	// should be called at the start of the GetDeltaTransformation Implemenation
	// returns true if the PrevRays were valid before this Func was called. False if they were invalid (now they should be valid but the next tick should be waited)
	bool AreRaysValid() const;

	//should be called at the end of the GetDeltaTransformation Implemenation
	void UpdateRays(const FVector& RayStart, const FVector& RayEnd);

	/**
	 * Adds or modifies an entry to the DomainMap.
	*/
	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void RegisterDomainComponent(class USceneComponent* Component
		, ETransformationDomain Domain);

public:

	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	void SetTransformProgressState(bool bInProgress
		, ETransformationDomain CurrentDomain);

	UFUNCTION(BlueprintCallable, Category = "Gizmo")
	bool GetTransformProgressState() const { return bTransformInProgress; }

	/**
	 * Delegate that is called when the Transform State is changed (when it changes from
	 * in progress = true to false (and viceversa)
	 * Can be used to Change the meshes and visuals of Gizmo while Transform is in Progress
	 */
	UPROPERTY(BlueprintAssignable, Category = "Gizmo")
	FGizmoStateChangedDelegate OnGizmoStateChange;

protected:

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	class USceneComponent* RootScene;

	/* Scene Component that will go Under the Root Scene
	 * This is so that we can Scale all the things under it without Scaling the Actor itself (i.e. root component)
	*/
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	class USceneComponent* ScalingScene;

	// The Hit Box for the X-Axis Direction Transform
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	class UBoxComponent* X_AxisBox;

	// The Hit Box for the X-Axis Direction Transform
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	class UBoxComponent* Y_AxisBox;

	// The Hit Box for the X-Axis Direction Transform
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Gizmo")
	class UBoxComponent* Z_AxisBox;

	// Used to calculate the distance the rays have travelled
	FVector PreviousRayStartPoint;
	FVector PreviousRayEndPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	float GizmoSceneScaleFactor;

	/* The Radius of the Arc (FOV) that the Camera covers. The bigger the value, the smaller the Gizmo would look. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	float CameraArcRadius;

private:
	// Maps the Box Component to their Respective Domain
	TMap<class UShapeComponent*, ETransformationDomain> DomainMap;

	//Whether Transform is in Progress or Not 
	bool bTransformInProgress;

protected:

	//bool to check whether the PrevRay vectors have been set
	bool bIsPrevRayValid;
};

