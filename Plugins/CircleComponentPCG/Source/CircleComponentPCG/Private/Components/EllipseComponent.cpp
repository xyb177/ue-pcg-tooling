#include "Components/EllipseComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SceneView.h"
#include "UObject/EditorObjectVersion.h"
#include "Math/RotationMatrix.h"
#include "MeshElementCollector.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveDrawingUtils.h"
#include "DynamicMeshBuilder.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EllipseComponent)

#define LOCTEXT_NAMESPACE "EllipseComponent"

namespace UE::EllipseComponent
{
	static FTransform GetTransformWithoutScale(const FTransform& InTransform)
	{
		return FTransform(InTransform.GetRotation(), InTransform.GetLocation(), FVector::OneVector);
	}

	static FVector ComputePosition(float A, float B, float Angle, float RotRadians)
	{
		const float CosA = FMath::Cos(Angle);
		const float SinA = FMath::Sin(Angle);
		const float X = A * CosA;
		const float Y = B * SinA;
		// Apply rotation of the major axis
		const float CosR = FMath::Cos(RotRadians);
		const float SinR = FMath::Sin(RotRadians);
		return FVector(X * CosR - Y * SinR, X * SinR + Y * CosR, 0.0f);
	}

	static FVector ComputeTangent(float A, float B, float Angle, float RotRadians)
	{
		const float CosA = FMath::Cos(Angle);
		const float SinA = FMath::Sin(Angle);
		const float Tx = -A * SinA;
		const float Ty =  B * CosA;
		const float CosR = FMath::Cos(RotRadians);
		const float SinR = FMath::Sin(RotRadians);
		return FVector(Tx * CosR - Ty * SinR, Tx * SinR + Ty * CosR, 0.0f).GetSafeNormal();
	}

	static FVector ComputeNormal(float A, float B, float Angle, float RotRadians)
	{
		const float CosA = FMath::Cos(Angle);
		const float SinA = FMath::Sin(Angle);
		const float Nx = B * CosA;
		const float Ny = A * SinA;
		const float CosR = FMath::Cos(RotRadians);
		const float SinR = FMath::Sin(RotRadians);
		return FVector(Nx * CosR - Ny * SinR, Nx * SinR + Ny * CosR, 0.0f).GetSafeNormal();
	}
}

UEllipseComponent::UEllipseComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, SemiMajorAxis(200.0f)
	, SemiMinorAxis(100.0f)
	, RotationAngle(0.0f)
	, Segments(64)
	, FullAngle(2.0f * UE_PI)
	, Thickness(5.0f)
	, ShapeType(EEllipseShapeType::Wire)
	, bDrawDebug(true)
{
	bWantsOnUpdateTransform = true;
	SetUsingAbsoluteScale(true);
	UpdateEllipse();
}

void UEllipseComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
}

void UEllipseComponent::PostLoad()
{
	Super::PostLoad();
	UpdateEllipse();
}

void UEllipseComponent::OnRegister()
{
	Super::OnRegister();
	UpdateEllipse();
}

#if WITH_EDITOR
void UEllipseComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateEllipse();
}
#endif

FBoxSphereBounds UEllipseComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const float A = GetEffectiveSemiMajor();
	const float B = GetEffectiveSemiMinor();
	const float MaxRadius = FMath::Max(A, B);
	return FBoxSphereBounds(FSphere(FVector::ZeroVector, MaxRadius).TransformBy(UE::EllipseComponent::GetTransformWithoutScale(LocalToWorld)));
}

void UEllipseComponent::UpdateEllipse()
{
	SemiMajorAxis = FMath::Max(SemiMajorAxis, UE_KINDA_SMALL_NUMBER);
	SemiMinorAxis = FMath::Clamp(SemiMinorAxis, UE_KINDA_SMALL_NUMBER, SemiMajorAxis);
	Segments = FMath::Max(Segments, 3);
	FullAngle = FMath::Clamp(FullAngle, UE_KINDA_SMALL_NUMBER, FullCircleAngle);

	RebuildCachedGeometry();

	MARK_PROPERTY_DIRTY_FROM_NAME(UEllipseComponent, SemiMajorAxis, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UEllipseComponent, SemiMinorAxis, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UEllipseComponent, Segments, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UEllipseComponent, FullAngle, this);

#if UE_ENABLE_DEBUG_DRAWING
	if (bDrawDebug) { MarkRenderStateDirty(); }
#endif
}

