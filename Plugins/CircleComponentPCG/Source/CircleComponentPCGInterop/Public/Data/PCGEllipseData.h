#pragma once

#include "Data/PCGPolyLineData.h"
#include "Components/EllipseComponent.h"

#include "PCGEllipseData.generated.h"

#define UE_API CIRCLECOMPONENTPCGINTEROP_API

UCLASS(MinimalAPI, BlueprintType, ClassGroup=(Procedural))
class CIRCLECOMPONENTPCGINTEROP_API UPCGEllipseData : public UPCGPolyLineData
{
	GENERATED_BODY()

public:
	UPCGEllipseData(const FObjectInitializer& ObjectInitializer);

	/** Initialize from an UEllipseComponent, copying its geometry. */
	UFUNCTION(BlueprintCallable, Category="EllipseData")
	void Initialize(const UEllipseComponent* InComponent);

	/** Initialize from explicit ellipse parameters. */
	UFUNCTION(BlueprintCallable, Category="EllipseData")
	void Initialize(const FTransform& InTransform, float InSemiMajorAxis, float InSemiMinorAxis, float InRotationAngle, float InFullAngle, int32 InResolution, bool bInClosedLoop);

	// ~Begin UPCGData
	virtual void AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const override;
	// ~End UPCGData

	// ~Begin UPCGSpatialData
	virtual int GetDimension() const override { return 1; }
	virtual FBox GetBounds() const override;
	virtual bool SamplePoint(const FTransform& Transform, const FBox& Bounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual bool ProjectPoint(const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const override;
	virtual UPCGSpatialData* CopyInternal(FPCGContext* Context) const override;
	// ~End UPCGSpatialData

	// ~Begin UPCGSpatialDataWithPointCache
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override;
	// ~End UPCGSpatialDataWithPointCache

	// ~Begin UPCGPolyLineData
	virtual FTransform GetTransform() const override;
	virtual int GetNumSegments() const override;
	virtual FVector::FReal GetSegmentLength(int SegmentIndex) const override;
	virtual FVector GetLocationAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace = true) const override;
	virtual FTransform GetTransformAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace = true, FBox* OutBounds = nullptr) const override;
	virtual FVector::FReal GetCurvatureAtDistance(int SegmentIndex, FVector::FReal Distance) const override;
	virtual float GetInputKeyAtDistance(int SegmentIndex, FVector::FReal Distance) const override;
	virtual float GetInputKeyAtAlpha(float Alpha) const override;
	virtual void GetTangentsAtSegmentStart(int SegmentIndex, FVector& OutArriveTangent, FVector& OutLeaveTangent) const override;
	virtual FVector::FReal GetDistanceAtSegmentStart(int SegmentIndex) const override;
	virtual FVector GetLocationAtAlpha(float Alpha) const override;
	virtual FTransform GetTransformAtAlpha(float Alpha) const override;
	virtual bool IsClosed() const override { return bClosedLoop; }
	// ~End UPCGPolyLineData

	UFUNCTION(BlueprintCallable, Category="EllipseData")
	float GetSemiMajorAxis() const { return SemiMajorAxis; }

	UFUNCTION(BlueprintCallable, Category="EllipseData")
	float GetSemiMinorAxis() const { return SemiMinorAxis; }

	UFUNCTION(BlueprintCallable, Category="EllipseData")
	float GetRotationAngle() const { return RotationAngle; }

	UFUNCTION(BlueprintCallable, Category="EllipseData")
	bool IsClosedLoop() const { return bClosedLoop; }

protected:
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	float SemiMajorAxis = 200.0f;

	UPROPERTY()
	float SemiMinorAxis = 100.0f;

	UPROPERTY()
	float RotationAngle = 0.0f;

	UPROPERTY()
	float FullAngle = 2.0f * UE_PI;

	UPROPERTY()
	int32 Resolution = 64;

	UPROPERTY()
	bool bClosedLoop = true;

private:
	static constexpr float FullCircleAngle = 2.0f * UE_PI;

	FVector ComputePositionLocal(float Angle) const;
	FTransform MakeTransformAtAngle(float InAngle, bool bWorldSpace) const;
	float ClampAngle(float InAngle) const;
};

#undef UE_API
