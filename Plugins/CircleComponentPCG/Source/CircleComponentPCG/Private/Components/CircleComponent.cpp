#include "Components/CircleComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SceneView.h"
#include "UObject/EditorObjectVersion.h"
#include "Math/RotationMatrix.h"
#include "MeshElementCollector.h"
#include "PrimitiveViewRelevance.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveDrawingUtils.h"
#include "Algo/ForEach.h"
#include "Net/UnrealNetwork.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Styling/SlateColor.h"
#include "Styling/StyleColors.h"

#include "LocalVertexFactory.h"
#include "SceneInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "DynamicMeshBuilder.h"
#include "StaticMeshResources.h"
#include "Misc/TransactionObjectEvent.h"
#include "Misc/MessageDialog.h"
#include "UObject/ICookInfo.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(CircleComponent)

#define LOCTEXT_NAMESPACE "CircleComponentPCG"

#if WITH_EDITOR
#include "Settings/LevelEditorViewportSettings.h"
#endif

namespace UE::CircleComponent
{
	static constexpr float FullCircleAngle = 2.0f * UE_PI;
	static constexpr float WireThickness = 1.5f;
	static constexpr float FullCircleAngleTolerance = 1e-4f;

	static FTransform GetTransformWithoutScale(const FTransform& InTransform)
	{
		return FTransform(InTransform.GetRotation(), InTransform.GetLocation(), FVector::OneVector);
	}

	static float NormalizeAngle(float InAngle, float InFullAngle)
	{
		const float SafeFullAngle = FMath::Max(InFullAngle, UE_KINDA_SMALL_NUMBER);
		float Wrapped = FMath::Fmod(InAngle, SafeFullAngle);
		if (Wrapped < 0.0f)
		{
			Wrapped += SafeFullAngle;
		}
		return Wrapped;
	}

	static float ResolveEvaluationAngle(float InAngle, float InFullAngle, bool bInClosedLoop)
	{
		const float SafeFullAngle = FMath::Max(InFullAngle, UE_KINDA_SMALL_NUMBER);
		const bool bTreatAsClosedFullCircle = bInClosedLoop && FMath::IsNearlyEqual(SafeFullAngle, FullCircleAngle, FullCircleAngleTolerance);
		return bTreatAsClosedFullCircle ? NormalizeAngle(InAngle, SafeFullAngle) : FMath::Clamp(InAngle, 0.0f, SafeFullAngle);
	}

	static FVector ComputePosition(float Radius, float Angle)
	{
		return FVector(Radius * FMath::Cos(Angle), Radius * FMath::Sin(Angle), 0.0f);
	}

	static FVector ComputeTangent(float Angle)
	{
		return FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.0f);
	}

	static void BuildCircleVertices(const FTransform& LocalToWorld, float Radius, float FullAngle, int32 Segments, TArray<FVector>& OutPoints)
	{
		const int32 SafeSegments = FMath::Max(Segments, 3);
		const float SafeFullAngle = FMath::Clamp(FullAngle, UE_KINDA_SMALL_NUMBER, FullCircleAngle);
		const float AngleStep = SafeFullAngle / static_cast<float>(SafeSegments);

		OutPoints.Reset(SafeSegments + 1);
		for (int32 Index = 0; Index <= SafeSegments; ++Index)
		{
			const float Angle = FMath::Min(Index * AngleStep, SafeFullAngle);
			const FVector Local = ComputePosition(Radius, Angle);
			OutPoints.Add(LocalToWorld.TransformPosition(Local));
		}
	}

	static float GetDefaultRadius()
	{
		return FMath::Max(UCircleComponent::GetDefaultCircleParams().Radius, UE_KINDA_SMALL_NUMBER);
	}

	static float GetUniformScaleFromRelativeScale(const FVector& RelativeScale)
	{
		const float AverageScale = (FMath::Abs(RelativeScale.X) + FMath::Abs(RelativeScale.Y) + FMath::Abs(RelativeScale.Z)) / 3.0f;
		return FMath::Max(AverageScale, UE_KINDA_SMALL_NUMBER);
	}
}

ECircleShapeType::Type ConvertIntToCircleShapeType(int32 IntValue)
{
	switch (IntValue)
	{
	case 0: return ECircleShapeType::Wire;
	case 1: return ECircleShapeType::Solid;
	case 2: return ECircleShapeType::ThickWire;
	default: return ECircleShapeType::Wire;
	}
}

int32 ConvertCircleShapeTypeToInt(ECircleShapeType::Type ShapeType)
{
	switch (ShapeType)
	{
	case ECircleShapeType::Wire: return 0;
	case ECircleShapeType::Solid: return 1;
	case ECircleShapeType::ThickWire: return 2;
	default: return 0;
	}
}

