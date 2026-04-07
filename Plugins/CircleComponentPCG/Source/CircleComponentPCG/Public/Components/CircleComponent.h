#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/PrimitiveComponent.h" 
#include "BoxTypes.h"

#include "CircleComponent.generated.h"

class FPrimitiveSceneProxy;
class FPrimitiveDrawInterface;
class FSceneView;

/** Permitted circle shape types for CircleComponent. */
UENUM(BlueprintType)
namespace ECircleShapeType
{
	enum Type : int
	{
		Wire,
		Solid,
		ThickWire
	};
}

USTRUCT(BlueprintType)
struct FCircleParams
{
	GENERATED_BODY()

	/** Circle radius */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Circle, meta = (DisplayName="Radius(半径)", ClampMin = "0.0", UIMin = "0.0"))
	float Radius = 100.0f;

	/** Number of segments */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Circle, meta = (DisplayName="Segments(分段数)", ClampMin = "3", UIMin = "3"))
	int32 Segments = 64;

	/** Line thickness used by ThickWire */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Circle, meta = (DisplayName="Thickness(厚度)", ClampMin = "0.0", UIMin = "0.0"))
	float Thickness = 5.0f;

	/** Circle shape type */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Circle, meta = (DisplayName="Shape Type(形状类型)"))
	TEnumAsByte<ECircleShapeType::Type> ShapeType = ECircleShapeType::Wire;

	FCircleParams() = default;

	FCircleParams(float InRadius, int32 InSegments, float InThickness, ECircleShapeType::Type InShapeType)
		: Radius(InRadius), Segments(InSegments), Thickness(InThickness), ShapeType(InShapeType)
	{
	}
};

USTRUCT(BlueprintType)
struct FCirclePoint
{
	GENERATED_BODY()

	/** Angle on the circle (stored in radians) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CirclePoint, meta = (DisplayName="Angle(弧度)"))
	float Angle;

	/** 3D world position at this angle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CirclePoint, meta = (DisplayName="Position(位置)"))
	FVector Position;

	/** Tangent direction at this angle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CirclePoint, meta = (DisplayName="Tangent(切线)"))
	FVector Tangent;

	/** Normal direction at this angle (pointing outward) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = CirclePoint, meta = (DisplayName="Normal(法线)"))
	FVector Normal;

	/** Default constructor */
	FCirclePoint()
		: Angle(0.0f), Position(0.0f), Tangent(1.0f, 0.0f, 0.0f), Normal(0.0f, 1.0f, 0.0f)
	{}

	/** Constructor */
	FCirclePoint(float InAngle, const FVector& InPosition, const FVector& InTangent, const FVector& InNormal)
		: Angle(InAngle), Position(InPosition), Tangent(InTangent), Normal(InNormal)
	{}
};

/**
 * Circle parameterization data container, stores geometric info for points on the circle
 * Similar to FSplineCurves design in SplineComponent
 */
USTRUCT()
struct FCircleCurves
{
	GENERATED_BODY()

	/** World positions of points on the circle */
	UPROPERTY()
	TArray<FVector> Positions;

	/** Tangent directions at each point */
	UPROPERTY()
	TArray<FVector> Tangents;

	/** Normal directions at each point (pointing outward) */
	UPROPERTY()
	TArray<FVector> Normals;

	/** Arc length to angle parameterization table */
	UPROPERTY()
	FInterpCurveFloat ArcLengthTable;

	/** Version number for tracking updates */
	UPROPERTY(transient)
	uint32 Version = 0xffffffff;

	bool operator==(const FCircleCurves& Other) const
	{
		return Positions == Other.Positions && Tangents == Other.Tangents && Normals == Other.Normals;
	}

	bool operator!=(const FCircleCurves& Other) const
	{
		return !(*this == Other);
	}

	/**
	 * Update internal data based on circle parameters
	 * @param InRadius Circle radius
	 * @param InSegments Number of segments
	 * @param InScale Deprecated and ignored. Kept for compatibility.
	 */
	CIRCLECOMPONENTPCG_API void UpdateCircle(float InRadius, int32 InSegments, const FVector& InScale = FVector(1.0f));

	/** Returns circle circumference */
	CIRCLECOMPONENTPCG_API float GetCircleCircumference() const;

	/** Returns arc length at specified angle */
	CIRCLECOMPONENTPCG_API float GetArcLengthAtAngle(float InAngle) const;

	/** Get total number of points on the circle */
	int32 GetNumPoints() const { return Positions.Num(); }
};

// --- CLASS DECLARATION ---

