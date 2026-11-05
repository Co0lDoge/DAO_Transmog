#pragma once

#include "dao/Plugin.h"
#include "dao/Strings.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace transmog::strings
{

enum class StrId : std::uint32_t
{
    TransmogTitle = 1,
    WardrobeTitle = 2,
    WardrobeHeader = 3,
    PresetTravel = 4,
    PresetCity = 5,
    PresetCamp = 6,
    AlwaysShow = 7,
    HideInDialogues = 8,
    AlwaysHide = 9,
    UseEquippedLook = 10,
    HideSlotBareBody = 11,
    ApplyOutfit = 12,
    ResetSlot = 13,
    ResetOutfit = 14,
    MatchMaterialToChest = 15,
    ToggleHelmetVis = 16,
    CyclePreset = 17,
    CompanionUnsupported = 18,
    PlotDisguiseActive = 19,
    RequireEquippedItem = 20,
    RequireEquippedItemTooltip = 21,
    BypassPlotDisguise = 22,
    BypassPlotDisguiseTooltip = 23,
    SuspendWhenCaptured = 24,
    SuspendWhenCapturedTooltip = 25,
    WeaponCompatFilter = 26,
    AppearanceUnlockMode = 27,
    SceneRule = 28,
    HelmetVisibility = 29,
    BackStrappedWeapons = 30,
    CampBehavior = 31,
    RuneVfx = 32,
    PortraitAppearance = 33,
    AllAppearances = 34,
    UnlockedOnly = 35,
    KeepTransmog = 36,
    ShowRealGear = 37,
    MatchingGrip = 38,
    SameCombatStyle = 39,
    ShowVanilla = 40,
    HideStaves2H = 41,
    HideAll = 42,
    KeepActive = 43,
    AutoSwitchCampPreset = 44,
    ShowTransmogSyncOnApply = 45,
    ShowRealEquippedGear = 46,
    PerSlotSetting = 47,
    SuppressOnTransmog = 48,
    RuneVfxShow = 49,
    RuneVfxHide = 50,
    SlotUnequippedNotice = 51,
    SyncAll = 52,
    JumpToLook = 53,
    ReturnToBackpack = 54,
    TransmogModeOn = 55,
    ToastTravel = 56,
    ToastCity = 57,
    ToastCamp = 58,
};

void Init(const dao::LoaderInfo &info);
std::wstring Get(StrId id, std::wstring_view fallback = {});

} // namespace transmog::strings
