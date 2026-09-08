# 04 — Blueprint Reference

Two parts: a hand-written orientation, then the **complete API surface extracted directly
from the headers**, so it matches the code rather than anyone's memory.

---

## Part 1 — Orientation

### Blueprint assets

The plugin ships **no** Blueprint assets — C++ base classes only. These are the ones **you
create**:

| Your asset | Reparent to | Required contents |
|---|---|---|
| `WBP_Minimap` | `MinimapWidgetBase` | `Background` (Image), `MarkerCanvas` (Canvas Panel), optional `North/South/East/West_Container` |
| `WBP_MinimapMarker` | `MinimapMarkerWidget` | `IconImage` (Image) |
| `M_Minimap` | — | Optional. Scalars `PlayerX`, `PlayerY`, `MapRotation`; texture param `MapTexture` |
| `DA_MinimapPreset` | `MinimapPresetAsset` | Optional. Portable settings |

**Name binding, not graph wiring.** `meta = (BindWidgetOptional)` means the C++ finds these
by name. Add a widget with the right name, tick *Is Variable*, and it is driven
automatically. Nothing to connect.

### Blueprint classes at a glance

| Class | Where it lives | What it does |
|---|---|---|
| `AMinimapBoundsVolume` | Level actor | Defines calibration. Owns the capture. |
| `UMinimapSubsystem` | World subsystem | Registry + the single batched update. Get it with **Get Minimap Subsystem**. |
| `UMinimapTrackedComponent` | Any Actor | Puts that actor on the map. |
| `UMinimapViewComponent` | PlayerController | One minimap viewport: anchor, orientation, zoom, compass. |
| `UMinimapCaptureComponent` | Auto-created | Top-down scene capture. Rarely touched directly. |
| `UMinimapWidgetBase` | Your widget's parent | Background, markers, compass, zoom. |
| `UMinimapMarkerWidget` | Your marker's parent | One marker icon. |
| `UMinimapFunctionLibrary` | Static | Pure maths, callable anywhere. |

### Key events you can implement or bind

**Implement on `WBP_Minimap`** (they appear as events in the graph):

| Event | When | Why |
|---|---|---|
| `Handle Pop Effect (bool bPlayForward)` | Expand/collapse changes | Branch to your existing `PlayPopEffect` / `StopPopEffect`. Named differently on purpose — declaring those names in C++ would collide on reparent. |
| `On Minimap Updated (Snapshots)` | Each batched update | Extra presentation on top of the native update. |

**Implement on `WBP_MinimapMarker`:**

| Event | When |
|---|---|
| `On Marker Updated (Snapshot)` | Every time this marker is re-projected. Native runs first. |
| `On Marker Bound (Marker)` | The pool hands this widget a new marker, or null on release. |

**Bind (Assign) these delegates:**

| Delegate | On | Fires |
|---|---|---|
| `OnMinimapPositionUpdated` | Tracked component | Position changed past tolerance |
| `OnOutOfBoundsChanged` | Tracked component | Actual OOB transition (hysteresis applied) |
| `OnVisibilityChanged` | Tracked component | Effective visibility flipped |
| `OnMinimapViewUpdated` | View component | Full snapshot array, once per update |
| `OnViewTransformChanged` | View component | Anchor or view yaw changed |
| `OnViewActorChanged` | View component | Followed actor swapped (respawn, repossession) |
| `OnCalibrationChanged` | Subsystem | Calibration installed or changed |
| `OnMarkerRegistered` / `OnMarkerUnregistered` | Subsystem | Registry changed |
| `OnBackgroundTextureChanged` | Subsystem | Background became available or was replaced |
| `OnBackgroundCaptured` | Capture component | A capture completed |

> **Multi-view note:** the tracked component's own delegates report the **primary view
> only**, because a marker has a different position in every view. For a non-primary view,
> bind `OnMinimapViewUpdated`, which carries the full per-view snapshot array.

### The ten nodes you will actually use

| Node | Class | Purpose |
|---|---|---|
| `Get Minimap Subsystem` | Library | Entry point from anywhere |
| `Request Background Refresh` | Subsystem | After furniture edits. Coalesced. |
| `Notify Minimap Content Ready` | Subsystem | Streamed / procedural content is ready |
| `Refit Bounds And Refresh` | Subsystem | Bounds themselves must change |
| `Set Marker Visible` | Tracked | Show/hide one marker |
| `Zoom In` / `Zoom Out` | View or Widget | Eased zoom |
| `Set Zoom Alpha` / `Get Zoom Alpha` | View or Widget | Slider |
| `Get Smoothed Compass Angle` | View or Widget | Drive compass art yourself |
| `Set Minimap Expanded` | Widget | Triggers `Handle Pop Effect` on change |
| `Validate Minimap Setup` | Bounds volume | Full diagnostic report |