/**
 * A circle component is a circular shape which can be used for other purposes (e.g. defining areas, paths).
 * It contains debug rendering capabilities.
 */
UCLASS(ClassGroup=Utility, ShowCategories = (Mobility), HideCategories = (Physics, Collision, Lighting, Rendering, Mobile), meta=(BlueprintSpawnableComponent))
class CIRCLECOMPONENTPCG_API UCircleComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

public:
	virtual ~UCircleComponent() override;

protected:
	
	UPROPERTY()
	FCircleParams CircleParams;
	
public:

	UFUNCTION(CallInEditor, Category="Circle")
	void ResetToDefaultParams();

	static FName GetCirclePropertyName() { return GET_MEMBER_NAME_CHECKED(UCircleComponent, CircleParams); }

	UPROPERTY(EditAnywhere, Replicated, Category=Parameters, meta=(ShowOnlyInnerProperties))
	FCircleParams Params;

	/** Parameterized curve data (similar to SplineComponent) */
	UPROPERTY(Replicated)
	FCircleCurves CircleCurves;

	/** Number of steps per circle segment to place in calculations */
	UPROPERTY(EditAnywhere, Replicated, AdvancedDisplay, Category = Circle, meta=(DisplayName="Steps Per Segment(每段步数)", ClampMin=3, UIMin=3, ClampMax=1000, UIMax=1000))
	int32 StepsPerSegment;

	/** Specifies the full angle of the circle. Stored in radians and displayed in degrees in the editor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Circle, meta=(DisplayName="Full Angle(完整角度)", ForceUnits="degrees", ClampMin="0.0", UIMin="0.0", ClampMax="360.0", UIMax="360.0"))
	float FullAngle;

	/** Whether the circle has been edited from its default by the circle component visualizer */
	UPROPERTY(EditAnywhere, Replicated, Category = Circle, meta=(DisplayName="Override Construction Script(覆盖构造脚本)"))
	bool bCircleHasBeenEdited;

	UPROPERTY()
	/** Whether the UCS has made changes to the circle params */
	bool bModifiedByConstructionScript;

	/**
	 * Whether the circle params should be passed to the User Construction Script so they can be further manipulated by it.
	 * If false, they will not be visible to it, and it will not be able to influence the per-instance params set in the editor.
	 */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = Circle, meta=(DisplayName="Input Circle Params To Construction Script(传递圆参数到构造脚本)"))
	bool bInputCircleParamsToConstructionScript;

	/** If true, the circle will be rendered if the Circles showflag is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Circle, meta=(DisplayName="Draw Debug(绘制调试)"))
	bool bDrawDebug;

private:
	/**
	 * Whether the circle is to be considered as a closed loop.
	 * Use SetClosedLoop() to set this property, and IsClosedLoop() to read it.
	 */
	UPROPERTY(EditAnywhere, Replicated, Category = Circle, meta=(DisplayName="Closed Loop(闭环)"))
	bool bClosedLoop;

	/** Material used for rendering circle segments. */
	UPROPERTY(Transient)
	TSoftObjectPtr<UMaterialInterface> LineMaterial = nullptr;

	/** Material used for rendering circle points. */
	UPROPERTY(Transient)
	TSoftObjectPtr<UMaterialInterface> PointMaterial = nullptr;

	/** Holds a strong reference to LineMaterial once loaded. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> LineMaterialLifetimePtr = nullptr;

	/** Holds a strong reference to PointMaterial once loaded. */
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PointMaterialLifetimePtr = nullptr;

	int32 LineMaterialLoadID = INDEX_NONE;
	int32 PointMaterialLoadID = INDEX_NONE;
	
public:
	/** Default up vector in local space to be used when calculating transforms along the circle */
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = Circle, meta=(DisplayName="Default Up Vector(默认上向量)", Spatial))
	FVector DefaultUpVector;

