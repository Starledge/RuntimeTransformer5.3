// Copyright 2020 Juan Marcelo Portillo. All Rights Reserved.


#include "TransformerPawn.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#include "Net/UnrealNetwork.h"
#include "Kismet/GameplayStatics.h"

/* Gizmos */
#include "Gizmos/BaseGizmo.h"
#include "Gizmos/TranslationGizmo.h"
#include "Gizmos/RotationGizmo.h"
#include "Gizmos/ScaleGizmo.h"

/* Interface */
#include "FocusableObject.h"

// Sets default values
ATransformerPawn::ATransformerPawn()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	GizmoPlacement			= EGizmoPlacement::GP_OnLastSelection;
	CurrentTransformation	= ETransformationType::TT_Translation;
	CurrentDomain			= ETransformationDomain::TD_None;
	CurrentSpaceType		= ESpaceType::ST_World;
	TranslationGizmoClass	= ATranslationGizmo::StaticClass();
	RotationGizmoClass		= ARotationGizmo::StaticClass();
	ScaleGizmoClass			= AScaleGizmo::StaticClass();

	CloneReplicationCheckFrequency = 0.05f;
	MinimumCloneReplicationTime = 0.01f;

	bResyncSelection = false;
	bReplicates = false;
	bIgnoreNonReplicatedObjects = false;

	ResetDeltaTransform(AccumulatedDeltaTransform);
	ResetDeltaTransform(NetworkDeltaTransform);

	SetTransformationType(CurrentTransformation);
	SetSpaceType(CurrentSpaceType);

	bTransformUFocusableObjects = true;
	bRotateOnLocalAxis = false;
	bForceMobility = false;
	bToggleSelectedInMultiSelection = true;
	bComponentBased = false;
}

void ATransformerPawn::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	//Fill here if we need to replicate Properties. For now, nothing needs constant replication/check
}

UObject* ATransformerPawn::GetUFocusable(USceneComponent* Component) const
{
	if (!Component) return nullptr;
	if (bComponentBased)
		return (Component->Implements<UFocusableObject>()) ? Component : nullptr;
	else if (AActor* ComponentOwner = Component->GetOwner())
		return (ComponentOwner->Implements<UFocusableObject>()) ? ComponentOwner : nullptr;
	return nullptr;
}

void ATransformerPawn::SetTransform(USceneComponent* Component, const FTransform& Transform)
{
	if (!Component) return;
	if (UObject* focusableObject = GetUFocusable(Component))
	{
		IFocusableObject::Execute_OnNewTransformation(focusableObject, this, Component, Transform, bComponentBased);
		if (bTransformUFocusableObjects)
			Component->SetWorldTransform(Transform);
	}
	else
		Component->SetWorldTransform(Transform);

}

void ATransformerPawn::Select(USceneComponent* Component, bool* bImplementsUFocusable)
{
	UObject* focusableObject = GetUFocusable(Component);
	if (focusableObject)
		IFocusableObject::Execute_Focus(focusableObject, this, Component, bComponentBased);
	if (bImplementsUFocusable)
		*bImplementsUFocusable = !!focusableObject;
}

void ATransformerPawn::Deselect(USceneComponent* Component, bool* bImplementsUFocusable)
{
	UObject* focusableObject = GetUFocusable(Component);
	if (focusableObject)
		IFocusableObject::Execute_Unfocus(focusableObject, this, Component, bComponentBased);
	if (bImplementsUFocusable)
		*bImplementsUFocusable = !!focusableObject;
}

void ATransformerPawn::FilterHits(TArray<FHitResult>& outHits)
{
	//eliminate all outHits that have non-replicated objects
	if (bIgnoreNonReplicatedObjects)
	{
		for (auto Iter = outHits.CreateIterator(); Iter; ++Iter)
		{
			//don't remove Gizmos! They do not replicate by default 
			if(Cast<ABaseGizmo>(Iter->GetActor()))
				continue;
		
			if (IsValid(Iter->GetActor()) && Iter->GetActor()->IsSupportedForNetworking())
			{
				if (bComponentBased)
				{
					if (Iter->Component.IsValid() && Iter->Component->IsSupportedForNetworking())
						continue; //components - actor owner + themselves need to replicate
				}
				else
					continue; //actors only consider whether they replicate
			}

			if (IsValid(Iter->GetActor()) && Iter->Component.IsValid())
			{
				UE_LOG(LogRuntimeTransformer, Warning, TEXT("Removing (Actor: %s   ComponentHit:  %s) from hits because it is not supported for networking.")
					, *Iter->GetActor()->GetName(), *Iter->Component->GetName());
			}
			
			Iter.RemoveCurrent();
		}


	}
}

void ATransformerPawn::SetSpaceType(ESpaceType Type)
{
	CurrentSpaceType = Type;
	SetGizmo();
}

ETransformationDomain ATransformerPawn::GetCurrentDomain(bool& TransformInProgress) const
{
	TransformInProgress = (CurrentDomain != ETransformationDomain::TD_None);
	return CurrentDomain;
}

void ATransformerPawn::ClearDomain()
{
	//Clear the Accumulated tranform when we stop Transforming
	ResetDeltaTransform(AccumulatedDeltaTransform);
	SetDomain(ETransformationDomain::TD_None);
}

bool ATransformerPawn::GetMouseStartEndPoints(float TraceDistance, FVector& outStartPoint, FVector& outEndPoint)
{
	if (APlayerController* PlayerController = Cast< APlayerController>(Controller))
	{
		FVector worldLocation, worldDirection;
		if (PlayerController->DeprojectMousePositionToWorld(worldLocation, worldDirection))
		{
			outStartPoint = worldLocation;
			outEndPoint = worldLocation + (worldDirection * TraceDistance);
			return true;
		}
	}
	return false;
}

