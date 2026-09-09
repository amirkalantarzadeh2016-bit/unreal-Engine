// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"

#include "TourPresetFactories.generated.h"

/** Content Browser factory for UTourPathPreset. */
UCLASS()
class UTourPathPresetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UTourPathPresetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};

/** Content Browser factory for UTourSequencePreset. */
UCLASS()
class UTourSequencePresetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UTourSequencePresetFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};

/** Content Browser factory for UTourInputConfig. */
UCLASS()
class UTourInputConfigFactory : public UFactory
{
	GENERATED_BODY()

public:
	UTourInputConfigFactory();

	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};