#if WITH_EDITORONLY_DATA
	/** Color of unselected circle component parts in the editor */
	UPROPERTY(EditAnywhere, Category = Editor, meta = (DisplayName="Editor Circle Unselected Color(编辑器圆未选中颜色)"))
	FLinearColor EditorUnselectedCircleSegmentColor;

	/** Color of selected circle component parts in the editor */
	UPROPERTY(EditAnywhere, Category = Editor, meta = (DisplayName="Editor Circle Selected Color(编辑器圆选中颜色)"))
	FLinearColor EditorSelectedCircleSegmentColor;

	/** Whether scale visualization should be displayed */
	UPROPERTY(EditAnywhere, Category = Editor, meta=(DisplayName="Should Visualize Scale(显示缩放可视化)", InlineEditConditionToggle=true))
	bool bShouldVisualizeScale;

	/** Width of circle in editor for use with scale visualization */
	UPROPERTY(EditAnywhere, Category = Editor, meta=(DisplayName="Scale Visualization Width(缩放可视化宽度)", EditCondition="bShouldVisualizeScale"))
	float ScaleVisualizationWidth;

	/** Delegate that's called when this component is deselected in the editor */
	DECLARE_MULTICAST_DELEGATE_OneParam(DeselectedInEditorDelegate, TObjectPtr<UCircleComponent>)
	DeselectedInEditorDelegate OnDeselectedInEditor;
#endif

	//~ Begin UObject Interface
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void OnRegister() override;
	virtual bool GetIgnoreBoundsForEditorFocus() const override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent) override;
#endif
	//~ End UObject Interface

	//~ Begin UActorComponent Interface.
	virtual TStructOnScope<FActorComponentInstanceData> GetComponentInstanceData() const override;
	//~ End UActorComponent Interface.

#if UE_ENABLE_DEBUG_DRAWING
	//~ Begin UPrimitiveComponent Interface.
	virtual void GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials = false) const override;
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
#endif
	
#if WITH_EDITOR
	virtual void PushSelectionToProxy() override;
