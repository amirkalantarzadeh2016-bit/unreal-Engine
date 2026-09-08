// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningSubsystem.h"

#include "ArchOpeningComponent.h"

void UArchOpeningSubsystem::RegisterOpening(UArchOpeningComponent* Opening)
{
	if (!::IsValid(Opening))
	{
		return;
	}

	// Register every component a click could plausibly land on. Roles are re-checked at interaction
	// time through IsComponentClickable(), so changing the clickable flags at runtime takes effect
	// without touching the registry.
	Opening->ForEachPart([this, Opening](const FArchOpeningPartRef& Part, EArchOpeningPartRole, int32)
	{
		if (Part.IsValidPart())
		{
			ComponentToOpening.Add(Part.Component.Get(), Opening);
		}
		return true;
	});

	for (const TObjectPtr<USceneComponent>& Proxy : Opening->Interaction.InteractionProxies)
	{
		if (::IsValid(Proxy))
		{
			ComponentToOpening.Add(Proxy.Get(), Opening);
		}
	}
}

void UArchOpeningSubsystem::UnregisterOpening(UArchOpeningComponent* Opening)
{
	for (auto It = ComponentToOpening.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || It.Value() == Opening || !It.Value().IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

UArchOpeningComponent* UArchOpeningSubsystem::FindOpeningForComponent(const USceneComponent* Component) const
{
	if (Component == nullptr)
	{
		return nullptr;
	}

	if (const TWeakObjectPtr<UArchOpeningComponent>* Found = ComponentToOpening.Find(Component))
	{
		return Found->Get();
	}

	return nullptr;
}

void UArchOpeningSubsystem::Deinitialize()
{
	ComponentToOpening.Reset();

	Super::Deinitialize();
}