### Editor buttons (bounds volume Details)

| Group | Button | Effect |
|---|---|---|
| Capture | **Capture / Refresh** | Render and update the preview. Editor, no PIE. Does not change configuration. |
| Capture | **Bake To Static Texture** | Save the capture as a `UTexture2D` asset and switch to Static mode |
| Bounds | **Fit To Geometry** | Tight fit to architectural meshes. ⚠️ Changes calibration |
| Bounds | **Fit To All Actors** | Looser union of actor bounds. ⚠️ Changes calibration |
| Diagnostics | **Validate Setup** | Staged pipeline report to `LogMinimap` |

---

## Part 2 — Complete API reference

*Generated from the headers. `Pure` = no exec pins; `Callable` = has exec pins;
**Editor Button** = also appears as a button in Details.*

---

## `MinimapBoundsVolume.h`

### Properties

| Property | Access |
|---|---|
| `TObjectPtr<UBoxComponent> BoundsBox` | BP read-only |
| `TObjectPtr<UBillboardComponent> EditorSprite` | Internal |
| `bool bApplyOnBeginPlay = true` | Editable in Details |
| `bool bManualOverride = false` | Editable in Details |
| `FMinimapCalibration ManualCalibration` | Editable in Details |
| `bool bUseActorYawAsMapYaw = true` | Editable in Details |
| `float AdditionalMapYaw = 0.0f` | Editable in Details |
| `bool bPreserveAspectRatio = true` | Editable in Details |
| `bool bCircularMap = false` | Editable in Details |
| `float Zoom = 1.0f` | Editable in Details |
| `bool bSwapUV = false` | Editable in Details |
| `bool bInvertU = false` | Editable in Details |
| `bool bInvertV = false` | Editable in Details |
| `bool bPreferredBounds = false` | Editable in Details |
| `FName BoundsSelectionTag = NAME_None` | Editable in Details |
| `TObjectPtr<UMinimapPresetAsset> Preset` | Editable in Details |
| `bool bOverridePresetCaptureSettings = false` | Editable in Details |
| `FMinimapCaptureSettings CaptureSettingsOverride` | Editable in Details |
| `TObjectPtr<UTexture2D> StaticMapTexture` | Editable in Details |
| `FString StaticTextureSavePath = TEXT("/Game/Minimap/Generated")` | Editable in Details |
| `FString StaticTextureAssetName` | Editable in Details |
| `bool bSwitchToStaticAfterSave = true` | Editable in Details |
| `TArray<TSoftObjectPtr<AActor>> CaptureExcludedActors` | Editable in Details |
| `TArray<TSoftObjectPtr<AActor>> CaptureIncludedActors` | Editable in Details |
| `TArray<FName> FitExcludeTags` | Editable in Details |
| `TArray<FName> FitRequireTags` | Editable in Details |
| `TArray<TSubclassOf<AActor>> FitExcludeClasses` | Editable in Details |
| `float FitMaxActorExtent = 100000.0f` | Editable in Details |
| `bool bFitIgnoreHiddenActors = true` | Editable in Details |
| `float MinComponentVolumeCubicMeters = 0.02f` | Editable in Details |
| `bool bGeometryFitRequiresCollision = false` | Editable in Details |
| `float GeometryOutlierTrim = 0.01f` | Editable in Details |
| `float FitPadding = 200.0f` | Editable in Details |
| `TObjectPtr<UMinimapCaptureComponent> CaptureComponent` | Internal |

### Functions

| Signature | Blueprint |
|---|---|
| `FMinimapCalibration BuildCalibration() const` | Callable |
| `bool ApplyCalibration()` | Callable + **Editor Button** |
| `FMinimapCaptureSettings GetEffectiveCaptureSettings() const` | Pure |
| `UMinimapCaptureComponent* GetCaptureComponent() const` | Pure |
| `void RefreshMinimapBackground()` | Callable + **Editor Button** |
| `void FitBoundsAndRefresh()` | Callable + **Editor Button** |
| `void ApplyCaptureSettingsAndRefresh()` | Callable + **Editor Button** |
| `FMinimapValidationReport ValidateMinimapSetup()` | Callable + **Editor Button** |
| `void NotifyMinimapContentReady()` | Callable |
| `bool CapturePreviewNow()` | Callable + **Editor Button** |
| `UTextureRenderTarget2D* GetPreviewRenderTarget() const` | Pure |
| `EMinimapBackgroundSource GetActiveBackgroundSource() const` | Pure |
| `FString GetPreviewStatusText() const` | Pure |
| `void FitToLevelBounds()` | Callable + **Editor Button** |
| `void FitToGeometryBounds()` | Callable + **Editor Button** |
| `void SaveCaptureAsStaticTexture()` | Callable + **Editor Button** |


