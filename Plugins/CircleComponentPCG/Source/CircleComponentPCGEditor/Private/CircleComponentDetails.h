// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/CircleComponent.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "PropertyHandle.h"
#include "Templates/SharedPointer.h"

class IDetailLayoutBuilder;

/** UI customization for UCircleComponent. */
class FCircleComponentDetails : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FCircleComponentDetails);
	}

	//~ Begin IDetailCustomization Interface.
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
	//~ End IDetailCustomization Interface.

private:
	TSharedPtr<IPropertyHandle> FullAngleHandle;

	TOptional<double> GetFullAngleDegrees() const;
	void OnFullAngleChanged(double NewValue);
	void OnFullAngleCommitted(double NewValue, ETextCommit::Type CommitInfo);
};
