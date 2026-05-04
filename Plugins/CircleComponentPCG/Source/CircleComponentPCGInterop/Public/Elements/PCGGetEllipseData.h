#pragma once

#include "Elements/PCGDataFromActor.h"

#include "PCGGetEllipseData.generated.h"

/** Builds ellipse data from Ellipse Components on the selected actors. */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class CIRCLECOMPONENTPCGINTEROP_API UPCGGetEllipseDataSettings : public UPCGDataFromActorSettings
{
	GENERATED_BODY()

public:
	UPCGGetEllipseDataSettings();

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetEllipseData")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGGetEllipseDataElement", "NodeTitle", "Get Ellipse Data"); }
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;

public:
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::PolyLine; }
};

class FPCGGetEllipseInteropElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const override;
};
