// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "Data/PCGCircleData.h"
#include "Components/CircleComponent.h"

namespace PCGCircleHelpers
{
	static constexpr float FullCircleAngle = 2.0f * UE_PI;

	inline FTransform GetTransformWithoutScale(const FTransform& InTransform)
	{
		return FTransform(InTransform.GetRotation(), InTransform.GetLocation(), FVector::OneVector);
	}

	inline float NormalizeAngle(float InAngle, float InFullAngle)
	{
		const float SafeFullAngle = FMath::Max(InFullAngle, UE_KINDA_SMALL_NUMBER);
		float Wrapped = FMath::Fmod(InAngle, SafeFullAngle);
		if (Wrapped < 0.0f)
		{
			Wrapped += SafeFullAngle;
		}
		return Wrapped;
	}

	inline UPCGCircleData* CreateCircleDataFromCircleComponent(FPCGContext* Context, const UCircleComponent* CircleComponent)
	{
		check(Context);

		if (!IsValid(CircleComponent))
		{
			return nullptr;
		}

		UPCGCircleData* CircleData = FPCGContext::NewObject_AnyThread<UPCGCircleData>(Context);
		CircleData->Initialize(CircleComponent);
		return CircleData;
	}
}