---

## `MinimapCaptureComponent.h`

**Delegates:** `FOnMinimapBackgroundCaptured`

### Properties

| Property | Access |
|---|---|
| `FMinimapCaptureSettings Settings` | Editable in Details |
| `TArray<TSoftObjectPtr<AActor>> ExcludedActors` | Editable in Details |
| `TArray<TSoftObjectPtr<AActor>> IncludedActors` | Editable in Details |
| `FOnMinimapBackgroundCaptured OnBackgroundCaptured` | **Delegate** |
| `TObjectPtr<UTextureRenderTarget2D> MinimapRenderTarget` | Internal |

### Functions

| Signature | Blueprint |
|---|---|
| `bool ApplyCalibration(const FMinimapCalibration& Calibration, FString& OutError)` | Callable |
| `void RequestBackgroundRefresh()` | Callable |
| `bool RefreshBackgroundImmediate()` | Callable |
| `void ApplyCaptureSettingsAndRefresh()` | Callable |
| `UTextureRenderTarget2D* GetMinimapRenderTarget() const` | Pure |
| `bool HasCapturedBackground() const` | Pure |
| `bool IsCaptureEnabled() const` | Pure |
| `const FString& GetLastCaptureError() const` | Pure |
| `float ResolveCaptureHeight(const FMinimapCalibration& Calibration) const` | Pure |
| `bool ProbeRenderTarget(float& OutMeanLuminance, float& OutMaxLuminance, int32& OutNonBlackPixels, FString& OutSummary)` | Callable + **Editor Button** |
| `bool CaptureForPreview(const FMinimapCalibration& Calibration, FString& OutError)` | Callable |
| `int32 GetCaptureCallCount() const` | Pure |
| `FString GetCaptureDiagnostics() const` | Callable + **Editor Button** |
| `void ReleaseCaptureResources()` | Callable |


---

## `MinimapCaptureTypes.h`

**Enums:** `EMinimapBackgroundSource`, `EMinimapCaptureHeightMode`, `EMinimapRefreshPolicy`, `EMinimapCaptureLightingMode`, `EMinimapCaptureExposureMode`, `EMinimapBackgroundApplyMode`

**Structs:** `FMinimapCaptureSettings`, `FMinimapValidationIssue`, `FMinimapValidationReport`

### Properties

| Property | Access |
|---|---|
| `EMinimapBackgroundSource BackgroundSource = EMinimapBackgroundSource::StaticTexture` | Editable in Details |
| `int32 CaptureResolution = 1024` | Editable in Details |
| `EMinimapCaptureHeightMode HeightMode = EMinimapCaptureHeightMode::AutoAboveBounds` | Editable in Details |
| `float AutoHeightMargin = 500.0f` | Editable in Details |
| `float ManualCaptureHeight = 0.0f` | Editable in Details |
| `float RelativeHeightAlpha = 1.0f` | Editable in Details |
| `float CaptureDepth = 0.0f` | Editable in Details |
| `bool bHideLocalPlayerPawn = true` | Editable in Details |
| `TArray<FName> ExclusionTags` | Editable in Details |
| `bool bUseShowOnlyList = false` | Editable in Details |
| `TArray<FName> InclusionTags` | Editable in Details |
| `EMinimapRefreshPolicy RefreshPolicy = EMinimapRefreshPolicy::CaptureOnceWhenReady` | Editable in Details |
| `float RefreshCoalesceSeconds = 0.15f` | Editable in Details |
| `float PeriodicRefreshInterval = 10.0f` | Editable in Details |
| `float InitialCaptureDelay = 0.25f` | Editable in Details |
| `bool bUseFlatCaptureLook = true` | Editable in Details |
| `EMinimapCaptureLightingMode LightingMode = EMinimapCaptureLightingMode::LitNoShadows` | Editable in Details |
| `int32 EdgeMaskPixels = 4` | Editable in Details |
| `EMinimapCaptureExposureMode ExposureMode = EMinimapCaptureExposureMode::InheritScene` | Editable in Details |
| `float ExposureBias = 0.0f` | Editable in Details |
| `float FixedExposureBrightness = 1.0f` | Editable in Details |
| `int32 WarmUpPasses = 1` | Editable in Details |
| `FLinearColor ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 1.0f)` | Editable in Details |
| `bool bCaptureAlpha = false` | Editable in Details |
| `bool bIsError = false` | BP read-only |
| `FString Message` | BP read-only |
| `TArray<FMinimapValidationIssue> Issues` | BP read-only |
| `int32 ErrorCount = 0` | BP read-only |
| `int32 WarningCount = 0` | BP read-only |