#endif
	//~ End UPrimitiveComponent Interface.

	//~ Begin USceneComponent Interface
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	//~ End USceneComponent Interface

	// Safe for caching circle data. 
	FCircleParams GetCircleParams() const;
	void SetCircleParams(const FCircleParams& InParams);

	// Safe for caching circle curves data
	FCircleCurves GetCircleCurves() const;
	void SetCircleCurves(const FCircleCurves& InCircleCurves);

	int32 GetVersion() const;

	/** Get the enabled Circle Shape types for this circle component. */
	virtual TArray<ECircleShapeType::Type> GetEnabledCircleShapeTypes() const;

	/** Controls the visibility of the Circle radius editor in the details panel. */
	virtual bool AllowsCircleRadiusEditing() const { return true; }
	/** Controls the visibility of the Circle segments editor in the details panel. */
	virtual bool AllowsCircleSegmentsEditing() const { return true; }

	void ApplyComponentInstanceData(struct FCircleInstanceData* ComponentInstanceData, const bool bPostUCS);
	void ApplyComponentInstanceData(struct FCircleComponentInstanceData* ComponentInstanceData, const bool bPostUCS);
	
	/** Reset the circle to its default shape (a circle with default params) */
	void ResetToDefault();
	bool CanResetToDefault() const;
	static FCircleParams GetDefaultCircleParams();

	/** Update the circle's internal mesh representation */
	UFUNCTION(BlueprintCallable, Category = Circle)
	virtual void UpdateCircle();

	/** Set the circle to be edited outside of the construction script */
	UFUNCTION(BlueprintCallable, Category = Circle)
	virtual void SetOverrideConstructionScript(bool InOverride);

	/** Get location along circle at the provided angle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetLocationAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Get tangent along circle at the provided angle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetTangentAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Get normal along circle at the provided angle (pointing outward) */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetNormalAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Get the number of segments that make up this circle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	int32 GetNumberOfCircleSegments() const;

	/** Get the number of points that make up this circle based on segments */
	UFUNCTION(BlueprintCallable, Category = Circle)
	int32 GetNumberOfCirclePoints() const;

	/** Gets the circle point of the circle at the specified angle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FCirclePoint GetCirclePointAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Returns total length (circumference) along this circle */
	UFUNCTION(BlueprintCallable, Category=Circle) 
	float GetCircleCircumference() const;

	/** Specify whether this circle should be rendered when the Editor/Game circle show flag is set */
	UFUNCTION(BlueprintCallable, Category = Circle)
	void SetDrawDebug(bool bShow);

	/** Specify whether the circle is a closed loop or not. Always true for a circle. */
	UFUNCTION(BlueprintCallable, Category = Circle)
	void SetClosedLoop(bool bInClosedLoop, bool bUpdateCircle = true);

	/** Check whether the circle is a closed loop or not. Always true for a circle. */
	UFUNCTION(BlueprintCallable, Category = Circle)
	bool IsClosedLoop() const;

	/** Set the radius of the circle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	void SetRadius(float NewRadius, bool bUpdateCircle = true);

	/** Get the radius of the circle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	float GetRadius() const;

	/** Set the number of segments of the circle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	void SetSegments(int32 NewSegments, bool bUpdateCircle = true);

	/** Get the number of segments of the circle */
	UFUNCTION(BlueprintCallable, Category = Circle)
	int32 GetSegments() const;

	/** Get arc length at the specified angle (in radians) */
	UFUNCTION(BlueprintCallable, Category = Circle)
	float GetArcLengthAtAngle(float InAngle) const;

	/**
	 * Get location at distance along the circle.
	 * @param Distance The distance along the circle (will be clamped to [0, Circumference])
	 * @param bInWorldSpace Whether to return world or local space location
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetLocationAtDistanceAlongCircle(float Distance, bool bInWorldSpace = true) const;

	/**
	 * Get tangent at distance along the circle.
	 * @param Distance The distance along the circle
	 * @param bInWorldSpace Whether to return world or local space vector
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetTangentAtDistanceAlongCircle(float Distance, bool bInWorldSpace = true) const;

	/**
	 * Get normal at distance along the circle.
	 * @param Distance The distance along the circle
	 * @param bInWorldSpace Whether to return world or local space vector
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetNormalAtDistanceAlongCircle(float Distance, bool bInWorldSpace = true) const;

	/**
	 * Get circle point at distance along the circle.
	 * @param Distance The distance along the circle
	 * @param bInWorldSpace Whether to return world or local space data
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FCirclePoint GetCirclePointAtDistanceAlongCircle(float Distance, bool bInWorldSpace = true) const;

	/**
	 * Get location at a normalized angle (0-1 range, where 1 = FullAngle).
	 * Similar to SplineComponent's GetLocationAtSplineInputKey.
	 * @param InAlpha Normalized position on the circle (0 = start, 1 = full circle)
	 * @param bInWorldSpace Whether to return world or local space location
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetLocationAtScaledAngle(float InAlpha, bool bInWorldSpace = true) const;

	/**
	 * Get tangent at a normalized angle (0-1 range).
	 * @param InAlpha Normalized position on the circle
	 * @param bInWorldSpace Whether to return world or local space vector
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetTangentAtScaledAngle(float InAlpha, bool bInWorldSpace = true) const;

	/**
	 * Get normal at a normalized angle (0-1 range).
	 * @param InAlpha Normalized position on the circle
	 * @param bInWorldSpace Whether to return world or local space vector
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FVector GetNormalAtScaledAngle(float InAlpha, bool bInWorldSpace = true) const;

	/**
	 * Get circle point at a normalized angle (0-1 range).
	 * @param InAlpha Normalized position on the circle
	 * @param bInWorldSpace Whether to return world or local space data
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FCirclePoint GetCirclePointAtScaledAngle(float InAlpha, bool bInWorldSpace = true) const;

	/**
	 * Convert a distance along the circle to the corresponding angle in radians.
	 * @param Distance The distance along the circle
	 * @return The corresponding angle in radians
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	float GetAngleAtDistanceAlongCircle(float Distance) const;

	/**
	 * Convert an angle (in radians) to the corresponding distance along the circle.
	 * @param InAngle The angle in radians
	 * @return The corresponding distance along the circle
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	float GetDistanceAtAngle(float InAngle) const;

	/**
	 * Get the total circumference of the circle (same as GetCircleCircumference).
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	float GetCircleLength() const;

	/**
	 * Get quaternion representing the rotation at the given angle.
	 * The rotation aligns the local +X direction with the tangent at that point.
	 * @param InAngle The angle in radians
	 * @param bInWorldSpace Whether to return world or local space rotation
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FQuat GetQuaternionAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/**
	 * Get transform at the given angle.
	 * @param InAngle The angle in radians
	 * @param bInWorldSpace Whether to return world or local space transform
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FTransform GetTransformAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/**
	 * Get transform at a normalized angle (0-1 range).
	 * @param InAlpha Normalized position on the circle
	 * @param bInWorldSpace Whether to return world or local space transform
	 */
	UFUNCTION(BlueprintCallable, Category = Circle)
	FTransform GetTransformAtScaledAngle(float InAlpha, bool bInWorldSpace = true) const;

	/** Get circle point from parameterized curves data */
	FCirclePoint GetCirclePointFromCurves(float InAngle, bool bInWorldSpace = true) const;

	/** Get the cached positions array from CircleCurves */
	const TArray<FVector>& GetCirclePositions() const;

	/** Get the cached tangents array from CircleCurves */
	const TArray<FVector>& GetCircleTangents() const;

	/** Get the cached normals array from CircleCurves */
	const TArray<FVector>& GetCircleNormals() const;