UClass* ATransformerPawn::GetGizmoClass(ETransformationType TransformationType) const /* private */
{
	//Assign correct Gizmo Class depending on given Transformation
	switch (CurrentTransformation)
	{
	case ETransformationType::TT_Translation:	return TranslationGizmoClass;
	case ETransformationType::TT_Rotation:		return RotationGizmoClass;
	case ETransformationType::TT_Scale:			return ScaleGizmoClass;
	default:									return nullptr;
	}
}

void ATransformerPawn::ResetDeltaTransform(FTransform& Transform)
{
	Transform = FTransform();
	Transform.SetScale3D(FVector::ZeroVector);
}

void ATransformerPawn::SetDomain(ETransformationDomain Domain)
{
	CurrentDomain = Domain;

	if (Gizmo.IsValid())
		Gizmo->SetTransformProgressState(CurrentDomain != ETransformationDomain::TD_None
			, CurrentDomain);
}

bool ATransformerPawn::MouseTraceByObjectTypes(float TraceDistance
	, TArray<TEnumAsByte<ECollisionChannel>> CollisionChannels
	, TArray<AActor*> IgnoredActors, bool bAppendToList)
{
	FVector start, end;
	bool bTraceSuccessful = false;
	if (GetMouseStartEndPoints(TraceDistance, start, end))
	{
		bTraceSuccessful = TraceByObjectTypes(start, end, CollisionChannels
			, IgnoredActors, bAppendToList);

		if (!bTraceSuccessful && !bAppendToList)
			ServerDeselectAll(false);
	}
	return bTraceSuccessful;
}

bool ATransformerPawn::MouseTraceByChannel(float TraceDistance
	, TEnumAsByte<ECollisionChannel> TraceChannel, TArray<AActor*> IgnoredActors
	, bool bAppendToList)
{
	FVector start, end;
	bool bTraceSuccessful = false;
	if (GetMouseStartEndPoints(TraceDistance, start, end))
	{
		bTraceSuccessful = TraceByChannel(start, end, TraceChannel
			, IgnoredActors, bAppendToList);
		if (!bTraceSuccessful && !bAppendToList)
			ServerDeselectAll(false);
	}
	return false;
}

bool ATransformerPawn::MouseTraceByProfile(float TraceDistance
	, const FName& ProfileName
	, TArray<AActor*> IgnoredActors
	, bool bAppendToList)
{
	FVector start, end;
	bool bTraceSuccessful = false;
	if (GetMouseStartEndPoints(TraceDistance, start, end))
	{

		bTraceSuccessful = TraceByProfile(start, end, ProfileName
			, IgnoredActors, bAppendToList);
		if (!bTraceSuccessful && !bAppendToList)
			ServerDeselectAll(false);

	}
	return bTraceSuccessful;
}

bool ATransformerPawn::TraceByObjectTypes(const FVector& StartLocation
	, const FVector& EndLocation
	, TArray<TEnumAsByte<ECollisionChannel>> CollisionChannels
	, TArray<AActor*> IgnoredActors
	, bool bAppendToList)
{
	if (UWorld* world = GetWorld())
	{
		FCollisionObjectQueryParams CollisionObjectQueryParams;
		FCollisionQueryParams CollisionQueryParams;

		//Add All Given Collisions to the Array
		for (auto& cc : CollisionChannels)
			CollisionObjectQueryParams.AddObjectTypesToQuery(cc);

		CollisionQueryParams.AddIgnoredActors(IgnoredActors);

		TArray<FHitResult> OutHits;
		if (world->LineTraceMultiByObjectType(OutHits, StartLocation, EndLocation
			, CollisionObjectQueryParams, CollisionQueryParams))
		{
			FilterHits(OutHits);
			return HandleTracedObjects(OutHits, bAppendToList);
		}
	}
	return false;
}

bool ATransformerPawn::TraceByChannel(const FVector& StartLocation
	, const FVector& EndLocation
	, TEnumAsByte<ECollisionChannel> TraceChannel
	, TArray<AActor*> IgnoredActors
	, bool bAppendToList)
{
	if (UWorld* world = GetWorld())
	{
		FCollisionQueryParams CollisionQueryParams;
		CollisionQueryParams.AddIgnoredActors(IgnoredActors);

		TArray<FHitResult> OutHits;
		if (world->LineTraceMultiByChannel(OutHits, StartLocation, EndLocation
			, TraceChannel, CollisionQueryParams))
		{
			FilterHits(OutHits);
			return HandleTracedObjects(OutHits, bAppendToList);
		}
	}
	return false;
}

bool ATransformerPawn::TraceByProfile(const FVector& StartLocation
	, const FVector& EndLocation
	, const FName& ProfileName, TArray<AActor*> IgnoredActors
	, bool bAppendToList)
{
	if (UWorld* world = GetWorld())
	{
		FCollisionQueryParams CollisionQueryParams;
		CollisionQueryParams.AddIgnoredActors(IgnoredActors);

		TArray<FHitResult> OutHits;
		if (world->LineTraceMultiByProfile(OutHits, StartLocation, EndLocation
			, ProfileName, CollisionQueryParams))
		{
			FilterHits(OutHits);
			return HandleTracedObjects(OutHits, bAppendToList);
		}
	}
	return false;
}

#include "Kismet/GameplayStatics.h"
void ATransformerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Gizmo.IsValid()) return;

	if (APlayerController* PlayerController = Cast< APlayerController>(Controller))
	{
		FVector worldLocation, worldDirection;
		if (PlayerController->IsLocalController() && PlayerController->PlayerCameraManager)
		{
			if (PlayerController->DeprojectMousePositionToWorld(worldLocation, worldDirection))
			{
				FTransform deltaTransform = UpdateTransform(PlayerController->PlayerCameraManager->GetActorForwardVector()
					, worldLocation, worldDirection);

				NetworkDeltaTransform = FTransform(
					deltaTransform.GetRotation() * NetworkDeltaTransform.GetRotation(),
					deltaTransform.GetLocation() + NetworkDeltaTransform.GetLocation(),
					deltaTransform.GetScale3D() + NetworkDeltaTransform.GetScale3D());
			}
				
		}			
	}
	
	//Only consider Local View
	if (APlayerController* LocalPlayerController = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (LocalPlayerController->PlayerCameraManager)
		{
			Gizmo->ScaleGizmoScene(LocalPlayerController->PlayerCameraManager->GetCameraLocation()
				, LocalPlayerController->PlayerCameraManager->GetActorForwardVector()
				, LocalPlayerController->PlayerCameraManager->GetFOVAngle());
		}
	}



	Gizmo->UpdateGizmoSpace(CurrentSpaceType); //ToDo: change when this is called to improve performance when a gizmo is there without doing anything
}