void UEllipseComponent::RebuildCachedGeometry()
{
	const float A = GetEffectiveSemiMajor();
	const float B = GetEffectiveSemiMinor();
	const float RotRad = FMath::DegreesToRadians(RotationAngle);
	const int32 N = Segments;
	const int32 NumPts = IsClosedLoop() ? N : N + 1;

	CachedPositions.Reset(NumPts);
	CachedTangents.Reset(NumPts);
	CachedNormals.Reset(NumPts);
	CachedArcLengths.Reset(NumPts);
	CachedCircumference = 0.0f;

	float AccumulatedLength = 0.0f;
	FVector PrevPos = UE::EllipseComponent::ComputePosition(A, B, 0.0f, RotRad);

	for (int32 i = 0; i < NumPts; ++i)
	{
		const float Alpha = static_cast<float>(i) / static_cast<float>(N);
		const float Angle = Alpha * FullAngle;
		const FVector Pos = UE::EllipseComponent::ComputePosition(A, B, Angle, RotRad);
		const FVector Tan = UE::EllipseComponent::ComputeTangent(A, B, Angle, RotRad);
		const FVector Nor = UE::EllipseComponent::ComputeNormal(A, B, Angle, RotRad);

		if (i > 0)
		{
			AccumulatedLength += FVector::Dist(PrevPos, Pos);
		}
		PrevPos = Pos;

		CachedPositions.Add(Pos);
		CachedTangents.Add(Tan);
		CachedNormals.Add(Nor);
		CachedArcLengths.Add(AccumulatedLength);
	}

	CachedCircumference = AccumulatedLength;

	// Ensure the last entry is correct if closed
	if (IsClosedLoop() && CachedArcLengths.Num() > 0)
	{
		CachedArcLengths.Last() = CachedCircumference;
	}
}

void UEllipseComponent::ComputeEllipseGeometry(float Angle, FVector& OutPosition, FVector& OutTangent, FVector& OutNormal) const
{
	const float A = GetEffectiveSemiMajor();
	const float B = GetEffectiveSemiMinor();
	const float RotRad = FMath::DegreesToRadians(RotationAngle);
	const float ClampedAngle = ClampAngle(Angle);

	OutPosition = UE::EllipseComponent::ComputePosition(A, B, ClampedAngle, RotRad);
	OutTangent  = UE::EllipseComponent::ComputeTangent(A, B, ClampedAngle, RotRad);
	OutNormal   = UE::EllipseComponent::ComputeNormal(A, B, ClampedAngle, RotRad);
}

float UEllipseComponent::ClampAngle(float InAngle) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	if (IsClosedLoop())
	{
		float Wrapped = FMath::Fmod(InAngle, SafeFullAngle);
		if (Wrapped < 0.0f) { Wrapped += SafeFullAngle; }
		return Wrapped;
	}
	return FMath::Clamp(InAngle, 0.0f, SafeFullAngle);
}

float UEllipseComponent::GetEffectiveSemiMajor() const { return FMath::Max(SemiMajorAxis, UE_KINDA_SMALL_NUMBER); }
float UEllipseComponent::GetEffectiveSemiMinor() const { return FMath::Clamp(SemiMinorAxis, UE_KINDA_SMALL_NUMBER, SemiMajorAxis); }

void UEllipseComponent::SetClosedLoop(bool bInClosedLoop)
{
	if (bInClosedLoop) { FullAngle = FullCircleAngle; }
	else { FullAngle = FMath::Clamp(FullAngle, UE_KINDA_SMALL_NUMBER, FullCircleAngle - UE_KINDA_SMALL_NUMBER); }
	UpdateEllipse();
}

