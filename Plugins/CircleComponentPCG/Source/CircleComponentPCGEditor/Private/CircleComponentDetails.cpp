// Copyright Epic Games, Inc. All Rights Reserved.

#include "CircleComponentDetails.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "Internationalization/Text.h"
#include "Math/UnrealMathUtility.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SNumericEntryBox.h"

#define LOCTEXT_NAMESPACE "CircleComponentDetails"

void FCircleComponentDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	DetailBuilder.HideProperty(TEXT("CircleParams"), UCircleComponent::StaticClass());
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCircleComponent, CircleCurves), UCircleComponent::StaticClass());
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCircleComponent, bInputCircleParamsToConstructionScript), UCircleComponent::StaticClass());
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCircleComponent, bCircleHasBeenEdited), UCircleComponent::StaticClass());
	DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UCircleComponent, FullAngle), UCircleComponent::StaticClass());

	FullAngleHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UCircleComponent, FullAngle), UCircleComponent::StaticClass());
	if (!FullAngleHandle.IsValid())
	{
		return;
	}

	IDetailCategoryBuilder& CircleCategory = DetailBuilder.EditCategory("Circle");
	CircleCategory.AddCustomRow(LOCTEXT("FullAngleSearch", "Full Angle"))
	.NameContent()
	[
		FullAngleHandle->CreatePropertyNameWidget()
	]
	.ValueContent()
	.MinDesiredWidth(120.0f)
	[
		SNew(SNumericEntryBox<double>)
		.AllowSpin(true)
		.MinValue(0.0)
		.MaxValue(360.0)
		.MinSliderValue(0.0)
		.MaxSliderValue(360.0)
		.Value(this, &FCircleComponentDetails::GetFullAngleDegrees)
		.OnValueChanged(this, &FCircleComponentDetails::OnFullAngleChanged)
		.OnValueCommitted(this, &FCircleComponentDetails::OnFullAngleCommitted)
	];
}

TOptional<double> FCircleComponentDetails::GetFullAngleDegrees() const
{
	if (!FullAngleHandle.IsValid())
	{
		return TOptional<double>();
	}

	double FullAngleRadians = 0.0;
	if (FullAngleHandle->GetValue(FullAngleRadians) == FPropertyAccess::Success)
	{
		return FMath::RadiansToDegrees(FullAngleRadians);
	}

	return TOptional<double>();
}

void FCircleComponentDetails::OnFullAngleChanged(double NewValue)
{
	if (!FullAngleHandle.IsValid())
	{
		return;
	}

	FullAngleHandle->SetValue(FMath::DegreesToRadians(NewValue));
}

void FCircleComponentDetails::OnFullAngleCommitted(double NewValue, ETextCommit::Type CommitInfo)
{
	(void)CommitInfo;
	if (!FullAngleHandle.IsValid())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SetFullAngle", "Set Full Angle"));
	FullAngleHandle->NotifyPreChange();
	FullAngleHandle->SetValue(FMath::DegreesToRadians(NewValue));
	FullAngleHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
}

#undef LOCTEXT_NAMESPACE
