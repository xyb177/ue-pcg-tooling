// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGCircleData.h"

#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGCircleHelpers.h"
#include "Helpers/PCGHelpers.h"
#include "Serialization/ArchiveCrc32.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGCircleData)

PCG_DEFINE_TYPE_INFO(FPCGDataTypeInfoCircle, UPCGCircleData)

UPCGCircleData::UPCGCircleData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPCGCircleData::Initialize(const UCircleComponent* InCircleComponent)
{
	check(InCircleComponent);

	Initialize(
		PCGCircleHelpers::GetTransformWithoutScale(InCircleComponent->GetComponentTransform()),
		FMath::Max(InCircleComponent->GetRadius(), UE_KINDA_SMALL_NUMBER),
		FMath::Clamp(InCircleComponent->FullAngle, UE_KINDA_SMALL_NUMBER, PCGCircleHelpers::FullCircleAngle),
		FMath::Max(InCircleComponent->GetNumberOfCircleSegments(), 3),
		InCircleComponent->IsClosedLoop());
}

void UPCGCircleData::Initialize(const FTransform& InTransform, float InRadius, float InFullAngle, int32 InResolution, bool bInClosedLoop)
{
	Transform = PCGCircleHelpers::GetTransformWithoutScale(InTransform);
	Radius = FMath::Max(InRadius, UE_KINDA_SMALL_NUMBER);
	FullAngle = FMath::Clamp(InFullAngle, UE_KINDA_SMALL_NUMBER, PCGCircleHelpers::FullCircleAngle);
	Resolution = FMath::Max(InResolution, 3);
	bClosedLoop = bInClosedLoop && FMath::IsNearlyEqual(FullAngle, PCGCircleHelpers::FullCircleAngle);
}

void UPCGCircleData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);

	FString ClassName = StaticClass()->GetPathName();
	Ar << ClassName;
	Ar << Transform;
	Ar << Radius;
	Ar << FullAngle;
	Ar << Resolution;
	Ar << bClosedLoop;
}

FBox UPCGCircleData::GetBounds() const
{
	const FVector Center = GetWorldCenter();
	return FBox(Center - FVector(Radius), Center + FVector(Radius));
}

bool UPCGCircleData::ProjectToCircle(const FVector& InWorldPosition, FVector& OutProjectedWorldPosition, float& OutAlpha, float& OutDistance) const
{
	const FVector LocalPosition = PCGCircleHelpers::GetTransformWithoutScale(Transform).InverseTransformPosition(InWorldPosition);
	const FVector LocalXY(LocalPosition.X, LocalPosition.Y, 0.0f);
	const float Angle = FMath::Atan2(LocalXY.Y, LocalXY.X);
	const float SafeAngle = bClosedLoop ? PCGCircleHelpers::NormalizeAngle(Angle, FullAngle) : FMath::Clamp(Angle, 0.0f, FullAngle);
	const float Alpha = SafeAngle / FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const FVector LocalProjected(FMath::Cos(SafeAngle) * Radius, FMath::Sin(SafeAngle) * Radius, 0.0f);

	OutProjectedWorldPosition = PCGCircleHelpers::GetTransformWithoutScale(Transform).TransformPosition(LocalProjected);
	OutAlpha = Alpha;
	OutDistance = (LocalPosition - LocalProjected).Size();
	return true;
}

bool UPCGCircleData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	FVector ProjectedWorldPosition = FVector::ZeroVector;
	float Alpha = 0.0f;
	float Distance = 0.0f;
	ProjectToCircle(InTransform.GetLocation(), ProjectedWorldPosition, Alpha, Distance);

	if (Distance > 1.0f)
	{
		return false;
	}

	OutPoint.Transform = MakeTransformAtAngle(Alpha * FullAngle, /*bWorldSpace=*/true);
	OutPoint.Transform.SetLocation(ProjectedWorldPosition);
	OutPoint.SetLocalBounds(InBounds);
	OutPoint.Density = 1.0f - Distance;

	return true;
}

bool UPCGCircleData::ProjectPoint(const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	FVector ProjectedWorldPosition = FVector::ZeroVector;
	float Alpha = 0.0f;
	float Distance = 0.0f;
	ProjectToCircle(InTransform.GetLocation(), ProjectedWorldPosition, Alpha, Distance);

	if (Distance > 1.0f && InParams.bProjectPositions)
	{
		return false;
	}

	OutPoint.Transform = MakeTransformAtAngle(Alpha * FullAngle, /*bWorldSpace=*/true);
	OutPoint.Transform.SetLocation(ProjectedWorldPosition);
	OutPoint.SetLocalBounds(InBounds);
	OutPoint.Density = 1.0f - FMath::Clamp(Distance, 0.0f, 1.0f);
	return true;
}

UPCGSpatialData* UPCGCircleData::CopyInternal(FPCGContext* Context) const
{
	UPCGCircleData* NewCircleData = FPCGContext::NewObject_AnyThread<UPCGCircleData>(Context);
	NewCircleData->InitializeFromData(this);
	NewCircleData->Transform = Transform;
	NewCircleData->Radius = Radius;
	NewCircleData->FullAngle = FullAngle;
	NewCircleData->Resolution = Resolution;
	NewCircleData->bClosedLoop = bClosedLoop;
	return NewCircleData;
}

