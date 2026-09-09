// Copyright Epic Games, Inc. All Rights Reserved.

#include "TourJsonUtils.h"

#include "ArchVizTourLog.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/Object.h"

namespace ArchVizTour::Json
{
	/**
	 * Transient and deprecated properties are excluded so the payload describes only authored
	 * state; editor-only properties are kept, because a preset exported from the editor has to
	 * import back into the editor unchanged.
	 */
	static constexpr int64 SkipFlags = CPF_Transient | CPF_Deprecated;

	bool ObjectToJsonString(const UObject* Object, FString& OutJson)
	{
		if (Object == nullptr)
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("ObjectToJsonString: null object."));
			return false;
		}

		const TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();
		if (!FJsonObjectConverter::UStructToJsonObject(Object->GetClass(), Object, JsonObject, /*CheckFlags*/ 0, SkipFlags))
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("ObjectToJsonString: failed to convert '%s'."), *Object->GetName());
			return false;
		}

		OutJson.Reset();
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJson);
		if (!FJsonSerializer::Serialize(JsonObject, Writer))
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("ObjectToJsonString: failed to serialise '%s'."), *Object->GetName());
			return false;
		}

		return true;
	}

	bool JsonStringToObject(const FString& InJson, UObject* Object)
	{
		if (Object == nullptr)
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("JsonStringToObject: null object."));
			return false;
		}

		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("JsonStringToObject: malformed JSON."));
			return false;
		}

		if (!FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), Object->GetClass(), Object, /*CheckFlags*/ 0, SkipFlags))
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("JsonStringToObject: payload did not match '%s'."), *Object->GetClass()->GetName());
			return false;
		}

		return true;
	}

	int32 PeekSchemaVersion(const FString& InJson)
	{
		TSharedPtr<FJsonObject> JsonObject;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InJson);
		if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
		{
			return INDEX_NONE;
		}

		int32 Version = INDEX_NONE;
		// FJsonObjectConverter lower-camel-cases field names on export, so accept both spellings
		// rather than depending on that behaviour staying put across engine versions.
		if (!JsonObject->TryGetNumberField(TEXT("schemaVersion"), Version)
			&& !JsonObject->TryGetNumberField(TEXT("SchemaVersion"), Version))
		{
			return INDEX_NONE;
		}

		return Version;
	}

	bool SaveStringToFile(const FString& AbsolutePath, const FString& Contents)
	{
		if (AbsolutePath.IsEmpty())
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("SaveStringToFile: empty path."));
			return false;
		}

		const FString Directory = FPaths::GetPath(AbsolutePath);
		if (!Directory.IsEmpty() && !IFileManager::Get().MakeDirectory(*Directory, /*Tree*/ true))
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("SaveStringToFile: could not create directory '%s'."), *Directory);
			return false;
		}

		if (!FFileHelper::SaveStringToFile(Contents, *AbsolutePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("SaveStringToFile: could not write '%s'."), *AbsolutePath);
			return false;
		}

		UE_LOG(LogArchVizTour, Log, TEXT("Wrote '%s' (%d characters)."), *AbsolutePath, Contents.Len());
		return true;
	}

	bool LoadStringFromFile(const FString& AbsolutePath, FString& OutContents)
	{
		if (!FFileHelper::LoadFileToString(OutContents, *AbsolutePath))
		{
			UE_LOG(LogArchVizTour, Warning, TEXT("LoadStringFromFile: could not read '%s'."), *AbsolutePath);
			return false;
		}

		return true;
	}
}
