// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "ArchSkySceneValidator.generated.h"

class UWorld;

/** How badly a validation finding will hurt. */
UENUM()
enum class EArchValidationSeverity : uint8
{
	/** Worth knowing, but the scene works. */
	Info,
	/** The scene works but will look or perform worse than intended. */
	Warning,
	/** The scene is broken: shadow studies will be wrong or nothing will render. */
	Error
};

/** One actionable finding. */
USTRUCT()
struct FArchValidationIssue
{
	GENERATED_BODY()

	UPROPERTY()
	EArchValidationSeverity Severity = EArchValidationSeverity::Warning;

	/** What is wrong, in one sentence. */
	UPROPERTY()
	FText Message;

	/** What the user should do about it. Always populated - a finding without a fix is noise. */
	UPROPERTY()
	FText Remedy;

	/** The actor the finding is about, so the message log can offer a "select" link. */
	UPROPERTY()
	TWeakObjectPtr<AActor> RelatedActor;
};

/**
 * Checks a level for the setup mistakes that make ArchSky produce a wrong or ugly result.
 *
 * ARCH NOTE: this deliberately reports rather than repairs. Several findings (a
 * stationary directional light, a second sky atmosphere) have more than one legitimate
 * fix, and silently rewriting a lighting artist's scene would be worse than saying what is
 * wrong. The toolbar offers "Validate Scene Setup"; it never offers "Fix Scene Setup".
 */
UCLASS()
class UArchSkySceneValidator : public UObject
{
	GENERATED_BODY()

public:
	/** Runs every check against a world and returns the findings, most severe first. */
	static TArray<FArchValidationIssue> ValidateWorld(UWorld* World);

	/** Runs the checks and writes the results into the "ArchSky" message log. */
	static void ValidateAndReport(UWorld* World);

	/** Name of the message-log category this validator writes to. */
	static const FName MessageLogName;
};
