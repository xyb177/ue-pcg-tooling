#include "Data/PCGEllipseData.h"

#include "PCGContext.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGHelpers.h"
#include "Serialization/ArchiveCrc32.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGEllipseData)

namespace
{
	static FTransform GetTransformWithoutScale(const FTransform& InTransform)
	{
		return FTransform(InTransform.GetRotation(), InTransform.GetLocation(), FVector::OneVector);
	}

	// Ellipse position in local space: X = A·cos(θ), Y = B·sin(θ), rotated
	static FVector ComputePositionLocal(float A, float B, float Angle, float RotRadians)
	{
		const float CosA = FMath::Cos(Angle);
		const float SinA = FMath::Sin(Angle);
		const float X = A * CosA;
		const float Y = B * SinA;
		const float CosR = FMath::Cos(RotRadians);
		const float SinR = FMath::Sin(RotRadians);
		return FVector(X * CosR - Y * SinR, X * SinR + Y * CosR, 0.0f);
	}
}

UPCGEllipseData::UPCGEllipseData(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UPCGEllipseData::Initialize(const UEllipseComponent* InComponent)
{
	check(InComponent);
	Initialize(
		GetTransformWithoutScale(InComponent->GetComponentTransform()),
		InComponent->SemiMajorAxis,
		InComponent->SemiMinorAxis,
		InComponent->RotationAngle,
		InComponent->FullAngle,
		InComponent->GetNumSegments(),
		InComponent->IsClosedLoop());
}

void UPCGEllipseData::Initialize(const FTransform& InTransform, float InSemiMajorAxis, float InSemiMinorAxis, float InRotationAngle, float InFullAngle, int32 InResolution, bool bInClosedLoop)
{
	Transform = GetTransformWithoutScale(InTransform);
	SemiMajorAxis = FMath::Max(InSemiMajorAxis, UE_KINDA_SMALL_NUMBER);
	SemiMinorAxis = FMath::Clamp(InSemiMinorAxis, UE_KINDA_SMALL_NUMBER, SemiMajorAxis);
	RotationAngle = InRotationAngle;
	FullAngle = FMath::Clamp(InFullAngle, UE_KINDA_SMALL_NUMBER, FullCircleAngle);
	Resolution = FMath::Max(InResolution, 3);
	bClosedLoop = bInClosedLoop && FMath::IsNearlyEqual(FullAngle, FullCircleAngle);
}

void UPCGEllipseData::AddToCrc(FArchiveCrc32& Ar, bool bFullDataCrc) const
{
	Super::AddToCrc(Ar, bFullDataCrc);
	FString ClassName = StaticClass()->GetPathName();
	Ar << ClassName;
	Ar << Transform;
	Ar << SemiMajorAxis;
	Ar << SemiMinorAxis;
	Ar << RotationAngle;
	Ar << FullAngle;
	Ar << Resolution;
	Ar << bClosedLoop;
}

FBox UPCGEllipseData::GetBounds() const
{
	const float MaxR = FMath::Max(SemiMajorAxis, SemiMinorAxis);
	return FBox(Transform.GetLocation() - FVector(MaxR), Transform.GetLocation() + FVector(MaxR));
}

FVector UPCGEllipseData::ComputePositionLocal(float Angle) const
{
	const float SafeAngle = ClampAngle(Angle);
	const float RotRad = FMath::DegreesToRadians(RotationAngle);
	return ::ComputePositionLocal(SemiMajorAxis, SemiMinorAxis, SafeAngle, RotRad);
}

float UPCGEllipseData::ClampAngle(float InAngle) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	if (bClosedLoop)
	{
		float Wrapped = FMath::Fmod(InAngle, SafeFullAngle);
		if (Wrapped < 0.0f) { Wrapped += SafeFullAngle; }
		return Wrapped;
	}
	return FMath::Clamp(InAngle, 0.0f, SafeFullAngle);
}

FTransform UPCGEllipseData::MakeTransformAtAngle(float InAngle, bool bWorldSpace) const
{
	const float SafeAngle = ClampAngle(InAngle);
	const float RotRad = FMath::DegreesToRadians(RotationAngle);

	const float CosA = FMath::Cos(SafeAngle);
	const float SinA = FMath::Sin(SafeAngle);
	const float CosR = FMath::Cos(RotRad);
	const float SinR = FMath::Sin(RotRad);

	const float X = SemiMajorAxis * CosA;
	const float Y = SemiMinorAxis * SinA;
	const FVector LocalPos(X * CosR - Y * SinR, X * SinR + Y * CosR, 0.0f);

	const float Tx = -SemiMajorAxis * SinA;
	const float Ty =  SemiMinorAxis * CosA;
	const FVector LocalTan(Tx * CosR - Ty * SinR, Tx * SinR + Ty * CosR, 0.0f);

	FTransform OutTransform;
	OutTransform.SetLocation(LocalPos);
	OutTransform.SetRotation(FRotationMatrix::MakeFromXZ(LocalTan.GetSafeNormal(), FVector::ZAxisVector).ToQuat());
	OutTransform.SetScale3D(FVector::OneVector);

	if (bWorldSpace) { OutTransform = OutTransform * Transform; }
	return OutTransform;
}