const UPCGPointData* UPCGCircleData::CreatePointData(FPCGContext* Context) const
{
	UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
	PointData->InitializeFromData(this);

	const int32 NumPoints = bClosedLoop ? Resolution : Resolution + 1;
	PointData->SetNumPoints(NumPoints);

	TArray<FPCGPoint>& Points = PointData->GetMutablePoints();
	for (int32 PointIndex = 0; PointIndex < NumPoints; ++PointIndex)
	{
		// Same denominator for both paths; the point count (NumPoints) already differs between closed/open
			const float Alpha = static_cast<float>(PointIndex) / static_cast<float>(Resolution);
		Points[PointIndex].Transform = GetTransformAtAlpha(Alpha);
		Points[PointIndex].Density = 1.0f;
		Points[PointIndex].SetLocalBounds(FBox(EForceInit::ForceInit));
	}

	return PointData;
}

FTransform UPCGCircleData::GetTransform() const
{
	return Transform;
}

int UPCGCircleData::GetNumSegments() const
{
	return Resolution;
}

FVector::FReal UPCGCircleData::GetSegmentLength(int SegmentIndex) const
{
	if (SegmentIndex < 0 || SegmentIndex >= Resolution)
	{
		return 0.0;
	}

	return static_cast<double>(Radius) * static_cast<double>(FullAngle) / static_cast<double>(Resolution);
}

FVector UPCGCircleData::GetLocationAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace) const
{
	return GetTransformAtDistance(SegmentIndex, Distance, bWorldSpace).GetLocation();
}

FTransform UPCGCircleData::GetTransformAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace, FBox* OutBounds) const
{
	if (OutBounds)
	{
		*OutBounds = FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector);
	}

	const float Alpha = GetAlphaAtDistance(SegmentIndex, Distance);
	const FTransform LocalTransform = MakeTransformAtAngle(Alpha * FullAngle, /*bWorldSpace=*/false);
	return bWorldSpace ? LocalTransform * Transform : LocalTransform;
}

FVector::FReal UPCGCircleData::GetCurvatureAtDistance(int SegmentIndex, FVector::FReal Distance) const
{
	return Radius > UE_KINDA_SMALL_NUMBER ? (1.0 / Radius) : 0.0;
}

float UPCGCircleData::GetInputKeyAtDistance(int SegmentIndex, FVector::FReal Distance) const
{
	const float SegmentLength = static_cast<float>(GetSegmentLength(SegmentIndex));
	if (SegmentLength <= UE_KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	return static_cast<float>(SegmentIndex) + static_cast<float>(Distance / SegmentLength);
}

float UPCGCircleData::GetInputKeyAtAlpha(float Alpha) const
{
	return static_cast<float>(Resolution) * FMath::Clamp(Alpha, 0.0f, 1.0f);
}

void UPCGCircleData::GetTangentsAtSegmentStart(int SegmentIndex, FVector& OutArriveTangent, FVector& OutLeaveTangent) const
{
	const float Alpha = Resolution > 0 ? static_cast<float>(SegmentIndex) / static_cast<float>(Resolution) : 0.0f;
	const FVector Tangent = GetTransformAtAlpha(Alpha).GetUnitAxis(EAxis::X);
	OutArriveTangent = Tangent;
	OutLeaveTangent = Tangent;
}

FVector::FReal UPCGCircleData::GetDistanceAtSegmentStart(int SegmentIndex) const
{
	return static_cast<double>(SegmentIndex) * GetSegmentLength(0);
}

FVector UPCGCircleData::GetLocationAtAlpha(float Alpha) const
{
	return MakeTransformAtAngle(FMath::Clamp(Alpha, 0.0f, 1.0f) * FullAngle, true).GetLocation();
}

FTransform UPCGCircleData::GetTransformAtAlpha(float Alpha) const
{
	return MakeTransformAtAngle(FMath::Clamp(Alpha, 0.0f, 1.0f) * FullAngle, true);
}

FVector UPCGCircleData::GetWorldCenter() const
{
	return Transform.GetLocation();
}

FVector UPCGCircleData::GetLocalXAxis() const
{
	return Transform.GetUnitAxis(EAxis::X);
}

FVector UPCGCircleData::GetLocalYAxis() const
{
	return Transform.GetUnitAxis(EAxis::Y);
}

FVector UPCGCircleData::GetLocalNormal() const
{
	return Transform.GetUnitAxis(EAxis::Z).GetSafeNormal();
}

FTransform UPCGCircleData::MakeTransformAtAngle(float InAngle, bool bWorldSpace) const
{
	const float SafeAngle = bClosedLoop ? PCGCircleHelpers::NormalizeAngle(InAngle, FullAngle) : FMath::Clamp(InAngle, 0.0f, FullAngle);
	const FVector LocalPosition(FMath::Cos(SafeAngle) * Radius, FMath::Sin(SafeAngle) * Radius, 0.0f);
	const FVector LocalTangent(-FMath::Sin(SafeAngle), FMath::Cos(SafeAngle), 0.0f);
	const FVector LocalNormal(0.0f, 0.0f, 1.0f);

	FTransform OutTransform;
	OutTransform.SetLocation(LocalPosition);
	OutTransform.SetRotation(FRotationMatrix::MakeFromXZ(LocalTangent, LocalNormal).ToQuat());
	OutTransform.SetScale3D(FVector::OneVector);

	if (bWorldSpace)
	{
		OutTransform = OutTransform * Transform;
	}

	return OutTransform;
}