FTransform ATransformerPawn::UpdateTransform(const FVector& LookingVector
	, const FVector& RayOrigin
	, const FVector& RayDirection)
{
	FTransform deltaTransform;
	deltaTransform.SetScale3D(FVector::ZeroVector);

	if (!Gizmo.IsValid() || CurrentDomain == ETransformationDomain::TD_None) 
		return deltaTransform;

	FVector rayEnd = RayOrigin + 1'000'000'00 * RayDirection;

	FTransform calcDeltaTransform = Gizmo->GetDeltaTransform(LookingVector, RayOrigin, rayEnd, CurrentDomain);

	//The delta transform we are actually going to apply (same if there is no Snapping taking place)
	deltaTransform = calcDeltaTransform;

	/* SNAPPING LOGIC */
	bool* snappingEnabled = SnappingEnabled.Find(CurrentTransformation);
	float* snappingValue = SnappingValues.Find(CurrentTransformation);

	if (snappingEnabled && *snappingEnabled && snappingValue)
			deltaTransform = Gizmo->GetSnappedTransform(AccumulatedDeltaTransform
				, calcDeltaTransform, CurrentDomain, *snappingValue);
				//GetSnapped Transform Modifies Accumulated Delta Transform by how much Snapping Occurred
	
	ApplyDeltaTransform(deltaTransform);
	return deltaTransform;
}

void ATransformerPawn::ApplyDeltaTransform(const FTransform& DeltaTransform)
{
	bool* snappingEnabled = SnappingEnabled.Find(CurrentTransformation);
	float* snappingValue = SnappingValues.Find(CurrentTransformation);

	for (auto& sc : SelectedComponents)
	{
		if (!sc) continue;
		if (bForceMobility || sc->Mobility == EComponentMobility::Type::Movable)
		{
			const FTransform& componentTransform = sc->GetComponentTransform();

			FQuat deltaRotation = DeltaTransform.GetRotation();

			FVector deltaLocation = componentTransform.GetLocation()
				- Gizmo->GetActorLocation();

			//DeltaScale is Unrotated Scale to Get Local Scale since World Scale is not supported
			FVector deltaScale = componentTransform.GetRotation()
				.UnrotateVector(DeltaTransform.GetScale3D());


			if (false == bRotateOnLocalAxis)
				deltaLocation = deltaRotation.RotateVector(deltaLocation);

			FTransform newTransform(
				deltaRotation * componentTransform.GetRotation(),
				//adding Gizmo Location + prevDeltaLocation 
				// (i.e. location from Gizmo to Object after optional Rotating)
				// + deltaTransform Location Offset
				deltaLocation + Gizmo->GetActorLocation() + DeltaTransform.GetLocation(),
				deltaScale + componentTransform.GetScale3D());


			/* SNAPPING LOGIC PER COMPONENT */
			if (snappingEnabled && *snappingEnabled && snappingValue)
				newTransform = Gizmo->GetSnappedTransformPerComponent(componentTransform
					, newTransform, CurrentDomain, *snappingValue);

			sc->SetMobility(EComponentMobility::Type::Movable);
			SetTransform(sc, newTransform);
		}
		else
		{
			UE_LOG(LogRuntimeTransformer, Warning, TEXT("Transform will not affect Component [%s] as it is NOT Moveable!"), *sc->GetName());
		}
			
	}
}

bool ATransformerPawn::HandleTracedObjects(const TArray<FHitResult>& HitResults
	, bool bAppendToList)
{
	//Assign as None just in case we don't hit Any Gizmos
	ClearDomain();

	//Search for our Gizmo (if Valid) First before Selecting any item
	if (Gizmo.IsValid())
	{
		for (auto& hitResult : HitResults)
		{
			if (Gizmo == hitResult.GetActor()) //check if it's OUR gizmo
			{
				//Check which Domain of Gizmo was Hit from the Test
				if (USceneComponent* componentHit = Cast<USceneComponent>(hitResult.Component))
				{
					SetDomain(Gizmo->GetTransformationDomain(componentHit));
					if (CurrentDomain != ETransformationDomain::TD_None)
					{
						Gizmo->SetTransformProgressState(true, CurrentDomain);
						return true; //finish only if the component actually has a domain, else continue
					}
				}
			}
		}
	}

	for (auto& hits : HitResults)
	{
		if (Cast<ABaseGizmo>(hits.GetActor()))
			continue; //ignore other Gizmos.

		if (bComponentBased)
			SelectComponent(Cast<USceneComponent>(hits.GetComponent()), bAppendToList);
		else
			SelectActor(hits.GetActor(), bAppendToList);

		return true; //don't process more!
	}

	return false;
}

void ATransformerPawn::SetComponentBased(bool bIsComponentBased)
{
	auto selectedComponents = DeselectAll();
	bComponentBased = bIsComponentBased;
	if(bComponentBased)
		SelectMultipleComponents(selectedComponents, false);
	else
	{
		TArray<AActor*> actors;
		for (auto& c : selectedComponents)
			actors.Add(c->GetOwner());
		SelectMultipleActors(actors, false);
	}

}

void ATransformerPawn::SetRotateOnLocalAxis(bool bRotateLocalAxis)
{
	bRotateOnLocalAxis = bRotateLocalAxis;
}