bool UPCGEllipseData::SamplePoint(const FTransform& InTransform, const FBox& InBounds, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	const FVector WorldPos = InTransform.GetLocation();
	FVector LocalPos = GetTransformWithoutScale(Transform).InverseTransformPosition(WorldPos);

	// Un-rotate by the ellipse's rotation angle so Atan2 works in canonical coordinates
	const float RotRad = FMath::DegreesToRadians(RotationAngle);
	if (!FMath::IsNearlyZero(RotRad))
	{
		const float CosR = FMath::Cos(RotRad);
		const float SinR = FMath::Sin(RotRad);
		const float X =  LocalPos.X * CosR + LocalPos.Y * SinR;
		const float Y = -LocalPos.X * SinR + LocalPos.Y * CosR;
		LocalPos = FVector(X, Y, 0.0f);
	}

	const float Angle = FMath::Atan2(LocalPos.Y / FMath::Max(SemiMinorAxis, UE_KINDA_SMALL_NUMBER),
	                                 LocalPos.X / FMath::Max(SemiMajorAxis, UE_KINDA_SMALL_NUMBER));
	const FVector Closest = ComputePositionLocal(Angle);
	const float Dist = (LocalPos - Closest).Size();

	if (Dist > 1.0f) { return false; }

	OutPoint.Transform = MakeTransformAtAngle(Angle, true);
	OutPoint.Density = 1.0f - Dist;
	OutPoint.SetLocalBounds(InBounds);
	return true;
}

bool UPCGEllipseData::ProjectPoint(const FTransform& InTransform, const FBox& InBounds, const FPCGProjectionParams& InParams, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	const FVector WorldPos = InTransform.GetLocation();
	FVector LocalPos = GetTransformWithoutScale(Transform).InverseTransformPosition(WorldPos);

	const float RotRad = FMath::DegreesToRadians(RotationAngle);
	if (!FMath::IsNearlyZero(RotRad))
	{
		const float CosR = FMath::Cos(RotRad);
		const float SinR = FMath::Sin(RotRad);
		const float X =  LocalPos.X * CosR + LocalPos.Y * SinR;
		const float Y = -LocalPos.X * SinR + LocalPos.Y * CosR;
		LocalPos = FVector(X, Y, 0.0f);
	}

	const float Angle = FMath::Atan2(LocalPos.Y / FMath::Max(SemiMinorAxis, UE_KINDA_SMALL_NUMBER),
	                                 LocalPos.X / FMath::Max(SemiMajorAxis, UE_KINDA_SMALL_NUMBER));
	const FVector Closest = ComputePositionLocal(Angle);
	const float Dist = (LocalPos - Closest).Size();

	if (Dist > 1.0f && InParams.bProjectPositions) { return false; }

	OutPoint.Transform = MakeTransformAtAngle(Angle, true);
	OutPoint.Density = 1.0f - FMath::Clamp(Dist, 0.0f, 1.0f);
	OutPoint.SetLocalBounds(InBounds);
	return true;
}

UPCGSpatialData* UPCGEllipseData::CopyInternal(FPCGContext* Context) const
{
	UPCGEllipseData* NewData = FPCGContext::NewObject_AnyThread<UPCGEllipseData>(Context);
	NewData->InitializeFromData(this);
	NewData->Transform = Transform;
	NewData->SemiMajorAxis = SemiMajorAxis;
	NewData->SemiMinorAxis = SemiMinorAxis;
	NewData->RotationAngle = RotationAngle;
	NewData->FullAngle = FullAngle;
	NewData->Resolution = Resolution;
	NewData->bClosedLoop = bClosedLoop;
	return NewData;
}

