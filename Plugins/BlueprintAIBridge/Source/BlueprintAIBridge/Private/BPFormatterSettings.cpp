// Copyright Epic Games, Inc. All Rights Reserved.

#include "BPFormatterSettings.h"

#define LOCTEXT_NAMESPACE "BlueprintAIBridge"

UBPAIBridgeSettings::UBPAIBridgeSettings()
	: HorizontalPadding(280.0f)
	, VerticalPadding(160.0f)
	, ClusterSpacing(220.0f)
	, bAutoFormatAfterApply(false)
	, bAnnotateClusters(true)
	, CommentPadding(40.0f)
	, MinClusterSizeToAnnotate(2)
{
	// Muted, low-alpha fills: a comment box sits behind the nodes, so a saturated colour makes
	// the graph harder to read rather than easier.
	ClusterColors.Add(EBPClusterType::Event,         FLinearColor(0.70f, 0.20f, 0.20f, 0.25f));
	ClusterColors.Add(EBPClusterType::Pure,          FLinearColor(0.20f, 0.55f, 0.35f, 0.25f));
	ClusterColors.Add(EBPClusterType::ControlFlow,   FLinearColor(0.25f, 0.40f, 0.70f, 0.25f));
	ClusterColors.Add(EBPClusterType::ErrorHandling, FLinearColor(0.75f, 0.50f, 0.15f, 0.25f));
	ClusterColors.Add(EBPClusterType::Output,        FLinearColor(0.45f, 0.30f, 0.65f, 0.25f));
	ClusterColors.Add(EBPClusterType::Logic,         FLinearColor(0.35f, 0.35f, 0.35f, 0.25f));
}

FName UBPAIBridgeSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName UBPAIBridgeSettings::GetSectionName() const
{
	// Identifier, not a label. The text shown in the settings tree comes from the class's
	// DisplayName meta via UDeveloperSettings::GetSectionText.
	return TEXT("BlueprintFormatter");
}

FLinearColor UBPAIBridgeSettings::GetClusterColor(EBPClusterType ClusterType) const
{
	if (const FLinearColor* Found = ClusterColors.Find(ClusterType))
	{
		return *Found;
	}

	return FLinearColor(0.35f, 0.35f, 0.35f, 0.25f);
}

FString UBPAIBridgeSettings::GetClusterTypeName(EBPClusterType ClusterType)
{
	switch (ClusterType)
	{
	case EBPClusterType::Event:         return TEXT("Event");
	case EBPClusterType::Pure:          return TEXT("Pure");
	case EBPClusterType::ControlFlow:   return TEXT("Control Flow");
	case EBPClusterType::ErrorHandling: return TEXT("Error Handling");
	case EBPClusterType::Output:        return TEXT("Output");
	case EBPClusterType::Logic:         return TEXT("Logic");
	default:                            return TEXT("Logic");
	}
}

#undef LOCTEXT_NAMESPACE