protected:

	/** Delegate broadcast any time the circle has potentially been mutated. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCircleChangedDelegate);
	UPROPERTY(BlueprintAssignable, Category = "Circle|Delegates")
	FCircleChangedDelegate OnCircleChanged;

	/** Delegate broadcast any time UpdateCircle is called (regardless of whether or not the circle has changed). */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCircleUpdatedDelegate);
	UPROPERTY(BlueprintAssignable, Category = "Circle|Delegates")
	FCircleUpdatedDelegate OnCircleUpdated;

	/** Delegate broadcast any time the circle's visualization properties have changed. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCircleDisplayChangedDelegate);
	UPROPERTY(BlueprintAssignable, Category = "Circle|Delegates")
	FCircleDisplayChangedDelegate OnCircleDisplayChanged;

private:
	
	/** Checks for consistency between CircleParams and internal state. */
	bool Validate() const;
	void EnforceLockedUniformScale(bool bForceWarning = false);
	void SyncRadiusFromRelativeScale();
	void SyncRelativeScaleFromRadius();

	/** Set the CircleParams with the default shape (Used by default constructor) */
	void SetDefaultCircle();

	bool bIsSynchronizingScaleAndRadius = false;

	// friend class FCircleComponentVisualizer;
};

/** Used to store circle data during RerunConstructionScripts */
USTRUCT()
struct FCircleInstanceData : public FSceneComponentInstanceData
{
	GENERATED_BODY()
public:
	FCircleInstanceData()
		: bCircleHasBeenEdited(false)
	{}
	explicit FCircleInstanceData(const UCircleComponent* SourceComponent)
		: FSceneComponentInstanceData(SourceComponent)
		, bCircleHasBeenEdited(false)
		, Params(SourceComponent->GetCircleParams())
		, CircleCurves(SourceComponent->GetCircleCurves())
		, bClosedLoop(SourceComponent->IsClosedLoop())
		, FullAngle(SourceComponent->FullAngle)
	{}
	virtual ~FCircleInstanceData() = default;

	virtual bool ContainsData() const override
	{
		return Super::ContainsData() || bCircleHasBeenEdited;
	}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		Super::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<UCircleComponent>(Component)->ApplyComponentInstanceData(this, (CacheApplyPhase == ECacheApplyPhase::PostUserConstructionScript));
	}

	UPROPERTY()
	bool bCircleHasBeenEdited;

	UPROPERTY()
	FCircleParams Params;

	UPROPERTY()
	FCircleCurves CircleCurves;

	UPROPERTY()
	bool bClosedLoop = true;

	UPROPERTY()
	float FullAngle = 2.0f * UE_PI;
};

/** Used to store circle data during RerunConstructionScripts */
USTRUCT()
struct FCircleComponentInstanceData : public FSceneComponentInstanceData
{
	GENERATED_BODY()
public:
	FCircleComponentInstanceData()
		: bCircleHasBeenEdited(false)
	{}
	explicit FCircleComponentInstanceData(const UCircleComponent* SourceComponent)
		: FSceneComponentInstanceData(SourceComponent)
		, bCircleHasBeenEdited(false)
		, Params(SourceComponent->GetCircleParams())
		, CircleCurves(SourceComponent->GetCircleCurves())
		, bClosedLoop(SourceComponent->IsClosedLoop())
		, FullAngle(SourceComponent->FullAngle)
	{}
	virtual ~FCircleComponentInstanceData() = default;

	virtual bool ContainsData() const override
	{
		return Super::ContainsData() || bCircleHasBeenEdited;
	}

	virtual void ApplyToComponent(UActorComponent* Component, const ECacheApplyPhase CacheApplyPhase) override
	{
		Super::ApplyToComponent(Component, CacheApplyPhase);
		CastChecked<UCircleComponent>(Component)->ApplyComponentInstanceData(this, (CacheApplyPhase == ECacheApplyPhase::PostUserConstructionScript));
	}

	UPROPERTY()
	bool bCircleHasBeenEdited;

	UPROPERTY()
	FCircleParams Params;

	UPROPERTY()
	FCircleCurves CircleCurves;

	UPROPERTY()
	bool bClosedLoop = true;

	UPROPERTY()
	float FullAngle = 2.0f * UE_PI;
};

CIRCLECOMPONENTPCG_API ECircleShapeType::Type ConvertIntToCircleShapeType(int32 IntValue);
CIRCLECOMPONENTPCG_API int32 ConvertCircleShapeTypeToInt(ECircleShapeType::Type ShapeType);
