// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGGetCircleData.h"

#include "PCGContext.h"
#include "Data/PCGSplineData.h"
#include "Helpers/PCGCircleHelpers.h"
#include "Helpers/PCGHelpers.h"

#include "Components/CircleComponent.h"
#include "Components/SplineComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGGetCircleData)

#define LOCTEXT_NAMESPACE "PCGGetCircleDataElement"

UPCGGetCircleDataSettings::UPCGGetCircleDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;

	ComponentSelector.ComponentSelection = EPCGComponentSelection::ByClass;
	ComponentSelector.ComponentSelectionClass = UCircleComponent::StaticClass();
	ComponentSelector.bShowComponentSelection = false;
	ComponentSelector.bShowComponentSelectionClass = false;
}

#if WITH_EDITOR
FText UPCGGetCircleDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("GetCircleDataTooltip", "Builds spline data from Circle Components on the selected actors.");
}
#endif

TArray<FPCGPinProperties> UPCGGetCircleDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::PolyLine);
	return PinProperties;
}

FPCGElementPtr UPCGGetCircleDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetCircleInteropElement>();
}

void FPCGGetCircleInteropElement::ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* InSettings, AActor* FoundActor) const
{
	check(Context && InSettings);

	const UPCGGetCircleDataSettings* Settings = CastChecked<UPCGGetCircleDataSettings>(InSettings);
	FPCGDataFromActorContext* DataFromActorContext = static_cast<FPCGDataFromActorContext*>(Context);

	if (!FoundActor || !IsValid(FoundActor))
	{
		return;
	}

	TInlineComponentArray<UCircleComponent*> CircleComponents;
	FoundActor->GetComponents(CircleComponents);

	for (UCircleComponent* CircleComponent : CircleComponents)
	{
		if (!IsValid(CircleComponent) || !DataFromActorContext->ComponentSelector.FilterComponent(CircleComponent))
		{
			continue;
		}

		if (Settings->bIgnorePCGGeneratedComponents && CircleComponent->ComponentTags.Contains(PCGHelpers::DefaultPCGTag))
		{
			continue;
		}

		UPCGSplineData* SplineData = PCGCircleHelpers::CreateSplineDataFromCircleComponent(Context, CircleComponent);
		if (!SplineData)
		{
			continue;
		}

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = SplineData;

		for (const FName& ComponentTag : CircleComponent->ComponentTags)
		{
			TaggedData.Tags.Add(ComponentTag.ToString());
		}

		if (AActor* Owner = CircleComponent->GetOwner())
		{
			for (const FName& ActorTag : Owner->Tags)
			{
				TaggedData.Tags.Add(ActorTag.ToString());
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
