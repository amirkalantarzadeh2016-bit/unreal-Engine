#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** Log category for every subsystem/component in the Minimap module. */
MINIMAP_API DECLARE_LOG_CATEGORY_EXTERN(LogMinimap, Log, All);

class FMinimapModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