void ATransformerPawn::SetTransformationType(ETransformationType TransformationType)
{
	//Don't continue if these are the same.
	if (CurrentTransformation == TransformationType) return;

	if (TransformationType == ETransformationType::TT_NoTransform)
    {
        UE_LOG(LogRuntimeTransformer, Warning, TEXT("Setting Transformation Type to None!"));
    }
       

	CurrentTransformation = TransformationType;

	//Clear the Accumulated tranform when we have a new Transformation
	ResetDeltaTransform(AccumulatedDeltaTransform);

	UpdateGizmoPlacement();
}

void ATransformerPawn::SetSnappingEnabled(ETransformationType TransformationType, bool bSnappingEnabled)
{
	SnappingEnabled.Add(TransformationType, bSnappingEnabled);
}

void ATransformerPawn::SetSnappingValue(ETransformationType TransformationType, float SnappingValue)
{
	SnappingValues.Add(TransformationType, SnappingValue);
}

void ATransformerPawn::GetSelectedComponents(TArray<class USceneComponent*>& outComponentList
	, USceneComponent*& outGizmoPlacedComponent) const
{
	outComponentList = SelectedComponents;
	if (Gizmo.IsValid())
		outGizmoPlacedComponent = Gizmo->GetParentComponent();
}

TArray<USceneComponent*> ATransformerPawn::GetSelectedComponents() const
{
	return SelectedComponents;
}

void ATransformerPawn::CloneSelected(bool bSelectNewClones
	, bool bAppendToList)
{
	if (GetLocalRole() < ROLE_Authority)
    {
        UE_LOG(LogRuntimeTransformer, Warning, TEXT("Cloning in a Non-Authority! Please use the Clone RPCs instead"));
    }
		

	auto CloneComponents = CloneFromList(SelectedComponents);

	if (bSelectNewClones)
		SelectMultipleComponents(CloneComponents, bAppendToList);
}

TArray<class USceneComponent*> ATransformerPawn::CloneFromList(const TArray<USceneComponent*>& ComponentList)
{

	TArray<class USceneComponent*> outClones;
	if (bComponentBased)
	{
		TArray<USceneComponent*> Components;
		
		for (auto& i : ComponentList)
		{
			if (i) Components.Add(i);
		}
		outClones = CloneComponents(Components);
	}
	else
	{
		TArray<AActor*> Actors;
		for (auto& i : ComponentList)
		{
			if (i)
				Actors.Add(i->GetOwner());
		}
		outClones = CloneActors(Actors);
	}

	if (CurrentDomain != ETransformationDomain::TD_None && Gizmo.IsValid())
		Gizmo->SetTransformProgressState(true, CurrentDomain);

	return outClones;
}

TArray<class USceneComponent*> ATransformerPawn::CloneActors(const TArray<AActor*>& Actors)
{
	TArray<class USceneComponent*> outClones;

	UWorld* world = GetWorld();
	if (!world) return outClones;
	
	TSet<AActor*>	actorsProcessed;
	TArray<AActor*> actorsToSelect;
	for (auto& templateActor : Actors)
	{
		if (!templateActor) continue;
		bool bAlreadyProcessed;
		actorsProcessed.Add(templateActor, &bAlreadyProcessed);
		if (bAlreadyProcessed) continue;

		FTransform spawnTransform;
		FActorSpawnParameters spawnParams;

		spawnParams.Template = templateActor;
		if (templateActor)
			templateActor->bNetStartup = false;

		if (AActor* actor = world->SpawnActor(templateActor->GetClass()
			, &spawnTransform, spawnParams))
		{
			outClones.Add(actor->GetRootComponent());
		}
	}
	return outClones;
}

TArray<class USceneComponent*> ATransformerPawn::CloneComponents(const TArray<class USceneComponent*>& Components)
{
	TArray<class USceneComponent*> outClones;

	UWorld* world = GetWorld();
	if (!world) return outClones;

	TMap<USceneComponent*, USceneComponent*> OcCc; //Original component - Clone component
	TMap<USceneComponent*, USceneComponent*> CcOp; //Clone component - Original parent

	//clone components phase
	for (auto& templateComponent : Components)
	{
		if (!templateComponent) continue;
		AActor* owner = templateComponent->GetOwner();
		if (!owner) continue;

		if (USceneComponent* clone = Cast<USceneComponent>(
			StaticDuplicateObject(templateComponent, owner)))
		{
			PostCreateBlueprintComponent(clone);
			clone->OnComponentCreated();

			clone->RegisterComponent();
			clone->SetRelativeTransform(templateComponent->GetRelativeTransform());

			outClones.Add(clone);
			//Add to these two maps for reparenting in next phase
			OcCc.Add(templateComponent, clone); //Original component - Clone component

			if (templateComponent == owner->GetRootComponent())
				CcOp.Add(clone, owner->GetRootComponent()); //this will cause a loop in the maps, so we must check for this!
			else 
				CcOp.Add(clone, templateComponent->GetAttachParent()); //Clone component - Original parent
		}
	}

	TArray<USceneComponent*> componentsToSelect;
	//reparenting phase
	FAttachmentTransformRules attachmentRule(EAttachmentRule::KeepWorld, false);
	for (auto& cp : CcOp)
	{
		//original parent
		USceneComponent* parent = cp.Value; 

		AActor* actorOwner = cp.Value->GetOwner();

		//find if we cloned the original parent
		USceneComponent** cloneParent = OcCc.Find(parent); 

		if (cloneParent)
		{
			if (*cloneParent != cp.Key) //make sure comp is not its own parent
				parent = *cloneParent;
		}
		else 
		{
			//couldn't find its parent, so find the parent of the parent and see if it's in the list.
			//repeat until found or root is reached
			while (1)
			{
				//if parent is root, then no need to find parents above it. (none)
				//attach to original parent, since there are no cloned parents.
				if (parent == actorOwner->GetRootComponent())
				{
					parent = cp.Value;
					break;
				}
				
				//check if parents have been cloned
				cloneParent = OcCc.Find(parent->GetAttachParent());
				if (cloneParent)
				{
					//attach to cloned parent if found
					parent = *cloneParent;
					break;
				}
				parent = parent->GetAttachParent(); //move up in the hierarchy
			}
		}

		cp.Key->AttachToComponent(parent, attachmentRule);

		//Selecting childs and parents can cause weird issues 
		// so only select the topmost clones (those that do not have cloned parents!)
		//only select those that have an "original parent". 
		//if ((parent == cp.Value 
		//	|| parent == actorOwner->GetRootComponent())) 
		//	//check if the parent of the cloned is original (means it's topmost)
		//	outParents.Add(cp.Key);
	}

	return outClones;
}