void FCircleCurves::UpdateCircle(float InRadius, int32 InSegments)
{
	const int32 SafeSegments = FMath::Max(InSegments, 3);
	const float SafeRadius = FMath::Max(InRadius, UE_KINDA_SMALL_NUMBER);

	Positions.Reset(SafeSegments);
	Tangents.Reset(SafeSegments);
	Normals.Reset(SafeSegments);
	ArcLengthTable.Points.Reset(SafeSegments + 1);

	float AccumulatedArcLength = 0.0f;
	for (int32 Index = 0; Index < SafeSegments; ++Index)
	{
		const float Alpha = static_cast<float>(Index) / static_cast<float>(SafeSegments);
		const float Angle = Alpha * UE::CircleComponent::FullCircleAngle;

		Positions.Add(UE::CircleComponent::ComputePosition(SafeRadius, Angle));
		Tangents.Add(UE::CircleComponent::ComputeTangent(Angle));
		Normals.Add(UE::CircleComponent::ComputePosition(SafeRadius, Angle).GetSafeNormal());

		AccumulatedArcLength = SafeRadius * Angle;
		ArcLengthTable.Points.Emplace(Angle, AccumulatedArcLength, 0.0f, 0.0f, CIM_Linear);
	}

	ArcLengthTable.Points.Emplace(UE::CircleComponent::FullCircleAngle, UE::CircleComponent::FullCircleAngle * SafeRadius, 0.0f, 0.0f, CIM_Linear);
	++Version;
}

float FCircleCurves::GetCircleCircumference() const
{
	if (ArcLengthTable.Points.Num() > 0)
	{
		return ArcLengthTable.Points.Last().OutVal;
	}
	return 0.0f;
}

float FCircleCurves::GetArcLengthAtAngle(float InAngle) const
{
	if (ArcLengthTable.Points.Num() < 2)
	{
		return 0.0f;
	}

	const float Angle = UE::CircleComponent::NormalizeAngle(InAngle, UE::CircleComponent::FullCircleAngle);
	return ArcLengthTable.Eval(Angle, 0.0f);
}

UCircleComponent::UCircleComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, StepsPerSegment(10)
	, FullAngle(UE::CircleComponent::FullCircleAngle)
	, bCircleHasBeenEdited(false)
	, bModifiedByConstructionScript(false)
	, bInputCircleParamsToConstructionScript(false)
	, bDrawDebug(true)
	, bClosedLoop(true)
	, DefaultUpVector(FVector::UpVector)
#if WITH_EDITORONLY_DATA
	, EditorUnselectedCircleSegmentColor(FStyleColors::White.GetSpecifiedColor())
	, EditorSelectedCircleSegmentColor(FStyleColors::AccentOrange.GetSpecifiedColor())
	, bShouldVisualizeScale(false)
	, ScaleVisualizationWidth(30.0f)
#endif
{
	bWantsOnUpdateTransform = true;
	SetUsingAbsoluteScale(true);
	SetDefaultCircle();

#if WITH_EDITORONLY_DATA
	if (GEngine)
	{
		EditorSelectedCircleSegmentColor = GEngine->GetSelectionOutlineColor();
	}
#endif

	LineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineMaterials/LineSetComponentMaterial.LineSetComponentMaterial")));
	PointMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/EngineMaterials/LineSetComponentMaterial.LineSetComponentMaterial")));
	UpdateCircle();
}

void UCircleComponent::ResetToDefaultParams()
{
	ResetToDefault();
	UpdateCircle();
}

void UCircleComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DISABLE_ALL_CLASS_REPLICATED_PROPERTIES(UCircleComponent, EFieldIteratorFlags::ExcludeSuper);
}

void UCircleComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	Ar.UsingCustomVersion(FEditorObjectVersion::GUID);
}

void UCircleComponent::PostLoad()
{
	Super::PostLoad();
	SetUsingAbsoluteScale(true);
	EnforceLockedUniformScale(false);
	SyncRelativeScaleFromRadius();
	UpdateCircle();
}

void UCircleComponent::OnRegister()
{
	Super::OnRegister();
	bWantsOnUpdateTransform = true;
	SetUsingAbsoluteScale(true);
	EnforceLockedUniformScale(false);

#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		FCookLoadScope CookLoadScope(ECookLoadType::UsedInGame);
		LineMaterialLifetimePtr = LineMaterial.LoadSynchronous();
		PointMaterialLifetimePtr = PointMaterial.LoadSynchronous();
	}
#endif
}

bool UCircleComponent::GetIgnoreBoundsForEditorFocus() const
{
	return Super::GetIgnoreBoundsForEditorFocus() || GetNumberOfCirclePoints() == 0;
}

void UCircleComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

	if (bIsSynchronizingScaleAndRadius)
	{
		return;
	}

	EnforceLockedUniformScale(false);
	SyncRadiusFromRelativeScale();
}

#if WITH_EDITOR
void UCircleComponent::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.Property != nullptr ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	const FName MemberPropertyName = PropertyChangedEvent.MemberProperty != nullptr ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;
	const FName AbsoluteScalePropertyName = USceneComponent::GetAbsoluteScalePropertyName();
	const FName RelativeScalePropertyName = USceneComponent::GetRelativeScale3DPropertyName();
	const FName RadiusPropertyName = GET_MEMBER_NAME_CHECKED(FCircleParams, Radius);
	const FName ParamsPropertyName = GET_MEMBER_NAME_CHECKED(UCircleComponent, Params);
	const FName CircleParamsPropertyName = GET_MEMBER_NAME_CHECKED(UCircleComponent, CircleParams);

	if ((PropertyName == AbsoluteScalePropertyName || MemberPropertyName == AbsoluteScalePropertyName) && !IsUsingAbsoluteScale())
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CircleComponentAbsoluteScaleLockedWarning", "CircleComponent scale is locked. Absolute scale cannot be disabled."));
		SetUsingAbsoluteScale(true);
	}

	if (PropertyName == RelativeScalePropertyName || MemberPropertyName == RelativeScalePropertyName)
	{
		EnforceLockedUniformScale(false);
		SyncRadiusFromRelativeScale();
	}
	else if (PropertyName == RadiusPropertyName || MemberPropertyName == RadiusPropertyName || PropertyName == ParamsPropertyName || MemberPropertyName == ParamsPropertyName || PropertyName == CircleParamsPropertyName || MemberPropertyName == CircleParamsPropertyName)
	{
		static const FName CircleParamsName = GET_MEMBER_NAME_CHECKED(UCircleComponent, CircleParams);
		static const FName ParamsName = GET_MEMBER_NAME_CHECKED(UCircleComponent, Params);
		if (PropertyName == CircleParamsName)
		{
			Params = CircleParams;
		}
		else if (PropertyName == ParamsName)
		{
			CircleParams = Params;
		}

		SyncRelativeScaleFromRadius();
		UpdateCircle();
		OnCircleChanged.Broadcast();
		OnCircleDisplayChanged.Broadcast();
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void UCircleComponent::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);
	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		UpdateCircle();
		OnCircleChanged.Broadcast();
	}
}
#endif

TStructOnScope<FActorComponentInstanceData> UCircleComponent::GetComponentInstanceData() const
{
	TStructOnScope<FActorComponentInstanceData> InstanceData = MakeStructOnScope<FActorComponentInstanceData, FCircleComponentInstanceData>(this);
	FCircleComponentInstanceData* CircleData = InstanceData.Cast<FCircleComponentInstanceData>();
	CircleData->bCircleHasBeenEdited = bCircleHasBeenEdited;
	CircleData->Params = Params;
	CircleData->CircleCurves = CircleCurves;
	CircleData->bClosedLoop = IsClosedLoop();
	CircleData->FullAngle = FullAngle;
	return InstanceData;
}

#if UE_ENABLE_DEBUG_DRAWING
void UCircleComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	if (LineMaterialLifetimePtr) { OutMaterials.Add(LineMaterialLifetimePtr); }
	if (PointMaterialLifetimePtr) { OutMaterials.Add(PointMaterialLifetimePtr); }
	if (Params.ShapeType == ECircleShapeType::Solid && GEngine && GEngine->DebugMeshMaterial)
	{
		OutMaterials.Add(GEngine->DebugMeshMaterial);
	}
}