---

## `MinimapFunctionLibrary.h`

### Functions

| Signature | Blueprint |
|---|---|
| `static FVector2D Rot2D(const FVector2D& V, float ThetaDegrees)` | Pure |
| `static FVector2D GetEffectiveExtent(const FMinimapCalibration& Calibration)` | Pure |
| `static FVector2D GetZoomedExtent(const FMinimapCalibration& Calibration)` | Pure |
| `static bool IsCalibrationValid(const FMinimapCalibration& Calibration)` | Pure |
| `static FMinimapProjectionContext MakeProjectionContext( const FMinimapCalibration& Calibration, const FVector2D& Anchor, float AnchorZ, float ViewYaw)` | Pure |
| `static FMinimapCalibration MakeCalibrationFromWorldRange( float MinX, float MaxX, float MinY, float MaxY, bool bLegacyAxisMapping = true, bool bPreserveAspectRatio = false)` | Pure |
| `static FVector2D ProjectWorldToNormalized( const FMinimapCalibration& Calibration, const FVector& WorldLocation, const FVector2D& Anchor, float ViewYaw)` | Pure |
| `static FVector2D ProjectWorldToNormalizedCached( const FMinimapProjectionContext& Context, const FVector& WorldLocation)` | Pure |
| `static FVector2D NormalizedToUV(const FVector2D& Normalized)` | Pure |
| `static FVector2D UVToNormalized(const FVector2D& UV)` | Pure |
| `static FVector2D NormalizedToMaterialParams(const FVector2D& Normalized)` | Pure |
| `static FVector2D NormalizedToWidgetPixels(const FVector2D& Normalized, const FVector2D& WidgetSize)` | Pure |
| `static FVector2D ProjectWorldToWidgetPixels( const FMinimapCalibration& Calibration, const FVector& WorldLocation, const FVector2D& Anchor, float ViewYaw, const FVector2D& WidgetSize)` | Pure |
| `static float GetHeightRatio(const FMinimapCalibration& Calibration, float WorldZ)` | Pure |
| `static float GetShapeMagnitude(const FVector2D& Normalized, bool bCircularMap)` | Pure |
| `static FVector2D ClampNormalizedToShape(const FVector2D& Normalized, bool bCircularMap, bool& bOutWasClamped)` | Pure |
| `static bool ResolveOutOfBounds( float ShapeMagnitude, bool bPreviouslyOutOfBounds, float EnterThreshold = 1.0f, float ExitThreshold = 0.98f)` | Pure |
| `static float GetEdgeAngle(const FVector2D& Normalized)` | Pure |
| `static float GetMarkerIconAngle(float ActorYaw, float ViewYaw)` | Pure |
| `static float GetMapRotationTurns(float ViewYaw, float MapYawOffset = 0.0f, bool bNegate = false)` | Pure |
| `static float GetCompassAngle(float ViewYaw)` | Pure |
| `static float NormalizeAngleDegrees(float AngleDegrees)` | Pure |
| `static bool IsCaptureAlignmentSupported(const FMinimapCalibration& Calibration, FString& OutReason)` | Pure |
| `static bool ComputeCaptureYaw(const FMinimapCalibration& Calibration, float& OutCaptureYaw, FString& OutReason)` | Pure |
| `static float GetCaptureOrthoWidth(const FMinimapCalibration& Calibration)` | Pure |
| `static UTexture2D* LoadPluginTexture(const FString& RelativePath)` | Callable |
| `static UTexture2D* LoadTextureByPath(const FString& FullObjectPath)` | Callable |
| `static FIntPoint ComputeCaptureResolution(const FMinimapCalibration& Calibration, int32 MaxDimension = 1024)` | Pure |


---

## `MinimapMarkerWidget.h`