void ATransformerPawn::SelectComponent(class USceneComponent* Component
	, bool bAppendToList)
{
	if (!Component) return;

	if (ShouldSelect(Component->GetOwner(), Component))
	{
		if (false == bAppendToList)
			DeselectAll();
		AddComponent_Internal(SelectedComponents, Component);
		UpdateGizmoPlacement();
	}
}

void ATransformerPawn::SelectActor(AActor* Actor
	, bool bAppendToList)
{
	if (!Actor) return;

	if (ShouldSelect(Actor, Actor->GetRootComponent()))
	{
		if (false == bAppendToList)
			DeselectAll();
		AddComponent_Internal(SelectedComponents, Actor->GetRootComponent());
		UpdateGizmoPlacement();
	}
}

void ATransformerPawn::SelectMultipleComponents(const TArray<USceneComponent*>& Components
	, bool bAppendToList)
{
	bool bValidList = false;

	for (auto& c : Components)
	{
		if (!c) continue;
		if (!ShouldSelect(c->GetOwner(), c)) continue;

		if (false == bAppendToList)
		{
			DeselectAll();
			bAppendToList = true;
			//only run once. This is not place outside in case a list is empty or contains only invalid components
		}
		bValidList = true;
		AddComponent_Internal(SelectedComponents, c);
	}

	if(bValidList) UpdateGizmoPlacement();
}

void ATransformerPawn::SelectMultipleActors(const TArray<AActor*>& Actors
	, bool bAppendToList)
{
	bool bValidList = false;
	for (auto& a : Actors)
	{
		if (!a) continue;
		if (!ShouldSelect(a, a->GetRootComponent())) continue;

		if (false == bAppendToList)
		{
			DeselectAll();
			bAppendToList = true;
			//only run once. This is not place outside in case a list is empty or contains only invalid components
		}

		bValidList = true;
		AddComponent_Internal(SelectedComponents, a->GetRootComponent());
	}
	if(bValidList) UpdateGizmoPlacement();
}

void ATransformerPawn::DeselectComponent(USceneComponent* Component)
{
	if (!Component) return;
	DeselectComponent_Internal(SelectedComponents, Component);
	UpdateGizmoPlacement();
}

void ATransformerPawn::DeselectActor(AActor* Actor)
{
	if (Actor)
		DeselectComponent(Actor->GetRootComponent());
}

TArray<USceneComponent*> ATransformerPawn::DeselectAll(bool bDestroyDeselected)
{
	TArray<USceneComponent*> componentsToDeselect = SelectedComponents;
	for (auto& i : componentsToDeselect)
		DeselectComponent(i);
		//calling internal so as not to modify SelectedComponents until the last bit!
	SelectedComponents.Empty();
	UpdateGizmoPlacement();

	if (bDestroyDeselected)
	{
		for (auto& c : componentsToDeselect)
		{
			if (!IsValid(c)) continue; //a component that was in the same actor destroyed will be pending kill
			if (AActor* actor = c->GetOwner())
			{
				//We destroy the actor if no components are left to destroy, or the system is currently ActorBased
				if (bComponentBased && actor->GetComponents().Num() > 1)
					c->DestroyComponent(true);
				else
					actor->Destroy();
			}
		}
	}

	return componentsToDeselect;
}

void ATransformerPawn::AddComponent_Internal(TArray<USceneComponent*>& OutComponentList
	, USceneComponent* Component)
{
	//if (!Component) return; //assumes that previous have checked, since this is Internal.

	int32 Index = OutComponentList.Find(Component);

	if (INDEX_NONE == Index) //Component is not in list
	{
		OutComponentList.Emplace(Component);
		bool bImplementsInterface;
		Select(OutComponentList.Last(), &bImplementsInterface);
		OnComponentSelectionChange(Component, true, bImplementsInterface);
	}
	else if (bToggleSelectedInMultiSelection)
		DeselectComponentAtIndex_Internal(OutComponentList, Index);
}

void ATransformerPawn::DeselectComponent_Internal(TArray<USceneComponent*>& OutComponentList
	, USceneComponent* Component)
{
	//if (!Component) return; //assumes that previous have checked, since this is Internal.

	int32 Index = OutComponentList.Find(Component);
	if (INDEX_NONE != Index)
		DeselectComponentAtIndex_Internal(OutComponentList, Index);
}

void ATransformerPawn::DeselectComponentAtIndex_Internal(
	TArray<USceneComponent*>& OutComponentList
	, int32 Index)
{
	//if (!Component) return; //assumes that previous have checked, since this is Internal.
	if (OutComponentList.IsValidIndex(Index))
	{
		USceneComponent* Component = OutComponentList[Index];
		bool bImplementsInterface;
		Deselect(Component, &bImplementsInterface);
		OutComponentList.RemoveAt(Index);
		OnComponentSelectionChange(Component, false, bImplementsInterface);
	}

}

