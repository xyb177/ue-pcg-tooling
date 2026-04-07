// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGContext.h"
#include "Data/PCGCircleData.h"
#include "Components/CircleComponent.h"

namespace PCGCircleHelpers
{
	inline FTransform GetTransformWithoutScale(const FTransform& InTransform)
	{
		return FTransform(InTransform.GetRotation(), InTransform.GetLocation(), FVector::OneVector);
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