### Properties

| Property | Access |
|---|---|
| `TObjectPtr<UImage> IconImage` | **Bind Widget** |
| `bool bRotateToEdgeAngleWhenOutOfBounds = true` | Editable in Details |

### Functions

| Signature | Blueprint |
|---|---|
| `void OnMarkerUpdated(const FMinimapMarkerSnapshot& Snapshot)` | BP Native Event |
| `UMinimapTrackedComponent* GetBoundMarker() const` | Pure |
| `void OnMarkerBound(UMinimapTrackedComponent* Marker)` | BP Native Event |


---

## `MinimapPresetAsset.h`

### Properties

| Property | Access |
|---|---|
| `FMinimapCaptureSettings CaptureSettings` | Editable in Details |
| `bool bPreserveAspectRatio = true` | Editable in Details |
| `bool bCircularMap = false` | Editable in Details |
| `bool bSwapUV = false` | Editable in Details |
| `bool bInvertU = false` | Editable in Details |
| `bool bInvertV = false` | Editable in Details |
| `bool bApplyCalibrationDefaults = false` | Editable in Details |


---

## `MinimapSubsystem.h`

**Delegates:** `FOnMinimapCalibrationChanged`, `FOnMinimapMarkerRegistryChanged`, `FOnMinimapBackgroundTextureChanged`

### Properties

| Property | Access |
|---|---|
| `FOnMinimapCalibrationChanged OnCalibrationChanged` | **Delegate** |
| `FOnMinimapMarkerRegistryChanged OnMarkerRegistered` | **Delegate** |
| `FOnMinimapMarkerRegistryChanged OnMarkerUnregistered` | **Delegate** |
| `FOnMinimapBackgroundTextureChanged OnBackgroundTextureChanged` | **Delegate** |
| `FMinimapCalibration Calibration` | Internal |
| `TObjectPtr<UTexture> StaticBackgroundTexture` | Internal |

### Functions

| Signature | Blueprint |
|---|---|
| `static UMinimapSubsystem* Get(const UObject* WorldContextObject)` | Pure |
| `bool SetCalibration(const FMinimapCalibration& NewCalibration)` | Callable |
| `FMinimapCalibration BP_GetCalibration() const` | Pure |
| `bool HasValidCalibration() const` | Pure |
| `void SetZoom(float NewZoom)` | Callable |
| `void RegisterMarker(UMinimapTrackedComponent* Marker)` | Callable |
| `void UnregisterMarker(UMinimapTrackedComponent* Marker)` | Callable |
| `void RegisterView(UMinimapViewComponent* View)` | Callable |
| `void UnregisterView(UMinimapViewComponent* View)` | Callable |
| `UMinimapViewComponent* GetPrimaryView() const` | Pure |
| `int32 GetRegisteredMarkerCount() const` | Pure |
| `int32 GetRegisteredViewCount() const` | Pure |
| `TArray<UMinimapTrackedComponent*> GetRegisteredMarkers() const` | Callable |
| `void RegisterBoundsVolume(AMinimapBoundsVolume* Volume)` | Callable |
| `void UnregisterBoundsVolume(AMinimapBoundsVolume* Volume)` | Callable |
| `AMinimapBoundsVolume* ResolveAuthoritativeBounds(FString& OutReason) const` | Callable |
| `void SetRequiredBoundsTag(FName NewTag)` | Callable |
| `FName GetRequiredBoundsTag() const` | Pure |
| `void RegisterBackgroundProvider(UMinimapCaptureComponent* Provider)` | Callable |
| `void UnregisterBackgroundProvider(UMinimapCaptureComponent* Provider)` | Callable |
| `UMinimapCaptureComponent* GetBackgroundProvider() const` | Pure |
| `UTexture* GetBackgroundTexture() const` | Pure |
| `void RequestBackgroundRefresh()` | Callable |
| `void RefitBoundsAndRefresh()` | Callable |
| `void ApplyCaptureSettingsAndRefresh()` | Callable |
| `void NotifyMinimapContentReady()` | Callable |
| `void SetStaticBackgroundTexture(UTexture* StaticTexture)` | Callable |
| `void SetTickInterval(float NewInterval)` | Callable |
| `float GetTickInterval() const` | Pure |
| `void SetMinimapEnabled(bool bEnabled)` | Callable |
| `bool IsMinimapEnabled() const` | Pure |
| `void ForceUpdate()` | Callable |
| `void HandleBackgroundCaptured(UMinimapCaptureComponent* Capture, UTextureRenderTarget2D* RenderTarget)` | — |


