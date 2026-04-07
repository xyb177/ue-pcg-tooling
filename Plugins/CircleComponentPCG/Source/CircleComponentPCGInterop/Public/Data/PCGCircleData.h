// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Data/PCGPolyLineData.h"
#include "Components/CircleComponent.h"

#include "PCGCircleData.generated.h"

#define UE_API CIRCLECOMPONENTPCGINTEROP_API

class UPCGPointData;

USTRUCT()
struct FPCGDataTypeInfoCircle : public FPCGDataTypeInfoPolyline
{
	GENERATED_BODY()

	PCG_DECLARE_LEGACY_TYPE_INFO(EPCGDataType::PolyLine)
};

UCLASS(MinimalAPI, BlueprintType, ClassGroup = (Procedural))
class CIRCLECOMPONENTPCGINTEROP_API UPCGCircleData : public UPCGPolyLineData
{
	GENERATED_BODY()

public:
	PCG_API UPCGCircleData(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category = "CircleData", meta = (DisplayName = "Initialize From Circle Component"))
	PCG_API void Initialize(const UCircleComponent* InCircleComponent);

	UFUNCTION(BlueprintCallable, Category = "CircleData", meta = (DisplayName = "Initialize From Circle Parameters"))
	PCG_API void Initialize(const FTransform& InTransform, float InRadius, float InFullAngle, int32 InResolution, bool bInClosedLoop);

	// ~Begin UPCGData interface
	PCG_ASSIGN_DEFAULT_TYPE_INFO(FPCGDataTypeInfoCircle)
	PCG_API virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	// ~End UPCGData interface

	// ~Begin UPCGSpatialData interface
	virtual int GetDimension() const override { return 1; }
	PCG_API virtual FBox GetBounds() const override;
	virtual bool HasNonTrivialTransform() const override { return true; }
	PCG_API virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	PCG_API virtual bool ProjectPoint(const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	PCG_API virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
	// ~End UPCGSpatialData interface

	// ~Begin UPCGSpatialDataWithPointCache interface
	PCG_API virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override;
	// ~End UPCGSpatialDataWithPointCache interface

	// ~Begin UPCGPolyLineData interface
	PCG_API virtual FTransform GetTransform() const override;
	PCG_API virtual int GetNumSegments() const override;
	PCG_API virtual FVector::FReal GetSegmentLength(int SegmentIndex) const override;
	PCG_API virtual FVector GetLocationAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace = true) const override;
	PCG_API virtual FTransform GetTransformAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace = true, FBox* OutBounds = nullptr) const override;
	PCG_API virtual FVector::FReal GetCurvatureAtDistance(int SegmentIndex, FVector::FReal Distance) const override;
	PCG_API virtual float GetInputKeyAtDistance(int SegmentIndex, FVector::FReal Distance) const override;
	PCG_API virtual float GetInputKeyAtAlpha(float Alpha) const override;
	PCG_API virtual void GetTangentsAtSegmentStart(int SegmentIndex, FVector& OutArriveTangent, FVector& OutLeaveTangent) const override;
	PCG_API virtual FVector::FReal GetDistanceAtSegmentStart(int SegmentIndex) const override;
	PCG_API virtual FVector GetLocationAtAlpha(float Alpha) const override;
	PCG_API virtual FTransform GetTransformAtAlpha(float Alpha) const override;
	virtual bool IsClosed() const override { return bClosedLoop; }
	// ~End UPCGPolyLineData interface

	UFUNCTION(BlueprintCallable, Category = "CircleData")
	PCG_API float GetRadius() const { return Radius; }

	UFUNCTION(BlueprintCallable, Category = "CircleData")
	PCG_API float GetFullAngle() const { return FullAngle; }

	UFUNCTION(BlueprintCallable, Category = "CircleData")
	PCG_API bool IsClosedLoop() const { return bClosedLoop; }

protected:
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	float Radius = 100.0f;

	UPROPERTY()
	float FullAngle = 2.0f * UE_PI;

	UPROPERTY()
	int32 Resolution = 64;

	UPROPERTY()
	bool bClosedLoop = true;

private:
	PCG_API bool ProjectToCircle(const FVector& InWorldPosition, FVector& OutProjectedWorldPosition, float& OutAlpha, float& OutDistance) const;
	PCG_API FTransform MakeTransformAtAngle(float InAngle, bool bWorldSpace) const;
	PCG_API FVector GetWorldCenter() const;
	PCG_API FVector GetLocalXAxis() const;
	PCG_API FVector GetLocalYAxis() const;
	PCG_API FVector GetLocalNormal() const;
};

#undef UE_API