FPrimitiveSceneProxy* UCircleComponent::CreateSceneProxy()
{
	class FCircleSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		explicit FCircleSceneProxy(const UCircleComponent* InComponent)
			: FPrimitiveSceneProxy(InComponent)
			, bDraw(InComponent->bDrawDebug)
			, Radius(FMath::Max(InComponent->GetRadius(), UE_KINDA_SMALL_NUMBER))
			, Segments(FMath::Max(InComponent->GetSegments(), 3))
			, FullAngle(FMath::Clamp(InComponent->FullAngle, UE_KINDA_SMALL_NUMBER, UE::CircleComponent::FullCircleAngle))
			, Thickness(FMath::Max(InComponent->Params.Thickness, 0.0f))
			, ShapeType(InComponent->Params.ShapeType)
#if WITH_EDITORONLY_DATA
			, LineColor(InComponent->EditorUnselectedCircleSegmentColor)
#else
			, LineColor(FLinearColor::White)
#endif
		{
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
			{
				if ((VisibilityMap & (1u << ViewIndex)) == 0u)
				{
					continue;
				}

				const FSceneView* View = Views[ViewIndex];
				FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
				const FTransform LocalToWorld = UE::CircleComponent::GetTransformWithoutScale(FTransform(GetLocalToWorld()));
				const FLinearColor DrawColor = GetViewSelectionColor(LineColor, *View, IsSelected(), IsHovered(), false, IsIndividuallySelected());

				const float DistanceSqr = (View->ViewMatrices.GetViewOrigin() - LocalToWorld.GetLocation()).SizeSquared();
				if (DistanceSqr < FMath::Square(GetMinDrawDistance()) || DistanceSqr > FMath::Square(GetMaxDrawDistance()))
				{
					continue;
				}

				if (ShapeType == ECircleShapeType::Solid)
				{
					const FMaterialRenderProxy* SolidMaterialProxy = nullptr;
					TArray<FVector> CircleVertices;
					UE::CircleComponent::BuildCircleVertices(LocalToWorld, Radius, FullAngle, Segments, CircleVertices);

					FDynamicMeshBuilder MeshBuilder(View->GetFeatureLevel());
					const FVector Center = LocalToWorld.GetLocation();
					const FVector XAxis = LocalToWorld.GetUnitAxis(EAxis::X);
					const FVector YAxis = LocalToWorld.GetUnitAxis(EAxis::Y);
					const FVector ZAxis = (XAxis ^ YAxis).GetSafeNormal();

					FDynamicMeshVertex CenterVertex;
					CenterVertex.Position = FVector3f(Center);
					CenterVertex.Color = DrawColor.ToFColor(true);
					CenterVertex.TextureCoordinate[0] = FVector2f(0.5f, 0.5f);
					CenterVertex.SetTangents((FVector3f)XAxis, (FVector3f)YAxis, (FVector3f)ZAxis);
					const int32 CenterIndex = MeshBuilder.AddVertex(CenterVertex);

					for (const FVector& Vertex : CircleVertices)
					{
						FDynamicMeshVertex MeshVertex;
						MeshVertex.Position = FVector3f(Vertex);
						MeshVertex.Color = DrawColor.ToFColor(true);
						MeshVertex.TextureCoordinate[0] = FVector2f(0.0f, 0.0f);
						MeshVertex.SetTangents((FVector3f)XAxis, (FVector3f)YAxis, (FVector3f)ZAxis);
						MeshBuilder.AddVertex(MeshVertex);
					}

					const int32 LastVertexIndex = CircleVertices.Num();
					for (int32 Index = 1; Index < LastVertexIndex; ++Index)
					{
						MeshBuilder.AddTriangle(CenterIndex, Index, Index + 1);
					}

					if (GEngine && GEngine->DebugMeshMaterial)
					{
						SolidMaterialProxy = &Collector.AllocateOneFrameResource<FColoredMaterialRenderProxy>(
							GEngine->DebugMeshMaterial->GetRenderProxy(),
							DrawColor,
							"GizmoColor");
					}
					else
					{
						SolidMaterialProxy = UMaterial::GetDefaultMaterial(MD_Surface)->GetRenderProxy();
					}

					MeshBuilder.GetMesh(LocalToWorld.ToMatrixWithScale(), SolidMaterialProxy, SDPG_World, true, false, true, ViewIndex, Collector);
				}
				else
				{
					const float AngleStep = FullAngle / static_cast<float>(Segments);
					FVector PrevLocal = UE::CircleComponent::ComputePosition(Radius, 0.0f);

					for (int32 Index = 1; Index <= Segments; ++Index)
					{
						const float Angle = FMath::Min(Index * AngleStep, FullAngle);
						const FVector CurrLocal = UE::CircleComponent::ComputePosition(Radius, Angle);
						PDI->DrawLine(LocalToWorld.TransformPosition(PrevLocal), LocalToWorld.TransformPosition(CurrLocal), DrawColor, SDPG_World, ShapeType == ECircleShapeType::ThickWire ? Thickness : UE::CircleComponent::WireThickness, 0.0f, true);
						PrevLocal = CurrLocal;
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
		float Radius;
		int32 Segments;
		float FullAngle;
		float Thickness;
		ECircleShapeType::Type ShapeType;
		FLinearColor LineColor;
	};

	return new FCircleSceneProxy(this);
}
#endif

#if WITH_EDITOR
void UCircleComponent::PushSelectionToProxy()
{
	if (!IsComponentIndividuallySelected())
	{
		OnDeselectedInEditor.Broadcast(this);
	}
	Super::PushSelectionToProxy();
}
#endif

FBoxSphereBounds UCircleComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	const float EffectiveRadius = FMath::Max(GetRadius(), UE_KINDA_SMALL_NUMBER);
	const float SafeFullAngle = FMath::Clamp(FullAngle, UE_KINDA_SMALL_NUMBER, UE::CircleComponent::FullCircleAngle);

	// Full circle: use sphere bounds for simplicity
	if (FMath::IsNearlyEqual(SafeFullAngle, UE::CircleComponent::FullCircleAngle, UE::CircleComponent::FullCircleAngleTolerance))
	{
		return FBoxSphereBounds(FSphere(FVector::ZeroVector, EffectiveRadius).TransformBy(UE::CircleComponent::GetTransformWithoutScale(LocalToWorld)));
	}

	// Arc: compute axis-aligned bounding box from the arc extent
	const float MinAngle = 0.0f;
	const float MaxAngle = SafeFullAngle;

	// Collect candidate extremal points: start, end, and any quadrant boundary crossings
	TArray<FVector, TInlineAllocator<8>> Candidates;
	Candidates.Add(UE::CircleComponent::ComputePosition(EffectiveRadius, MinAngle));
	Candidates.Add(UE::CircleComponent::ComputePosition(EffectiveRadius, MaxAngle));

	// Include quadrant boundaries (0, π/2, π, 3π/2) if they lie within the arc
	constexpr float HalfPi = UE_PI * 0.5f;
	for (float Boundary : {HalfPi, UE_PI, 3.0f * HalfPi})
	{
		if (Boundary > MinAngle && Boundary < MaxAngle)
		{
			Candidates.Add(UE::CircleComponent::ComputePosition(EffectiveRadius, Boundary));
		}
	}

	FBox LocalBox(ForceInit);
	for (const FVector& Candidate : Candidates)
	{
		LocalBox += Candidate;
	}

	return FBoxSphereBounds(LocalBox.TransformBy(UE::CircleComponent::GetTransformWithoutScale(LocalToWorld)));
}

FCircleParams UCircleComponent::GetCircleParams() const
{
	return Params;
}

void UCircleComponent::SetCircleParams(const FCircleParams& InParams)
{
	Params = InParams;
	CircleParams = InParams;
	SyncRelativeScaleFromRadius();
}

const FCircleCurves& UCircleComponent::GetCircleCurves() const
{
	return CircleCurves;
}

void UCircleComponent::SetCircleCurves(const FCircleCurves& InCircleCurves)
{
	CircleCurves = InCircleCurves;
}

int32 UCircleComponent::GetVersion() const
{
	return static_cast<int32>(CircleCurves.Version);
}

void UCircleComponent::ApplyComponentInstanceData(FCircleComponentInstanceData* ComponentInstanceData, const bool bPostUCS)
{
	if (ComponentInstanceData == nullptr)
	{
		return;
	}

	bCircleHasBeenEdited = ComponentInstanceData->bCircleHasBeenEdited;
	SetCircleParams(ComponentInstanceData->Params);
	CircleCurves = ComponentInstanceData->CircleCurves;
	FullAngle = ComponentInstanceData->FullAngle;
	if (bPostUCS)
	{
		EnforceLockedUniformScale(false);
		UpdateCircle();
	}
}

void UCircleComponent::ResetToDefault()
{
	SetDefaultCircle();
	StepsPerSegment = 10;
	FullAngle = UE::CircleComponent::FullCircleAngle;
	bCircleHasBeenEdited = false;
	bModifiedByConstructionScript = false;
	bInputCircleParamsToConstructionScript = false;
	bDrawDebug = true;
	bClosedLoop = true;
	DefaultUpVector = FVector::UpVector;
#if WITH_EDITORONLY_DATA
	EditorUnselectedCircleSegmentColor = FStyleColors::White.GetSpecifiedColor();
	EditorSelectedCircleSegmentColor = FStyleColors::AccentOrange.GetSpecifiedColor();
	bShouldVisualizeScale = false;
	ScaleVisualizationWidth = 30.0f;
#endif
}

bool UCircleComponent::CanResetToDefault() const
{
	const UCircleComponent* Archetype = CastChecked<UCircleComponent>(GetArchetype());
	const bool bParamsDifferent =
		!FMath::IsNearlyEqual(Params.Radius, Archetype->Params.Radius)
		|| Params.Segments != Archetype->Params.Segments
		|| !FMath::IsNearlyEqual(Params.Thickness, Archetype->Params.Thickness)
		|| Params.ShapeType != Archetype->Params.ShapeType;

	return bParamsDifferent
		|| !FMath::IsNearlyEqual(FullAngle, Archetype->FullAngle)
		|| bDrawDebug != Archetype->bDrawDebug;
}

FCircleParams UCircleComponent::GetDefaultCircleParams()
{
	return StaticClass()->GetDefaultObject<UCircleComponent>()->Params;
}

void UCircleComponent::UpdateCircle()
{
	Params.Radius = FMath::Max(Params.Radius, UE_KINDA_SMALL_NUMBER);
	Params.Segments = FMath::Max(Params.Segments, 3);
	CircleParams = Params;
	FullAngle = FMath::Clamp(FullAngle, UE_KINDA_SMALL_NUMBER, UE::CircleComponent::FullCircleAngle);

	CircleCurves.UpdateCircle(Params.Radius, Params.Segments);

	MARK_PROPERTY_DIRTY_FROM_NAME(UCircleComponent, Params, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UCircleComponent, CircleCurves, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UCircleComponent, bCircleHasBeenEdited, this);

#if UE_ENABLE_DEBUG_DRAWING
	if (bDrawDebug)
	{
		MarkRenderStateDirty();
	}
#endif

	OnCircleUpdated.Broadcast();
	OnCircleChanged.Broadcast();
}

void UCircleComponent::EnforceLockedUniformScale(bool bForceWarning)
{
	if (bIsSynchronizingScaleAndRadius)
	{
		return;
	}

	if (!IsUsingAbsoluteScale())
	{
#if WITH_EDITOR
		if (bForceWarning)
		{
			FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("CircleComponentAbsoluteScaleLockedWarning", "CircleComponent scale is locked. Absolute scale cannot be disabled."));
		}
#endif

		TGuardValue<bool> SynchronizingGuard(bIsSynchronizingScaleAndRadius, true);
		SetUsingAbsoluteScale(true);
	}

	const FVector RelativeScale = GetRelativeScale3D();
	const float UniformScale = GetUniformScaleFromRelativeScale(RelativeScale);
	const FVector LockedScale(UniformScale);

	if (!RelativeScale.Equals(LockedScale, KINDA_SMALL_NUMBER))
	{
		TGuardValue<bool> SynchronizingGuard(bIsSynchronizingScaleAndRadius, true);
		SetRelativeScale3D(LockedScale);
	}
}

void UCircleComponent::SyncRadiusFromRelativeScale()
{
	if (bIsSynchronizingScaleAndRadius)
	{
		return;
	}

	const float DefaultRadius = GetDefaultRadius();
	const float UniformScale = GetUniformScaleFromRelativeScale(GetRelativeScale3D());
	const float NewRadius = FMath::Max(DefaultRadius * UniformScale, UE_KINDA_SMALL_NUMBER);

	if (!FMath::IsNearlyEqual(Params.Radius, NewRadius))
	{
		TGuardValue<bool> SynchronizingGuard(bIsSynchronizingScaleAndRadius, true);
		Params.Radius = NewRadius;
		CircleParams.Radius = NewRadius;
		UpdateCircle();
	}
}

void UCircleComponent::SyncRelativeScaleFromRadius()
{
	if (bIsSynchronizingScaleAndRadius)
	{
		return;
	}

	const float DefaultRadius = GetDefaultRadius();
	const float TargetScale = FMath::Max(Params.Radius / DefaultRadius, UE_KINDA_SMALL_NUMBER);
	const FVector LockedScale(TargetScale);

	if (!GetRelativeScale3D().Equals(LockedScale, KINDA_SMALL_NUMBER))
	{
		TGuardValue<bool> SynchronizingGuard(bIsSynchronizingScaleAndRadius, true);
		SetRelativeScale3D(LockedScale);
	}
}

void UCircleComponent::SetOverrideConstructionScript(bool InOverride)
{
	bCircleHasBeenEdited = InOverride;
}

void UCircleComponent::ComputeCircleGeometryAtAngle(float InAngle, FVector& OutPosition, FVector& OutTangent, FVector& OutNormal) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float EvaluationAngle = UE::CircleComponent::ResolveEvaluationAngle(InAngle, SafeFullAngle, IsClosedLoop());
	const float CircleAngle = (EvaluationAngle / SafeFullAngle) * UE::CircleComponent::FullCircleAngle;
	const float R = GetRadius();

	OutPosition = UE::CircleComponent::ComputePosition(R, CircleAngle);
	OutTangent  = UE::CircleComponent::ComputeTangent(CircleAngle).GetSafeNormal();
	OutNormal   = UE::CircleComponent::ComputePosition(1.0f, CircleAngle).GetSafeNormal();
}

