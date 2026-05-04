#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"

#include "EllipseComponent.generated.h"

class FPrimitiveSceneProxy;

/** Permitted shape types for EllipseComponent. */
UENUM(BlueprintType)
enum class EEllipseShapeType : uint8
{
	Wire,
	Solid,
	ThickWire
};

/**
 * A component that renders an ellipse/elliptical-arc in the viewport.
 * Parameterized by semi-major axis (a), semi-minor axis (b), and rotation angle of the major axis.
 * Inheritance from UPrimitiveComponent provides Transform, Bounds, and rendering infrastructure.
 */
UCLASS(ClassGroup=Utility, ShowCategories=(Mobility), HideCategories=(Physics,Collision,Lighting,Rendering,Mobile), meta=(BlueprintSpawnableComponent))
class CIRCLECOMPONENTPCG_API UEllipseComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

public:
	/** Semi-major axis length (world units). Must be >= SemiMinorAxis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse, meta=(ClampMin="0.0", UIMin="0.0"))
	float SemiMajorAxis;

	/** Semi-minor axis length (world units). Clamped to [0, SemiMajorAxis]. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse, meta=(ClampMin="0.0", UIMin="0.0"))
	float SemiMinorAxis;

	/** Rotation angle of the semi-major axis relative to local +X, in degrees (editor) / radians (storage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse, meta=(ForceUnits="deg", ClampMin="0.0", UIMin="0.0", ClampMax="360.0", UIMax="360.0"))
	float RotationAngle;

	/** Number of segments used for rendering and PCG conversion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse, meta=(ClampMin="3", UIMin="3"))
	int32 Segments;

	/** Full angle of the elliptical arc in radians (editor displays degrees). 2π = full ellipse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse, meta=(ForceUnits="deg", ClampMin="0.0", UIMin="0.0", ClampMax="360.0", UIMax="360.0"))
	float FullAngle;

	/** Line thickness used when ShapeType is ThickWire. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse, meta=(ClampMin="0.0", UIMin="0.0"))
	float Thickness;

	/** Shape rendering style. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse)
	EEllipseShapeType ShapeType;

	/** If true, the ellipse will be rendered when the debug show flag is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Ellipse)
	bool bDrawDebug;

	// ── Blueprint-callable API ───────────────────────────────────

	/** Set whether the ellipse is treated as a closed loop. Overrides FullAngle to 2π when true. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	void SetClosedLoop(bool bInClosedLoop);

	/** Whether this ellipse is a closed loop. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	bool IsClosedLoop() const;

	/** Get world-space position at the given angle (radians). */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FVector GetLocationAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Get world-space tangent direction at the given angle. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FVector GetTangentAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Get world-space outward normal direction at the given angle. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FVector GetNormalAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Get the number of segments. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	int32 GetNumSegments() const;

	/** Get number of sample points (Segments or Segments+1 depending on closed/open). */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	int32 GetNumPoints() const;

	/** Get location at a normalized angle (0 = start, 1 = FullAngle). */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FVector GetLocationAtAlpha(float InAlpha, bool bInWorldSpace = true) const;

	/** Get tangent at a normalized angle. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FVector GetTangentAtAlpha(float InAlpha, bool bInWorldSpace = true) const;

	/** Get normal at a normalized angle. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FVector GetNormalAtAlpha(float InAlpha, bool bInWorldSpace = true) const;

	/** Get transform (position + rotation) at the given angle. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FTransform GetTransformAtAngle(float InAngle, bool bInWorldSpace = true) const;

	/** Get transform at a normalized angle. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	FTransform GetTransformAtAlpha(float InAlpha, bool bInWorldSpace = true) const;

	/** Approximate circumference using Riemann sum of chord lengths. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	float GetApproximateCircumference() const;

	/** Update the internal cached geometry. Called automatically on parameter changes. */
	UFUNCTION(BlueprintCallable, Category=Ellipse)
	void UpdateEllipse();

	// ── UObject / UActorComponent / UPrimitiveComponent overrides ──
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void OnRegister() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
#if UE_ENABLE_DEBUG_DRAWING
	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
#endif

private:
	/** Rebuild cached position/tangent/normal arrays for the given segment count. */
	void RebuildCachedGeometry();

	/** Shared computation kernel: position/tangent/normal at an angle, in local space. */
	void ComputeEllipseGeometry(float Angle, FVector& OutPosition, FVector& OutTangent, FVector& OutNormal) const;

	/** Clamp angle to valid range considering FullAngle and closed-loop state. */
	float ClampAngle(float InAngle) const;

	/** Effective semi-major axis (clamped to > 0). */
	float GetEffectiveSemiMajor() const;
	/** Effective semi-minor axis (clamped to [0, SemiMajor]). */
	float GetEffectiveSemiMinor() const;

	/** Cached positions for each segment start. */
	TArray<FVector> CachedPositions;
	/** Cached tangents for each segment start. */
	TArray<FVector> CachedTangents;
	/** Cached normals for each segment start. */
	TArray<FVector> CachedNormals;
	/** Cached cumulative arc lengths for each segment (approximate). */
	TArray<float> CachedArcLengths;
	/** Total approximate circumference. */
	float CachedCircumference = 0.0f;

	static constexpr float FullCircleAngle = 2.0f * UE_PI;
	static constexpr float FullCircleAngleTolerance = 1e-4f;
	static constexpr float WireThickness = 1.5f;
};