const UPCGPointData* UPCGEllipseData::CreatePointData(FPCGContext* Context) const
{
	UPCGPointData* PointData = FPCGContext::NewObject_AnyThread<UPCGPointData>(Context);
	PointData->InitializeFromData(this);

	const int32 NumPoints = bClosedLoop ? Resolution : Resolution + 1;
	PointData->SetNumPoints(NumPoints);
	TArray<FPCGPoint>& Points = PointData->GetMutablePoints();

	for (int32 i = 0; i < NumPoints; ++i)
	{
		const float Alpha = static_cast<float>(i) / static_cast<float>(Resolution);
		Points[i].Transform = GetTransformAtAlpha(Alpha);
		Points[i].Density = 1.0f;
		Points[i].SetLocalBounds(FBox(EForceInit::ForceInit));
	}
	return PointData;
}

FTransform UPCGEllipseData::GetTransform() const { return Transform; }
int UPCGEllipseData::GetNumSegments() const { return Resolution; }

FVector::FReal UPCGEllipseData::GetSegmentLength(int SegmentIndex) const
{
	if (SegmentIndex < 0 || SegmentIndex >= Resolution) { return 0.0; }
	const float Alpha0 = static_cast<float>(SegmentIndex) / static_cast<float>(Resolution);
	const float Alpha1 = static_cast<float>(SegmentIndex + 1) / static_cast<float>(Resolution);
	return static_cast<double>(FVector::Dist(ComputePositionLocal(Alpha0 * FullAngle), ComputePositionLocal(Alpha1 * FullAngle)));
}

FVector UPCGEllipseData::GetLocationAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace) const
{
	return GetTransformAtDistance(SegmentIndex, Distance, bWorldSpace).GetLocation();
}

FTransform UPCGEllipseData::GetTransformAtDistance(int SegmentIndex, FVector::FReal Distance, bool bWorldSpace, FBox* OutBounds) const
{
	if (OutBounds) { *OutBounds = FBox::BuildAABB(FVector::ZeroVector, FVector::OneVector); }
	const float SegLen = static_cast<float>(GetSegmentLength(SegmentIndex));
	const float Alpha = SegLen > UE_KINDA_SMALL_NUMBER
		? (static_cast<float>(SegmentIndex) + static_cast<float>(Distance / SegLen)) / static_cast<float>(Resolution)
		: static_cast<float>(SegmentIndex) / static_cast<float>(Resolution);
	return MakeTransformAtAngle(Alpha * FullAngle, bWorldSpace);
}

FVector::FReal UPCGEllipseData::GetCurvatureAtDistance(int SegmentIndex, FVector::FReal Distance) const
{
	// Approximate: curvature at ellipse vertex = b/a² for major, a/b² for minor
	const float A = FMath::Max(SemiMajorAxis, UE_KINDA_SMALL_NUMBER);
	const float B = FMath::Max(SemiMinorAxis, UE_KINDA_SMALL_NUMBER);
	return static_cast<double>(FMath::Max(B / (A * A), A / (B * B)));
}

float UPCGEllipseData::GetInputKeyAtDistance(int SegmentIndex, FVector::FReal Distance) const
{
	const float SegLen = static_cast<float>(GetSegmentLength(SegmentIndex));
	if (SegLen <= UE_KINDA_SMALL_NUMBER) { return 0.0f; }
	return static_cast<float>(SegmentIndex) + static_cast<float>(Distance / SegLen);
}

float UPCGEllipseData::GetInputKeyAtAlpha(float Alpha) const
{
	return static_cast<float>(Resolution) * FMath::Clamp(Alpha, 0.0f, 1.0f);
}

void UPCGEllipseData::GetTangentsAtSegmentStart(int SegmentIndex, FVector& OutArriveTangent, FVector& OutLeaveTangent) const
{
	const float Alpha = Resolution > 0 ? static_cast<float>(SegmentIndex) / static_cast<float>(Resolution) : 0.0f;
	const FVector Tangent = GetTransformAtAlpha(Alpha).GetUnitAxis(EAxis::X);
	OutArriveTangent = Tangent;
	OutLeaveTangent = Tangent;
}

FVector::FReal UPCGEllipseData::GetDistanceAtSegmentStart(int SegmentIndex) const
{
	// Accumulate segment lengths up to SegmentIndex
	double Total = 0.0;
	for (int32 i = 0; i < SegmentIndex && i < Resolution; ++i)
	{
		Total += GetSegmentLength(i);
	}
	return Total;
}

FVector UPCGEllipseData::GetLocationAtAlpha(float Alpha) const
{
	return MakeTransformAtAngle(FMath::Clamp(Alpha, 0.0f, 1.0f) * FullAngle, true).GetLocation();
}

FTransform UPCGEllipseData::GetTransformAtAlpha(float Alpha) const
{
	return MakeTransformAtAngle(FMath::Clamp(Alpha, 0.0f, 1.0f) * FullAngle, true);
}
