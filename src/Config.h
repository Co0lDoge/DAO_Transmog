#pragma once

#include <cstdint>
#include <functional>

namespace transmog::config
{

enum class WeaponCompat : std::int32_t
{
    MatchingGrip = 0,
    SameCombatStyle = 1,
};

enum class UnlockMode : std::int32_t
{
    AllAppearances = 0,
    UnlockedOnly = 1,
};

enum class SceneRule : std::int32_t
{
    KeepTransmog = 0,
    HideHelmetsOnly = 1,
    ShowRealGear = 2,
};

enum class HelmetVisibilityRule : std::int32_t
{
    PerSlot = 0,
    HideInDialogues = 1,
    AlwaysHide = 2,
    AlwaysShow = 3,
};

enum class BackWeaponRule : std::int32_t
{
    ShowVanilla = 0,
    HideStavesAnd2H = 1,
    HideAll = 2,
};

enum class CampBehavior : std::int32_t
{
    KeepActive = 0,
    AutoSwitchCampPreset = 1,
    SuspendInCamp = 2,
};

enum class RuneVfxRule : std::int32_t
{
    PerSlot = 0,
    SuppressOnTransmog = 1,
    AlwaysSuppress = 2,
};

enum class PortraitSyncRule : std::int32_t
{
    ShowTransmog = 0,
    ShowRealGear = 1,
};

struct Settings
{
    bool requireEquipped = true;
    bool bypassPlotDisguise = true;
    bool suspendWhenCaptured = true;

    WeaponCompat weaponCompat = WeaponCompat::MatchingGrip;
    UnlockMode unlockMode = UnlockMode::AllAppearances;
    SceneRule sceneRule = SceneRule::KeepTransmog;
    HelmetVisibilityRule helmetRule = HelmetVisibilityRule::PerSlot;
    BackWeaponRule backWeaponRule = BackWeaponRule::ShowVanilla;
    CampBehavior campBehavior = CampBehavior::KeepActive;
    RuneVfxRule runeVfxRule = RuneVfxRule::PerSlot;
    PortraitSyncRule portraitRule = PortraitSyncRule::ShowTransmog;
};

// Global settings accessor
const Settings &Get();
void Set(const Settings &s);

// Helper convenience accessors
inline bool Enabled() { return true; }
inline bool RequireEquipped() { return Get().requireEquipped; }
inline bool BypassDisguises() { return Get().bypassPlotDisguise; }
inline bool HideOnNaked() { return Get().suspendWhenCaptured; }
inline bool AlwaysHideInDialogues() { return Get().helmetRule == HelmetVisibilityRule::HideInDialogues; }
inline bool AutoSwitchCamp() { return Get().campBehavior == CampBehavior::AutoSwitchCampPreset; }

// Hotkey event callbacks
void SetOnToggleHelmetCallback(std::function<void()> cb);
void SetOnCyclePresetCallback(std::function<void()> cb);
void SetOnToggleTransmogCallback(std::function<void()> cb);

// Registers Options section and Keybindings
void Init();

} // namespace transmog::config