bool UEllipseComponent::IsClosedLoop() const
{
	return FMath::IsNearlyEqual(FullAngle, FullCircleAngle, FullCircleAngleTolerance);
}

FVector UEllipseComponent::GetLocationAtAngle(float InAngle, bool bInWorldSpace) const
{
	FVector Pos, Tan, Nor;
	ComputeEllipseGeometry(InAngle, Pos, Tan, Nor);
	return bInWorldSpace ? UE::EllipseComponent::GetTransformWithoutScale(GetComponentTransform()).TransformPosition(Pos) : Pos;
}

FVector UEllipseComponent::GetTangentAtAngle(float InAngle, bool bInWorldSpace) const
{
	FVector Pos, Tan, Nor;
	ComputeEllipseGeometry(InAngle, Pos, Tan, Nor);
	return bInWorldSpace ? GetComponentTransform().TransformVectorNoScale(Tan).GetSafeNormal() : Tan;
}

FVector UEllipseComponent::GetNormalAtAngle(float InAngle, bool bInWorldSpace) const
{
	FVector Pos, Tan, Nor;
	ComputeEllipseGeometry(InAngle, Pos, Tan, Nor);
	return bInWorldSpace ? GetComponentTransform().TransformVectorNoScale(Nor).GetSafeNormal() : Nor;
}

int32 UEllipseComponent::GetNumSegments() const { return FMath::Max(Segments, 3); }
int32 UEllipseComponent::GetNumPoints() const { return IsClosedLoop() ? Segments : Segments + 1; }

FVector UEllipseComponent::GetLocationAtAlpha(float InAlpha, bool bInWorldSpace) const
{
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * FullAngle;
	return GetLocationAtAngle(Angle, bInWorldSpace);
}

FVector UEllipseComponent::GetTangentAtAlpha(float InAlpha, bool bInWorldSpace) const
{
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * FullAngle;
	return GetTangentAtAngle(Angle, bInWorldSpace);
}

FVector UEllipseComponent::GetNormalAtAlpha(float InAlpha, bool bInWorldSpace) const
{
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * FullAngle;
	return GetNormalAtAngle(Angle, bInWorldSpace);
}

FTransform UEllipseComponent::GetTransformAtAngle(float InAngle, bool bInWorldSpace) const
{
	const FVector Pos = GetLocationAtAngle(InAngle, false);
	const FVector Tan = GetTangentAtAngle(InAngle, false);
	const FVector Up = FVector::UpVector;
	const FQuat Rot = FRotationMatrix::MakeFromXZ(Tan, Up).ToQuat();
	FTransform Result(Rot, Pos);
	if (bInWorldSpace)
	{
		Result = Result * UE::EllipseComponent::GetTransformWithoutScale(GetComponentTransform());
	}
	return Result;
}

FTransform UEllipseComponent::GetTransformAtAlpha(float InAlpha, bool bInWorldSpace) const
{
	return GetTransformAtAngle(FMath::Clamp(InAlpha, 0.0f, 1.0f) * FullAngle, bInWorldSpace);
}

float UEllipseComponent::GetApproximateCircumference() const { return CachedCircumference; }