---

## `MinimapTrackedComponent.h`

**Delegates:** `FOnMinimapPositionUpdated`, `FOnMinimapOutOfBoundsChanged`, `FOnMinimapVisibilityChanged`

### Properties

| Property | Access |
|---|---|
| `FMinimapMarkerStyle Style` | Editable in Details |
| `int32 Priority = 0` | Editable in Details |
| `bool bUseActorYaw = false` | Editable in Details |
| `bool bMarkerVisible = true` | Editable in Details |
| `float MaxTrackDistance = 0.0f` | Editable in Details |
| `EMinimapOutOfBoundsPolicy OutOfBoundsPolicy = EMinimapOutOfBoundsPolicy::ClampToEdge` | Editable in Details |
| `bool bUseHeightFilter = false` | Editable in Details |
| `float HeightFilterAbove = 300.0f` | Editable in Details |
| `float HeightFilterBelow = 300.0f` | Editable in Details |
| `float MoveTolerance = 2.0f` | Editable in Details |
| `float AngleTolerance = 0.5f` | Editable in Details |
| `bool bAutoRegister = true` | Editable in Details |
| `bool bAllowIndividualTick = false` | Editable in Details |
| `FOnMinimapPositionUpdated OnMinimapPositionUpdated` | **Delegate** |
| `FOnMinimapOutOfBoundsChanged OnOutOfBoundsChanged` | **Delegate** |
| `FOnMinimapVisibilityChanged OnVisibilityChanged` | **Delegate** |
| `FMinimapMarkerSnapshot LastSnapshot` | Internal |

### Functions

| Signature | Blueprint |
|---|---|
| `void RegisterWithMinimap()` | Callable |
| `void UnregisterFromMinimap()` | Callable |
| `bool IsRegisteredWithMinimap() const` | Pure |
| `void SetMarkerVisible(bool bNewVisible)` | Callable |
| `bool IsMarkerVisible() const` | Pure |
| `FMinimapMarkerSnapshot BP_GetLastSnapshot() const` | Pure |
| `bool HasSnapshot() const` | Pure |
| `bool IsOutOfBounds() const` | Pure |
| `void MarkDirty()` | Callable |


---

## `MinimapTypes.h`

**Enums:** `EMinimapOrientationMode`, `EMinimapAnchorMode`, `EMinimapOutOfBoundsPolicy`

**Structs:** `FMinimapCalibration`, `FMinimapProjectionContext`, `FMinimapMarkerStyle`, `FMinimapMarkerSnapshot`

### Properties

| Property | Access |
|---|---|
| `FVector2D WorldCenter = FVector2D::ZeroVector` | Editable in Details |
| `FVector2D WorldExtent = FVector2D(1000.0, 1000.0)` | Editable in Details |
| `float MapYaw = 0.0f` | Editable in Details |
| `float Zoom = 1.0f` | Editable in Details |
| `float MinZ = -10000.0f` | Editable in Details |
| `float MaxZ = 10000.0f` | Editable in Details |
| `bool bPreserveAspectRatio = true` | Editable in Details |
| `bool bCircularMap = false` | Editable in Details |
| `bool bSwapUV = false` | Editable in Details |
| `bool bInvertU = false` | Editable in Details |
| `bool bInvertV = false` | Editable in Details |
| `FVector2D Anchor = FVector2D::ZeroVector` | BP read-only |
| `float AnchorZ = 0.0f` | BP read-only |
| `float ViewYaw = 0.0f` | BP read-only |
| `bool bCircularMap = false` | BP read-only |
| `bool bValid = false` | BP read-only |
| `double CosTheta = 1.0` | Internal |
| `double SinTheta = 0.0` | Internal |
| `FVector2D InvExtent = FVector2D(1.0, 1.0)` | Internal |
| `float SignU = 1.0f` | Internal |
| `float SignV = 1.0f` | Internal |
| `bool bSwapUV = false` | Internal |
| `float MinZ = -10000.0f` | Internal |
| `float MaxZ = 10000.0f` | Internal |
| `TObjectPtr<UTexture2D> Icon = nullptr` | Editable in Details |
| `TObjectPtr<UTexture2D> OutOfBoundsIcon = nullptr` | Editable in Details |
| `FLinearColor Tint = FLinearColor::White` | Editable in Details |
| `FVector2D IconSize = FVector2D(24.0, 24.0)` | Editable in Details |
| `TSubclassOf<UUserWidget> MarkerWidgetClass` | Editable in Details |
| `TObjectPtr<UMinimapTrackedComponent> Tracked = nullptr` | BP read-only |
| `FVector2D Normalized = FVector2D::ZeroVector` | BP read-only |
| `FVector2D Clamped = FVector2D::ZeroVector` | BP read-only |
| `float EdgeAngle = 0.0f` | BP read-only |
| `float IconAngle = 0.0f` | BP read-only |
| `float HeightRatio = 0.5f` | BP read-only |
| `float HeightOffset = 0.0f` | BP read-only |
| `float DistanceToAnchor = 0.0f` | BP read-only |
| `bool bOutOfBounds = false` | BP read-only |
| `bool bVisible = true` | BP read-only |
| `int32 Priority = 0` | BP read-only |


