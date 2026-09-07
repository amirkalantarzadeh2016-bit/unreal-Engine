#include "MinimapEditorModule.h"

#include "MinimapBoundsVolume.h"
#include "MinimapBoundsVolumeDetails.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

#define LOCTEXT_NAMESPACE "FMinimapEditorModule"

void FMinimapEditorModule::StartupModule()
{
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyModule.RegisterCustomClassLayout(
		AMinimapBoundsVolume::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMinimapBoundsVolumeDetails::MakeInstance));

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FMinimapEditorModule::ShutdownModule()
{
	// Unregister defensively: the property module may already be gone during shutdown.
	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule =
			FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

		PropertyModule.UnregisterCustomClassLayout(AMinimapBoundsVolume::StaticClass()->GetFName());
		PropertyModule.NotifyCustomizationModuleChanged();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMinimapEditorModule, MinimapEditor)