FVector UCircleComponent::GetLocationAtAngle(float InAngle, bool bInWorldSpace) const
{
	FVector Position, Tangent, Normal;
	ComputeCircleGeometryAtAngle(InAngle, Position, Tangent, Normal);
	return bInWorldSpace ? UE::CircleComponent::GetTransformWithoutScale(GetComponentTransform()).TransformPosition(Position) : Position;
}

FVector UCircleComponent::GetTangentAtAngle(float InAngle, bool bInWorldSpace) const
{
	FVector Position, Tangent, Normal;
	ComputeCircleGeometryAtAngle(InAngle, Position, Tangent, Normal);
	return bInWorldSpace ? GetComponentTransform().TransformVectorNoScale(Tangent).GetSafeNormal() : Tangent;
}

FVector UCircleComponent::GetNormalAtAngle(float InAngle, bool bInWorldSpace) const
{
	FVector Position, Tangent, Normal;
	ComputeCircleGeometryAtAngle(InAngle, Position, Tangent, Normal);
	return bInWorldSpace ? GetComponentTransform().TransformVectorNoScale(Normal).GetSafeNormal() : Normal;
}

int32 UCircleComponent::GetNumberOfCircleSegments() const
{
	return FMath::Max(Params.Segments, 3);
}

int32 UCircleComponent::GetNumberOfCirclePoints() const
{
	return CircleCurves.GetNumPoints();
}

