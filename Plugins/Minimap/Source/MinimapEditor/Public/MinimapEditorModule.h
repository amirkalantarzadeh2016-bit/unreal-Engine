#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Editor-only module for the Minimap plugin.
 *
 * Exists so the Details-panel customisation can depend on UnrealEd and PropertyEditor
 * without dragging those into the runtime module, which must stay packageable.
 */
class FMinimapEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
