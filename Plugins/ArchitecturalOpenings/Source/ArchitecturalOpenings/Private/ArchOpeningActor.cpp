// Copyright Epic Games, Inc. All Rights Reserved.

#include "ArchOpeningActor.h"

#include "ArchOpeningComponent.h"

AArchOpeningActor::AArchOpeningActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Opening = CreateDefaultSubobject<UArchOpeningComponent>(TEXT("Opening"));
	RootComponent = Opening;
}