FCirclePoint UCircleComponent::GetCirclePointAtAngle(float InAngle, bool bInWorldSpace) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float EvaluationAngle = UE::CircleComponent::ResolveEvaluationAngle(InAngle, SafeFullAngle, IsClosedLoop());

	FVector Position, Tangent, Normal;
	ComputeCircleGeometryAtAngle(InAngle, Position, Tangent, Normal);

	return FCirclePoint(
		EvaluationAngle,
		bInWorldSpace ? UE::CircleComponent::GetTransformWithoutScale(GetComponentTransform()).TransformPosition(Position) : Position,
		bInWorldSpace ? GetComponentTransform().TransformVectorNoScale(Tangent).GetSafeNormal() : Tangent,
		bInWorldSpace ? GetComponentTransform().TransformVectorNoScale(Normal).GetSafeNormal() : Normal);
}

float UCircleComponent::GetCircleCircumference() const
{
	return UE::CircleComponent::FullCircleAngle * GetRadius() * (FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER) / UE::CircleComponent::FullCircleAngle);
}

void UCircleComponent::SetDrawDebug(bool bShow)
{
	bDrawDebug = bShow;
	MarkRenderStateDirty();
	OnCircleDisplayChanged.Broadcast();
}

void UCircleComponent::SetClosedLoop(bool bInClosedLoop, bool bUpdateCircle)
{
	if (bInClosedLoop)
	{
		FullAngle = UE::CircleComponent::FullCircleAngle;
	}
	else
	{
		// Non-closed: clamp to at most 2π - epsilon so the derivative below stays correct
		FullAngle = FMath::Clamp(FullAngle, UE_KINDA_SMALL_NUMBER, UE::CircleComponent::FullCircleAngle - UE_KINDA_SMALL_NUMBER);
	}

	if (bUpdateCircle)
	{
		UpdateCircle();
	}
}

