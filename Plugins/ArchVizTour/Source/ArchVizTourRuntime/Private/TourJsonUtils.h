// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UObject;

/**
 * Shared JSON plumbing for the two preset types.
 *
 * Both presets serialise through FJsonObjectConverter against their own UClass, so every
 * UPROPERTY - SchemaVersion and PresetId included - is written without a hand-maintained
 * field list that would drift the moment someone adds a property.
 */
namespace ArchVizTour::Json
{
	/** Serialise every non-transient UPROPERTY of Object to a pretty-printed JSON string. */
	bool ObjectToJsonString(const UObject* Object, FString& OutJson);

	/** Populate Object's UPROPERTYs from a JSON string produced by ObjectToJsonString. */
	bool JsonStringToObject(const FString& Json, UObject* Object);

	/**
	 * Read just the SchemaVersion field, before the payload is applied.
	 * Migration has to know the incoming version, and applying the payload first would already
	 * have overwritten the asset's own SchemaVersion with it.
	 * @return The version found, or INDEX_NONE when the field is absent or unparseable.
	 */
	int32 PeekSchemaVersion(const FString& Json);

	/** Write Contents as UTF-8, creating parent directories. Logs and returns false on failure. */
	bool SaveStringToFile(const FString& AbsolutePath, const FString& Contents);

	/** Read a UTF-8 file. Logs and returns false when it is missing or unreadable. */
	bool LoadStringFromFile(const FString& AbsolutePath, FString& OutContents);
}