---

## `MinimapViewComponent.h`

**Delegates:** `FOnMinimapViewUpdated`, `FOnMinimapViewTransformChanged`, `FOnMinimapViewActorChanged`

### Properties

| Property | Access |
|---|---|
| `EMinimapOrientationMode OrientationMode = EMinimapOrientationMode::RotatingMap` | Editable in Details |
| `EMinimapAnchorMode AnchorMode = EMinimapAnchorMode::ViewerCentered` | Editable in Details |
| `float MapYawOffset = 0.0f` | Editable in Details |
| `bool bNegateMapRotation = false` | Editable in Details |
| `float ZoomMultiplier = 1.0f` | Editable in Details |
| `TWeakObjectPtr<AActor> ExplicitViewActor` | Editable in Details |
| `bool bPrimaryView = true` | Editable in Details |
| `float OutOfBoundsEnterThreshold = 1.0f` | Editable in Details |
| `float OutOfBoundsExitThreshold = 0.98f` | Editable in Details |
| `float AnchorMoveTolerance = 1.0f` | Editable in Details |
| `float ViewAngleTolerance = 0.25f` | Editable in Details |
| `int32 MaxMarkersPerView = 0` | Editable in Details |
| `bool bAutoRegister = true` | Editable in Details |
| `FOnMinimapViewUpdated OnMinimapViewUpdated` | **Delegate** |
| `FOnMinimapViewTransformChanged OnViewTransformChanged` | **Delegate** |
| `FOnMinimapViewActorChanged OnViewActorChanged` | **Delegate** |
| `bool bSmoothCompass = true` | Editable in Details |
| `float CompassInterpSpeed = 7.0f` | Editable in Details |
| `float CompassSettleTolerance = 0.05f` | Editable in Details |
| `bool bSmoothZoom = true` | Editable in Details |
| `float ZoomInterpSpeed = 8.0f` | Editable in Details |
| `float MinZoomMultiplier = 0.5f` | Editable in Details |
| `float MaxZoomMultiplier = 4.0f` | Editable in Details |
| `float ZoomStep = 1.25f` | Editable in Details |
| `TArray<FMinimapMarkerSnapshot> Snapshots` | Internal |
| `FMinimapProjectionContext ProjectionContext` | Internal |

### Functions

| Signature | Blueprint |
|---|---|
| `float GetSmoothedCompassAngle() const` | Pure |
| `float GetCardinalScreenAngle(int32 CardinalIndex) const` | Pure |
| `FVector2D GetCardinalRingOffset(int32 CardinalIndex, float RingRadius) const` | Pure |
| `void SetZoomTarget(float NewZoom)` | Callable |
| `void ZoomIn()` | Callable |
| `void ZoomOut()` | Callable |
| `float GetZoomAlpha() const` | Pure |
| `void SetZoomAlpha(float Alpha)` | Callable |
| `float GetZoomTarget() const` | Pure |
| `void RegisterView()` | Callable |
| `void UnregisterView()` | Callable |
| `void SetViewRenderingEnabled(bool bEnabled)` | Callable |
| `bool IsViewActive() const` | Pure |
| `void SetOrientationMode(EMinimapOrientationMode NewMode)` | Callable |
| `void SetZoomMultiplier(float NewZoom)` | Callable |
| `void SetExplicitViewActor(AActor* NewViewActor)` | Callable |
| `AActor* GetCurrentViewActor() const` | Pure |
| `FVector2D GetAnchor() const` | Pure |
| `float GetViewYaw() const` | Pure |
| `float GetMapRotationTurns() const` | Pure |
| `float GetCompassAngle() const` | Pure |
| `FVector2D GetViewerNormalizedOnFixedMap() const` | Pure |
| `FVector2D GetMaterialPlayerParams() const` | Pure |
| `TArray<FMinimapMarkerSnapshot> BP_GetSnapshots() const` | Pure |
| `FMinimapProjectionContext BP_GetProjectionContext() const` | Pure |
| `bool IsProjectionValid() const` | Pure |