bool UCircleComponent::IsClosedLoop() const
{
	return FMath::IsNearlyEqual(FullAngle, UE::CircleComponent::FullCircleAngle, UE::CircleComponent::FullCircleAngleTolerance);
}

void UCircleComponent::SetRadius(float NewRadius, bool bUpdateCircle)
{
	Params.Radius = NewRadius;
	CircleParams.Radius = NewRadius;
	SyncRelativeScaleFromRadius();
	if (bUpdateCircle)
	{
		UpdateCircle();
	}
}

float UCircleComponent::GetRadius() const
{
	return FMath::Max(Params.Radius, UE_KINDA_SMALL_NUMBER);
}

void UCircleComponent::SetSegments(int32 NewSegments, bool bUpdateCircle)
{
	Params.Segments = NewSegments;
	CircleParams.Segments = NewSegments;
	if (bUpdateCircle)
	{
		UpdateCircle();
	}
}

int32 UCircleComponent::GetSegments() const
{
	return FMath::Max(Params.Segments, 3);
}

float UCircleComponent::GetArcLengthAtAngle(float InAngle) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float Clamped = FMath::Clamp(InAngle, 0.0f, SafeFullAngle);
	return (Clamped / SafeFullAngle) * GetCircleCircumference();
}

FCirclePoint UCircleComponent::GetCirclePointFromCurves(float InAngle, bool bInWorldSpace) const
{
	return GetCirclePointAtAngle(InAngle, bInWorldSpace);
}

const TArray<FVector>& UCircleComponent::GetCirclePositions() const
{
	return CircleCurves.Positions;
}

const TArray<FVector>& UCircleComponent::GetCircleTangents() const
{
	return CircleCurves.Tangents;
}

const TArray<FVector>& UCircleComponent::GetCircleNormals() const
{
	return CircleCurves.Normals;
}

