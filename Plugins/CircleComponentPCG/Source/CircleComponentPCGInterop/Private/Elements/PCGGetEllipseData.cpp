#include "Elements/PCGGetEllipseData.h"

#include "PCGContext.h"
#include "Data/PCGEllipseData.h"
#include "Helpers/PCGHelpers.h"
#include "Components/EllipseComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGGetEllipseData)

#define LOCTEXT_NAMESPACE "PCGGetEllipseDataElement"

UPCGGetEllipseDataSettings::UPCGGetEllipseDataSettings()
{
	Mode = EPCGGetDataFromActorMode::ParseActorComponents;
	ComponentSelector.ComponentSelection = EPCGComponentSelection::ByClass;
	ComponentSelector.ComponentSelectionClass = UEllipseComponent::StaticClass();
	ComponentSelector.bShowComponentSelection = false;
	ComponentSelector.bShowComponentSelectionClass = false;
}

#if WITH_EDITOR
FText UPCGGetEllipseDataSettings::GetNodeTooltipText() const
{
	return LOCTEXT("GetEllipseDataTooltip", "Builds ellipse data from Ellipse Components on the selected actors.");
}
#endif

TArray<FPCGPinProperties> UPCGGetEllipseDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::PolyLine);
	return PinProperties;
}

FPCGElementPtr UPCGGetEllipseDataSettings::CreateElement() const
{
	return MakeShared<FPCGGetEllipseInteropElement>();
}

void FPCGGetEllipseInteropElement::ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* InSettings, AActor* FoundActor) const
{
	check(Context && InSettings);

	const UPCGGetEllipseDataSettings* Settings = CastChecked<UPCGGetEllipseDataSettings>(InSettings);
	FPCGDataFromActorContext* DataFromActorContext = static_cast<FPCGDataFromActorContext*>(Context);

	if (!FoundActor || !IsValid(FoundActor)) { return; }

	TInlineComponentArray<UEllipseComponent*> EllipseComponents;
	FoundActor->GetComponents(EllipseComponents);

	for (UEllipseComponent* EllipseComponent : EllipseComponents)
	{
		if (!IsValid(EllipseComponent) || !DataFromActorContext->ComponentSelector.FilterComponent(EllipseComponent)) { continue; }
		if (Settings->bIgnorePCGGeneratedComponents && EllipseComponent->ComponentTags.Contains(PCGHelpers::DefaultPCGTag)) { continue; }

		UPCGEllipseData* EllipseData = FPCGContext::NewObject_AnyThread<UPCGEllipseData>(Context);
		EllipseData->Initialize(EllipseComponent);

		FPCGTaggedData& TaggedData = Context->OutputData.TaggedData.Emplace_GetRef();
		TaggedData.Data = EllipseData;

		for (const FName& Tag : EllipseComponent->ComponentTags) { TaggedData.Tags.Add(Tag.ToString()); }
		if (AActor* Owner = EllipseComponent->GetOwner())
		{
			for (const FName& Tag : Owner->Tags) { TaggedData.Tags.Add(Tag.ToString()); }
		}
	}
}

#undef LOCTEXT_NAMESPACE