---

## `MinimapWidgetBase.h`

### Properties

| Property | Access |
|---|---|
| `TObjectPtr<UImage> Background` | **Bind Widget** |
| `TObjectPtr<UWidget> North_Container` | **Bind Widget** |
| `TObjectPtr<UWidget> South_Container` | **Bind Widget** |
| `TObjectPtr<UWidget> East_Container` | **Bind Widget** |
| `TObjectPtr<UWidget> West_Container` | **Bind Widget** |
| `bool bOrbitCardinalIndicators = false` | Editable in Details |
| `float CardinalRingRadius = 110.0f` | Editable in Details |
| `bool bKeepCardinalIconsUpright = true` | Editable in Details |
| `TObjectPtr<UCanvasPanel> MarkerCanvas` | **Bind Widget** |
| `FName PlayerXParameterName = TEXT("PlayerX")` | Editable in Details |
| `FName PlayerYParameterName = TEXT("PlayerY")` | Editable in Details |
| `FName MapRotationParameterName = TEXT("MapRotation")` | Editable in Details |
| `float MaterialUpdateTolerance = 0.0005f` | Editable in Details |
| `bool bDriveMaterialParameters = true` | Editable in Details |
| `FName MapTextureParameterName = TEXT("MapTexture")` | Editable in Details |
| `EMinimapBackgroundApplyMode BackgroundApplyMode = EMinimapBackgroundApplyMode::Automatic` | Editable in Details |
| `bool bDriveNorthIndicator = true` | Editable in Details |
| `TSubclassOf<UMinimapMarkerWidget> DefaultMarkerWidgetClass` | Editable in Details |
| `int32 InitialMarkerPoolSize = 16` | Editable in Details |
| `int32 MaxMarkerWidgets = 64` | Editable in Details |
| `bool bManageMarkerWidgets = true` | Editable in Details |
| `FVector2D FallbackMapSize = FVector2D(256.0, 256.0)` | Editable in Details |
| `int32 CompositedResolution = 512` | Editable in Details |
| `TObjectPtr<UMaterialInstanceDynamic> CachedMapMID` | Internal |
| `TArray<TObjectPtr<UMinimapMarkerWidget>> MarkerPool` | Internal |
| `TObjectPtr<UTexture> AppliedBackgroundTexture` | Internal |
| `TObjectPtr<UTextureRenderTarget2D> CompositedRenderTarget` | Internal |
| `TObjectPtr<UTexture> CompositorSourceTexture` | Internal |

### Functions

| Signature | Blueprint |
|---|---|
| `void InitializeMinimap(UMinimapViewComponent* InView)` | Callable |
| `void ShutdownMinimap()` | Callable |
| `UMinimapViewComponent* GetMinimapView() const` | Pure |
| `bool IsMinimapInitialized() const` | Pure |
| `UMaterialInstanceDynamic* GetCachedMapMaterial() const` | Pure |
| `void ApplyBackgroundTexture(UTexture* BackgroundTexture)` | Callable |
| `bool UpdateCompositedBackground(UMinimapViewComponent* View)` | Callable |
| `UTextureRenderTarget2D* GetCompositedRenderTarget() const` | Pure |
| `UTexture* GetAppliedBackgroundTexture() const` | Pure |
| `void SetMinimapExpanded(bool bExpanded)` | Callable |
| `bool IsMinimapExpanded() const` | Pure |
| `void HandlePopEffect(bool bPlayForward)` | **BP Event — you implement** |
| `void UpdateCardinalIndicators()` | Callable |
| `float GetSmoothedCompassAngle() const` | Pure |
| `void ZoomIn()` | Callable |
| `void ZoomOut()` | Callable |
| `void SetZoomAlpha(float Alpha)` | Callable |
| `float GetZoomAlpha() const` | Pure |
| `void OnMinimapUpdated(const TArray<FMinimapMarkerSnapshot>& Snapshots)` | **BP Event — you implement** |
| `void HandleViewUpdated(UMinimapViewComponent* View, const TArray<FMinimapMarkerSnapshot>& Snapshots)` | — |
| `void HandleBackgroundTextureChanged(UTexture* BackgroundTexture)` | — |