void UCircleComponent::SetDefaultCircle()
{
	SetCircleParams(FCircleParams(100.0f, 64, 5.0f, ECircleShapeType::Wire));
}

UCircleComponent::~UCircleComponent()
{
}

float UCircleComponent::GetAngleAtDistanceAlongCircle(float Distance) const
{
	const float Circumference = GetCircleCircumference();
	if (Circumference <= 0.0f)
	{
		return 0.0f;
	}

	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float SafeDistance = FMath::Clamp(Distance, 0.0f, Circumference);
	return (SafeDistance / Circumference) * SafeFullAngle;
}

float UCircleComponent::GetDistanceAtAngle(float InAngle) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float ClampedAngle = FMath::Clamp(InAngle, 0.0f, SafeFullAngle);
	return (ClampedAngle / SafeFullAngle) * GetCircleCircumference();
}

float UCircleComponent::GetCircleLength() const
{
	return GetCircleCircumference();
}

FVector UCircleComponent::GetLocationAtDistanceAlongCircle(float Distance, bool bInWorldSpace) const
{
	const float Angle = GetAngleAtDistanceAlongCircle(Distance);
	return GetLocationAtAngle(Angle, bInWorldSpace);
}

FVector UCircleComponent::GetTangentAtDistanceAlongCircle(float Distance, bool bInWorldSpace) const
{
	const float Angle = GetAngleAtDistanceAlongCircle(Distance);
	return GetTangentAtAngle(Angle, bInWorldSpace);
}

FVector UCircleComponent::GetNormalAtDistanceAlongCircle(float Distance, bool bInWorldSpace) const
{
	const float Angle = GetAngleAtDistanceAlongCircle(Distance);
	return GetNormalAtAngle(Angle, bInWorldSpace);
}

FCirclePoint UCircleComponent::GetCirclePointAtDistanceAlongCircle(float Distance, bool bInWorldSpace) const
{
	const float Angle = GetAngleAtDistanceAlongCircle(Distance);
	return GetCirclePointAtAngle(Angle, bInWorldSpace);
}

FVector UCircleComponent::GetLocationAtScaledAngle(float InAlpha, bool bInWorldSpace) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * SafeFullAngle;
	return GetLocationAtAngle(Angle, bInWorldSpace);
}

FVector UCircleComponent::GetTangentAtScaledAngle(float InAlpha, bool bInWorldSpace) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * SafeFullAngle;
	return GetTangentAtAngle(Angle, bInWorldSpace);
}

FVector UCircleComponent::GetNormalAtScaledAngle(float InAlpha, bool bInWorldSpace) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * SafeFullAngle;
	return GetNormalAtAngle(Angle, bInWorldSpace);
}

FCirclePoint UCircleComponent::GetCirclePointAtScaledAngle(float InAlpha, bool bInWorldSpace) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * SafeFullAngle;
	return GetCirclePointAtAngle(Angle, bInWorldSpace);
}

FQuat UCircleComponent::GetQuaternionAtAngle(float InAngle, bool bInWorldSpace) const
{
	const FVector Tangent = GetTangentAtAngle(InAngle, false);
	const FVector Normal = GetNormalAtAngle(InAngle, false);
	
	// Build rotation: +X aligns with tangent, +Z aligns with up vector
	const FVector SafeTangent = Tangent.GetSafeNormal();
	const FVector SafeUp = DefaultUpVector.GetSafeNormal();
	const FVector SafeNormal = Normal.GetSafeNormal();
	
	FMatrix RotMatrix = FRotationMatrix::MakeFromXZ(SafeTangent, SafeUp);
	FQuat LocalQuat(RotMatrix);
	
	if (bInWorldSpace)
	{
		return GetComponentQuat() * LocalQuat;
	}
	return LocalQuat;
}

FTransform UCircleComponent::GetTransformAtAngle(float InAngle, bool bInWorldSpace) const
{
	FTransform Result;
	
	const FVector Location = GetLocationAtAngle(InAngle, bInWorldSpace);
	const FQuat Rotation = GetQuaternionAtAngle(InAngle, bInWorldSpace);
	
	Result.SetComponents(Rotation, Location, FVector::OneVector);
	return Result;
}

FTransform UCircleComponent::GetTransformAtScaledAngle(float InAlpha, bool bInWorldSpace) const
{
	const float SafeFullAngle = FMath::Max(FullAngle, UE_KINDA_SMALL_NUMBER);
	const float Angle = FMath::Clamp(InAlpha, 0.0f, 1.0f) * SafeFullAngle;
	return GetTransformAtAngle(Angle, bInWorldSpace);
}

#undef LOCTEXT_NAMESPACE