#if UE_ENABLE_DEBUG_DRAWING
FPrimitiveSceneProxy* UEllipseComponent::CreateSceneProxy()
{
	class FEllipseSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		explicit FEllipseSceneProxy(const UEllipseComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
			, bDraw(InComponent->bDrawDebug)
			, A(FMath::Max(InComponent->SemiMajorAxis, UE_KINDA_SMALL_NUMBER))
			, B(FMath::Clamp(InComponent->SemiMinorAxis, UE_KINDA_SMALL_NUMBER, InComponent->SemiMajorAxis))
			, RotRad(FMath::DegreesToRadians(InComponent->RotationAngle))
			, Segs(FMath::Max(InComponent->Segments, 3))
			, FullAng(FMath::Clamp(InComponent->FullAngle, UE_KINDA_SMALL_NUMBER, InComponent->FullCircleAngle))
			, bClosed(InComponent->IsClosedLoop())
			, Thick(FMath::Max(InComponent->Thickness, 0.0f))
			, Shape(InComponent->ShapeType)
#if WITH_EDITORONLY_DATA
			, LineColor(FLinearColor::White)
#else
			, LineColor(FLinearColor::White)
#endif
		{}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if ((VisibilityMap & (1u << ViewIndex)) == 0u) continue;

				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				const FTransform LocalToWorld = UE::EllipseComponent::GetTransformWithoutScale(FTransform(GetLocalToWorld()));
				const FLinearColor DrawColor = GetViewSelectionColor(LineColor, *View, IsSelected(), IsHovered(), false, IsIndividuallySelected());

				const int32 N = bClosed ? Segs : Segs + 1;
				TArray<FVector> Verts;
				Verts.Reserve(N);
				for (int32 i = 0; i < N; ++i)
				{
					const float Alpha = static_cast<float>(i) / static_cast<float>(Segs);
					const float Angle = Alpha * FullAng;
					Verts.Add(LocalToWorld.TransformPosition(UE::EllipseComponent::ComputePosition(A, B, Angle, RotRad)));
				}

				if (Shape == EEllipseShapeType::Solid && N >= 3)
				{
					FDynamicMeshBuilder MeshBuilder(View->GetFeatureLevel());
					const FVector Center = LocalToWorld.GetLocation();
					const FVector XAxis = LocalToWorld.GetUnitAxis(EAxis::X);
					const FVector YAxis = LocalToWorld.GetUnitAxis(EAxis::Y);
					const FVector ZAxis = (XAxis ^ YAxis).GetSafeNormal();

					FDynamicMeshVertex CV;
					CV.Position = FVector3f(Center);
					CV.Color = DrawColor.ToFColor(true);
					CV.TextureCoordinate[0] = FVector2f(0.5f, 0.5f);
					CV.SetTangents((FVector3f)XAxis, (FVector3f)YAxis, (FVector3f)ZAxis);
					const int32 CI = MeshBuilder.AddVertex(CV);

					for (const FVector& V : Verts)
					{
						FDynamicMeshVertex MV;
						MV.Position = FVector3f(V);
						MV.Color = DrawColor.ToFColor(true);
						MV.TextureCoordinate[0] = FVector2f(0.0f, 0.0f);
						MV.SetTangents((FVector3f)XAxis, (FVector3f)YAxis, (FVector3f)ZAxis);
						MeshBuilder.AddVertex(MV);
					}

					for (int32 i = 1; i < N; ++i) { MeshBuilder.AddTriangle(CI, i, i + 1); }

					const FMaterialRenderProxy* MatProxy = nullptr;
					if (GEngine && GEngine->DebugMeshMaterial)
					{
						MatProxy = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(GEngine->DebugMeshMaterial->GetRenderProxy(), DrawColor, "GizmoColor");
					}
					else
					{
						MatProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					}
					MeshBuilder.GetMesh(LocalToWorld.ToMatrixWithScale(), MatProxy, SDPG_World, true, false, true, ViewIndex, Collector);
				}
				else
				{
					for (int32 i = 1; i < Verts.Num(); ++i)
					{
						PDI->DrawLine(Verts[i - 1], Verts[i], DrawColor, SDPG_World, Shape == EEllipseShapeType::ThickWire ? Thick : UEllipseComponent::WireThickness, 0.0f, true);
					}
				}
			}
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = bDraw && IsShown(View) && View->Family->EngineShowFlags.Splines;
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			return Result;
		}

		virtual uint32 GetMemoryFootprint(void) const override { return sizeof(*this) + GetAllocatedSize(); }
		uint32 GetAllocatedSize(void) const { return FPrimitiveSceneProxy::GetAllocatedSize(); }

	private:
		bool bDraw;
		float A, B, RotRad;
		int32 Segs;
		float FullAng;
		bool bClosed;
		float Thick;
		EEllipseShapeType Shape;
		FLinearColor LineColor;
	};

	return new FEllipseSceneProxy(this);
}
#endif

#undef LOCTEXT_NAMESPACE