void ATransformerPawn::SetGizmo()
{

	//If there are selected components, then we see whether we need to create a new gizmo.
	if (SelectedComponents.Num() > 0)
	{
		bool bCreateGizmo = true;
		if (Gizmo.IsValid())
		{
			if (CurrentTransformation == Gizmo->GetGizmoType())
			{
				bCreateGizmo = false; // do not create gizmo if there is already a matching gizmo
			}
			else
			{
				// Destroy the current gizmo as the transformation types do not match
				Gizmo->Destroy();
				Gizmo.Reset();
			}
		}

		if (bCreateGizmo)
		{
			if (UWorld* world = GetWorld())
			{
				UClass* GizmoClass = GetGizmoClass(CurrentTransformation);
				if (GizmoClass)
				{
					Gizmo = Cast<ABaseGizmo>(world->SpawnActor(GizmoClass));
					Gizmo->OnGizmoStateChange.AddDynamic(this, &ATransformerPawn::OnGizmoStateChanged);
				}
			}
		}
	}
	//Since there are no selected components, we must destroy any gizmos present
	else
	{
		if (Gizmo.IsValid())
		{
			Gizmo->Destroy();
			Gizmo.Reset();
		}
	}


}

void ATransformerPawn::UpdateGizmoPlacement()
{
	SetGizmo();
	//means that there are no active gizmos (no selections) so nothing to do in this func
	if (!Gizmo.IsValid()) return;

	USceneComponent* ComponentToAttachTo = nullptr;

	switch (GizmoPlacement)
	{
	case EGizmoPlacement::GP_OnFirstSelection: 
		ComponentToAttachTo = SelectedComponents[0]; break;
	case EGizmoPlacement::GP_OnLastSelection:
		ComponentToAttachTo = SelectedComponents.Last(); break;
	}

	if (ComponentToAttachTo)
	{
		Gizmo->AttachToComponent(ComponentToAttachTo
		, FAttachmentTransformRules::SnapToTargetIncludingScale);
	}
	else
	{
	//	Gizmo->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	Gizmo->UpdateGizmoSpace(CurrentSpaceType);
}


///////////////////////// NETWORKING ////////////////////////////////////////////////////////////////////////


void ATransformerPawn::ReplicatedMouseTraceByObjectTypes(float TraceDistance
	, TArray<TEnumAsByte<ECollisionChannel>> CollisionChannels, bool bAppendToList)
{
	FVector start, end;
	if (GetMouseStartEndPoints(TraceDistance, start, end))
	{
		bool bTraceSuccessful = TraceByObjectTypes(start, end
			, CollisionChannels
			, TArray<AActor*>(), bAppendToList);

		//Server
		if (GetLocalRole() == ROLE_Authority)
			ReplicateServerTraceResults(bTraceSuccessful, bAppendToList);
		//Client
		else
		{
			if (!bTraceSuccessful && !bAppendToList)
				ServerDeselectAll(false);
			else
			{
				// If a Local Trace was on a Gizmo, just tell the Server that we 
				// have hit our Gizmo and just change the Domain there.
				// Else, do the Server Trace
				if (CurrentDomain == ETransformationDomain::TD_None)
					ServerTraceByObjectTypes(start, end, CollisionChannels, bAppendToList);
				else
					ServerSetDomain(CurrentDomain);
			}
			
		}

		
	}
}

void ATransformerPawn::ReplicatedMouseTraceByChannel(float TraceDistance
	, TEnumAsByte<ECollisionChannel> CollisionChannel, bool bAppendToList)
{
	FVector start, end;
	if (GetMouseStartEndPoints(TraceDistance, start, end))
	{
		bool bTraceSuccessful = TraceByChannel(start, end
			, CollisionChannel
			, TArray<AActor*>(), bAppendToList);

		//Server
		if (GetLocalRole() == ROLE_Authority)
			ReplicateServerTraceResults(bTraceSuccessful, bAppendToList);
		//Client
		else
		{
			if (!bTraceSuccessful && !bAppendToList)
				ServerDeselectAll(false);

			// If a Local Trace was on a Gizmo, just tell the Server that we 
			// have hit our Gizmo and just change the Domain there.
			// Else, do the Server Trace
			if (CurrentDomain == ETransformationDomain::TD_None)
				ServerTraceByChannel(start, end, CollisionChannel, bAppendToList);
			else
				ServerSetDomain(CurrentDomain);
		}
	}
}

void ATransformerPawn::ReplicatedMouseTraceByProfile(float TraceDistance
	, const FName& ProfileName, bool bAppendToList)
{
	FVector start, end;
	if (GetMouseStartEndPoints(TraceDistance, start, end))
	{
		bool bTraceSuccessful = TraceByProfile(start, end
			, ProfileName
			, TArray<AActor*>(), bAppendToList);

		//Server
		if (GetLocalRole() == ROLE_Authority)
			ReplicateServerTraceResults(bTraceSuccessful, bAppendToList);
		//Client
		else
		{
			if (!bTraceSuccessful && !bAppendToList)
				ServerDeselectAll(false);

			// If a Local Trace was on a Gizmo, just tell the Server that we 
			// have hit our Gizmo and just change the Domain there.
			// Else, do the Server Trace
			if (CurrentDomain == ETransformationDomain::TD_None)
				ServerTraceByProfile(start, end, ProfileName, bAppendToList);
			else
				ServerSetDomain(CurrentDomain);
		}		
	}
}



TArray<AActor*> ATransformerPawn::GetIgnoredActorsForServerTrace() const
{
	TArray<AActor*> ignoredActors;
	//Ignore Gizmo in Server Trace Test if it's not Server controlling Pawn (since Gizmo is relative)
	if (!IsLocallyControlled())
	{
		if (Gizmo.IsValid())
			ignoredActors.Add(Gizmo.Get());
	}
	return ignoredActors;
}

void ATransformerPawn::ReplicateServerTraceResults(bool bTraceSuccessful, bool bAppendToList)
{
	//Only perform this on Clients
	if (!HasAuthority())
	{
		if (!bTraceSuccessful && !bAppendToList)
			DeselectAll(false);
		MulticastSetDomain(CurrentDomain);
		MulticastSetSelectedComponents(SelectedComponents);
	}
}

void ATransformerPawn::LogSelectedComponents()
{

	UE_LOG(LogRuntimeTransformer, Log, TEXT("******************** SELECTED COMPONENTS LOG START ********************"));
    UE_LOG(LogRuntimeTransformer, Log, TEXT("   * Selected Component Count: %d"), SelectedComponents.Num());
    UE_LOG(LogRuntimeTransformer, Log, TEXT("   * -------------------------------- "));
	for (int32 i = 0; i < SelectedComponents.Num(); ++i)
	{
		USceneComponent* cmp = SelectedComponents[i];
		FString message = "Component: ";
		if (cmp)
		{
			message += cmp->GetName() + "\tOwner: ";
			if (AActor* owner = cmp->GetOwner())
				message += owner->GetName();
			else
				message += TEXT("[INVALID]");
		}
		else
			message += TEXT("[INVALID]");

        UE_LOG(LogRuntimeTransformer, Log, TEXT("   * [%d] %s"), i, *message);
	}

    UE_LOG(LogRuntimeTransformer, Log, TEXT("******************** SELECTED COMPONENTS LOG END   ********************"));
}

bool ATransformerPawn::ServerTraceByObjectTypes_Validate(
	const FVector& StartLocation, const FVector& EndLocation
	, const TArray<TEnumAsByte<ECollisionChannel>>& CollisionChannels
	, bool bAppendToList) 
{ 
	return true; 
}

void ATransformerPawn::ServerTraceByObjectTypes_Implementation(
	const FVector& StartLocation, const FVector& EndLocation
	, const TArray<TEnumAsByte<ECollisionChannel>>& CollisionChannels
	, bool bAppendToList)
{
	bool bTraceSuccessful = TraceByObjectTypes(StartLocation, EndLocation, CollisionChannels
		, GetIgnoredActorsForServerTrace(), bAppendToList);

	if (!bTraceSuccessful && !bAppendToList)
		//check whether trace was successful and we're not doing multi selection
		DeselectAll(false);

	MulticastSetDomain(CurrentDomain);
	MulticastSetSelectedComponents(SelectedComponents);
}



bool ATransformerPawn::ServerTraceByChannel_Validate(
	const FVector& StartLocation, const FVector& EndLocation
	, ECollisionChannel TraceChannel, bool bAppendToList)
{
	return true;
}

void ATransformerPawn::ServerTraceByChannel_Implementation(
	const FVector& StartLocation, const FVector& EndLocation
	, ECollisionChannel TraceChannel, bool bAppendToList)
{
	bool bTraceSuccessful = TraceByChannel(StartLocation, EndLocation, TraceChannel
		, GetIgnoredActorsForServerTrace(), bAppendToList);

	if (!bTraceSuccessful && !bAppendToList)
		//check whether trace was successful and we're not doing multi selection
		DeselectAll(false);

	MulticastSetDomain(CurrentDomain);
	MulticastSetSelectedComponents(SelectedComponents);
}



bool ATransformerPawn::ServerTraceByProfile_Validate(const FVector& StartLocation
	, const FVector& EndLocation, const FName& ProfileName, bool bAppendToList) 
{ 
	return true; 
}


void ATransformerPawn::ServerTraceByProfile_Implementation(
	const FVector& StartLocation, const FVector& EndLocation
	, const FName& ProfileName, bool bAppendToList)
{
	bool bTraceSuccessful = TraceByProfile(StartLocation, EndLocation, ProfileName
		, GetIgnoredActorsForServerTrace(), bAppendToList);

	if (!bTraceSuccessful && !bAppendToList) 
		//check whether trace was successful and we're not doing multi selection
		DeselectAll(false); 

	MulticastSetDomain(CurrentDomain);
	MulticastSetSelectedComponents(SelectedComponents);
}

bool ATransformerPawn::ServerClearDomain_Validate() 
{ 
	return true; 
}
void ATransformerPawn::ServerClearDomain_Implementation()
{
	MulticastClearDomain();
}

void ATransformerPawn::MulticastClearDomain_Implementation()
{
	ClearDomain();
}

bool ATransformerPawn::ServerApplyTransform_Validate(const FTransform& DeltaTransform)
{
	return true;
}
void ATransformerPawn::ServerApplyTransform_Implementation(const FTransform& DeltaTransform)
{
	MulticastApplyTransform(DeltaTransform);
}

void ATransformerPawn::MulticastApplyTransform_Implementation(const FTransform& DeltaTransform)
{
	if (Controller && !Controller->IsLocalController()) //only apply to others
		ApplyDeltaTransform(DeltaTransform);
}


void ATransformerPawn::ReplicateFinishTransform()
{
	ServerClearDomain();
	ServerApplyTransform(NetworkDeltaTransform);
	ResetDeltaTransform(NetworkDeltaTransform);
}

bool ATransformerPawn::ServerDeselectAll_Validate(bool bDestroySelected) 
{ 
	return true; 
}
void ATransformerPawn::ServerDeselectAll_Implementation(bool bDestroySelected)
{
	MulticastDeselectAll(bDestroySelected);
}

void ATransformerPawn::MulticastDeselectAll_Implementation(bool bDestroySelected)
{
	DeselectAll(bDestroySelected);
}

bool ATransformerPawn::ServerSetSpaceType_Validate(ESpaceType Space) 
{ 
	return true; 
}
void ATransformerPawn::ServerSetSpaceType_Implementation(ESpaceType Space)
{
	MulticastSetSpaceType(Space);
}

void ATransformerPawn::MulticastSetSpaceType_Implementation(ESpaceType Space)
{
	SetSpaceType(Space);
}


bool ATransformerPawn::ServerSetTransformationType_Validate(ETransformationType Transformation) 
{ 
	return true; 
}
void ATransformerPawn::ServerSetTransformationType_Implementation(ETransformationType Transformation)
{
	MulticastSetTransformationType(Transformation);
}

void ATransformerPawn::MulticastSetTransformationType_Implementation(ETransformationType Transformation)
{
	SetTransformationType(Transformation);
}

bool ATransformerPawn::ServerSetComponentBased_Validate(bool bIsComponentBased) 
{
	return true; 
}
void ATransformerPawn::ServerSetComponentBased_Implementation(bool bIsComponentBased)
{
	MulticastSetComponentBased(bIsComponentBased);
}

void ATransformerPawn::MulticastSetComponentBased_Implementation(bool bIsComponentBased)
{
	SetComponentBased(bIsComponentBased);
}

bool ATransformerPawn::ServerSetRotateOnLocalAxis_Validate(bool bRotateLocalAxis) 
{ 
	return true; 
}
void ATransformerPawn::ServerSetRotateOnLocalAxis_Implementation(bool bRotateLocalAxis)
{
	MulticastSetRotateOnLocalAxis(bRotateLocalAxis);
}

void ATransformerPawn::MulticastSetRotateOnLocalAxis_Implementation(bool bRotateLocalAxis)
{
	SetRotateOnLocalAxis(bRotateLocalAxis);
}

#include "TimerManager.h"
bool ATransformerPawn::ServerCloneSelected_Validate(bool bSelectNewClones, bool bAppendToList)
{
	return true;
}
void ATransformerPawn::ServerCloneSelected_Implementation(bool bSelectNewClones
	, bool bAppendToList)
{
	if (bComponentBased)
	{
		UE_LOG(LogRuntimeTransformer, Warning, TEXT("** Component Cloning is currently not supported in a Network Environment :( **"));
		// see PluginLimitations.txt for a reason why Component Cloning is not supported
		return;
	}

	auto ComponentListCopy = GetSelectedComponents();

	//just create 'em, not select 'em (we select 'em later)
	auto CloneList = CloneFromList(ComponentListCopy);

	//if we have to select the new clones, multicast for these new objects
	if (bSelectNewClones)
	{
		SelectMultipleComponents(CloneList, bAppendToList);
		UnreplicatedComponentClones = CloneList;

		//Timer to loop until all Unreplicated Actors have finished replicating!
		if (UWorld* world = GetWorld())
		{
			if (!CheckUnrepTimerHandle.IsValid())
				world->GetTimerManager().SetTimer(CheckUnrepTimerHandle, this
					, &ATransformerPawn::CheckUnreplicatedActors
					, CloneReplicationCheckFrequency, true, 0.0f);
		}
	}
}

void ATransformerPawn::CheckUnreplicatedActors()
{

	int32 RemoveCount = 0;
	float timeElapsed = GetWorldTimerManager().GetTimerElapsed(CheckUnrepTimerHandle);
	for (auto& c : UnreplicatedComponentClones)
	{
		// rather than calling "IsSupportedForNetworking" (which returns true all the time)
		// call HasActorBegunPlay (which means we are sure the BeginPlay for AActor has finished completely.
		// and so we can safely send this reference over the network.
		if (c && c->HasBegunPlay() && c->IsSupportedForNetworking() &&
			timeElapsed > MinimumCloneReplicationTime) // Make sure there's a minimum time...
			++RemoveCount;
	}

	// Remove with performance (Swapping). We don't care about the order in this case
	// since the role of this is to check that all unreplicated components have been replicated.
	UnreplicatedComponentClones.RemoveAtSwap(0, RemoveCount);

	// check if all clones have been replicated
	if (UnreplicatedComponentClones.Num() == 0)
	{
		//stop calling this if no more unreplicated actors
		GetWorldTimerManager().ClearTimer(CheckUnrepTimerHandle);

		UE_LOG(LogRuntimeTransformer, Log, TEXT("[SERVER] Time Elapsed for %d Replicated Actors to replicate: %f")
			, SelectedComponents.Num(), timeElapsed);

		//send all the selected replicated actors!
		MulticastSetSelectedComponents(SelectedComponents);

	}
}

bool ATransformerPawn::ServerSetDomain_Validate(ETransformationDomain Domain)
{
	return true;
}
void ATransformerPawn::ServerSetDomain_Implementation(ETransformationDomain Domain)
{
	MulticastSetDomain(Domain);
}

void ATransformerPawn::MulticastSetDomain_Implementation(ETransformationDomain Domain)
{
	SetDomain(Domain);
}

bool ATransformerPawn::ServerSyncSelectedComponents_Validate()
{
	return true;
}
void ATransformerPawn::ServerSyncSelectedComponents_Implementation()
{
	MulticastSetSelectedComponents(SelectedComponents);
}

void ATransformerPawn::MulticastSetSelectedComponents_Implementation(
	const TArray<USceneComponent*>& Components)
{
	if (GetLocalRole() < ROLE_Authority)
    {
        UE_LOG(LogRuntimeTransformer, Log, TEXT("MulticastSelect ComponentCount: %d"), Components.Num());
    }
		

	DeselectAll(); //calling here because Selecting MultipleComponents empty is not going to call Deselect all
	SelectMultipleComponents(Components, true);

	//Tells whether we have Selected the exact number of components that came in 
	// or there was a nullptr in Components and therefore there is a difference.
	// if there is a difference we will need to resync
	bResyncSelection = (Components.Num() != SelectedComponents.Num());
	if (bResyncSelection)
	{
		//Timer to loop until all Unreplicated Actors have finished replicating!
		if (UWorld* world = GetWorld())
		{
			if (!ResyncSelectionTimerHandle.IsValid())
				world->GetTimerManager().SetTimer(ResyncSelectionTimerHandle, this
					, &ATransformerPawn::ResyncSelection
					, 0.1f, true, 0.0f);
		}
	}
	

	if (GetLocalRole() < ROLE_Authority)
    {
        UE_LOG(LogRuntimeTransformer, Log, TEXT("Selected ComponentCount: %d"), SelectedComponents.Num());
    }
		
}

void ATransformerPawn::ResyncSelection()
{
	if (bResyncSelection)
	{
        UE_LOG(LogRuntimeTransformer,Warning, TEXT("Resyncing Selection"));
		ServerSyncSelectedComponents();
	}
	else
	{
        UE_LOG(LogRuntimeTransformer, Warning, TEXT("Resyncing FINISHED"));
		//stop calling this if no more resync is needed
		GetWorldTimerManager().ClearTimer(ResyncSelectionTimerHandle);
	}
}

#undef RTT_LOG
