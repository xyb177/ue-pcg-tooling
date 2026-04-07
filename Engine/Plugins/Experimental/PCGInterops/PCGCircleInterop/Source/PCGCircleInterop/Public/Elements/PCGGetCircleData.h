// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/PCGDataFromActor.h"
#include "Elements/PCGTypedGetter.h"

#include "PCGGetCircleData.generated.h"

/** Builds spline data from Circle Components on the selected actors. */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCGCIRCLEINTEROP_API UPCGGetCircleDataSettings : public UPCGGetSplineSettings
{
	GENERATED_BODY()

public:
	UPCGGetCircleDataSettings();

	//~Begin UPCGSettings interface
#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const override { return FName(TEXT("GetCircleData_Interop")); }
	virtual FText GetDefaultNodeTitle() const override { return NSLOCTEXT("PCGGetCircleDataElement", "NodeTitle", "Get Circle Data"); }
	virtual FText GetNodeTooltipText() const override;
#endif

protected:
	virtual TArray<FPCGPinProperties> OutputPinProperties() const override;
	virtual FPCGElementPtr CreateElement() const override;
	//~End UPCGSettings interface

	//~Begin UPCGDataFromActorSettings interface
public:
	virtual EPCGDataType GetDataFilter() const override { return EPCGDataType::PolyLine; }
	//~End UPCGDataFromActorSettings interface
};

class FPCGGetCircleInteropElement : public FPCGDataFromActorElement
{
protected:
	virtual void ProcessActor(FPCGContext* Context, const UPCGDataFromActorSettings* Settings, AActor* FoundActor) const override;
};
