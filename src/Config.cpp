#include "Config.h"
#include "Strings.h"
#include "dao/Input.h"
#include "dao/Log.h"
#include "dao/Options.h"

namespace transmog::config
{

namespace
{
Settings g_settings;
std::function<void()> g_onToggleHelmet;
std::function<void()> g_onCyclePreset;
std::function<void()> g_onToggleTransmog;
} // namespace

const Settings &Get()
{
    return g_settings;
}

void Set(const Settings &s)
{
    g_settings = s;
}

void SetOnToggleHelmetCallback(std::function<void()> cb)
{
    g_onToggleHelmet = std::move(cb);
}

void SetOnCyclePresetCallback(std::function<void()> cb)
{
    g_onCyclePreset = std::move(cb);
}

void SetOnToggleTransmogCallback(std::function<void()> cb)
{
    g_onToggleTransmog = std::move(cb);
}

void Init()
{
    using namespace strings;

    // 1. Register Game Options Section: "Transmog"
    std::vector<dao::options::Toggle> toggles = {
        {
            .name = L"DAOTransmog_RequireEquipped",
            .label = Get(StrId::RequireEquippedItem, L"Require Equipped Item in Slot"),
            .tooltip = Get(StrId::RequireEquippedItemTooltip,
                           L"Only applies transmog appearance if an item is equipped in that slot."),
            .get = [] { return g_settings.requireEquipped; },
            .set = [](bool val) { return g_settings.requireEquipped = val; },
            .defaultValue = true,
        },
        {
            .name = L"DAOTransmog_BypassPlotDisguise",
            .label = Get(StrId::BypassPlotDisguise, L"Bypass for Plot / Disguise Gear"),
            .tooltip = Get(StrId::BypassPlotDisguiseTooltip,
                           L"Temporarily suspends transmog when plot disguises are equipped."),
            .get = [] { return g_settings.bypassPlotDisguise; },
            .set = [](bool val) { return g_settings.bypassPlotDisguise = val; },
            .defaultValue = true,
        },
        {
            .name = L"DAOTransmog_SuspendWhenCaptured",
            .label = Get(StrId::SuspendWhenCaptured, L"Suspend When Captured / Stripped"),
            .tooltip = Get(StrId::SuspendWhenCapturedTooltip,
                           L"Suspends appearance override when stripped in prison sequences."),
            .get = [] { return g_settings.suspendWhenCaptured; },
            .set = [](bool val) { return g_settings.suspendWhenCaptured = val; },
            .defaultValue = true,
        },
    };

    std::vector<dao::options::Choice> choices = {
        {
            .name = L"DAOTransmog_WeaponCompat",
            .label = Get(StrId::WeaponCompatFilter, L"Weapon Compatibility Filter"),
            .tooltip = L"",
            .values = {Get(StrId::MatchingGrip, L"Matching Grip"), Get(StrId::SameCombatStyle, L"Same Combat Style")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.weaponCompat); },
            .set =
                [](std::int32_t val) {
                    g_settings.weaponCompat = static_cast<WeaponCompat>(val);
                    return val;
                },
            .defaultValue = 0,
        },
        {
            .name = L"DAOTransmog_UnlockMode",
            .label = Get(StrId::AppearanceUnlockMode, L"Appearance Unlock Mode"),
            .tooltip = L"",
            .values = {Get(StrId::AllAppearances, L"All Valid Appearances"), Get(StrId::UnlockedOnly, L"Unlocked Only")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.unlockMode); },
            .set =
                [](std::int32_t val) {
                    g_settings.unlockMode = static_cast<UnlockMode>(val);
                    return val;
                },
            .defaultValue = 0,
        },
        {
            .name = L"DAOTransmog_SceneRule",
            .label = Get(StrId::SceneRule, L"Scene Rule (Cutscenes & Dialogues)"),
            .tooltip = L"",
            .values = {Get(StrId::KeepTransmog, L"Keep Transmog"), Get(StrId::HideInDialogues, L"Hide Helmets Only"),
                       Get(StrId::ShowRealGear, L"Show Real Gear")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.sceneRule); },
            .set =
                [](std::int32_t val) {
                    g_settings.sceneRule = static_cast<SceneRule>(val);
                    return val;
                },
            .defaultValue = 0,
        },
        {
            .name = L"DAOTransmog_HelmetVisibility",
            .label = Get(StrId::HelmetVisibility, L"Helmet Visibility"),
            .tooltip = L"",
            .values = {Get(StrId::PerSlotSetting, L"Per-Slot Setting"),
                       Get(StrId::HideInDialogues, L"Hide in Dialogues"), Get(StrId::AlwaysHide, L"Always Hide"),
                       Get(StrId::AlwaysShow, L"Always Show")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.helmetRule); },
            .set =
                [](std::int32_t val) {
                    g_settings.helmetRule = static_cast<HelmetVisibilityRule>(val);
                    return val;
                },
            .defaultValue = 0,
        },
        {
            .name = L"DAOTransmog_BackWeapons",
            .label = Get(StrId::BackStrappedWeapons, L"Back-Strapped Weapons in Dialogue"),
            .tooltip = L"",
            .values = {Get(StrId::ShowVanilla, L"Show (Vanilla)"), Get(StrId::HideStaves2H, L"Hide Staves & 2H"),
                       Get(StrId::HideAll, L"Hide All")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.backWeaponRule); },
            .set =
                [](std::int32_t val) {
                    g_settings.backWeaponRule = static_cast<BackWeaponRule>(val);
                    return val;
                },
            .defaultValue = 0,
        },
        {
            .name = L"DAOTransmog_CampBehavior",
            .label = Get(StrId::CampBehavior, L"Party Camp Behavior"),
            .tooltip = L"",
            .values = {Get(StrId::KeepActive, L"Keep Active"),
                       Get(StrId::AutoSwitchCampPreset, L"Auto-Switch to Camp Preset (3)"),
                       Get(StrId::ShowRealGear, L"Suspend in Camp")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.campBehavior); },
            .set =
                [](std::int32_t val) {
                    g_settings.campBehavior = static_cast<CampBehavior>(val);
                    return val;
                },
            .defaultValue = 0,
        },
        {
            .name = L"DAOTransmog_RuneVfx",
            .label = Get(StrId::RuneVfx, L"Weapon Enchantment / Rune VFX"),
            .tooltip = L"",
            .values = {Get(StrId::PerSlotSetting, L"Per-Slot (Default: Show)"),
                       Get(StrId::SuppressOnTransmog, L"Suppress on Transmog"),
                       Get(StrId::AlwaysHide, L"Always Suppress")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.runeVfxRule); },
            .set =
                [](std::int32_t val) {
                    g_settings.runeVfxRule = static_cast<RuneVfxRule>(val);
                    return val;
                },
            .defaultValue = 0,
        },
        {
            .name = L"DAOTransmog_PortraitAppearance",
            .label = Get(StrId::PortraitAppearance, L"2D Mini-Portrait Appearance"),
            .tooltip = L"",
            .values = {Get(StrId::ShowTransmogSyncOnApply, L"Show Transmog (Sync on Apply)"),
                       Get(StrId::ShowRealEquippedGear, L"Show Real Equipped Gear")},
            .style = dao::options::ChoiceStyle::Dropdown,
            .get = [] { return static_cast<std::int32_t>(g_settings.portraitRule); },
            .set =
                [](std::int32_t val) {
                    g_settings.portraitRule = static_cast<PortraitSyncRule>(val);
                    return val;
                },
            .defaultValue = 0,
        },
    };

    dao::options::AddSection(Get(StrId::TransmogTitle, L"Transmog"), std::move(toggles), std::move(choices));

    // 2. Register Hotkeys in Options -> Controls
    dao::input::RegisterAction(
        {
            .name = L"DAOTransmog_ToggleHelmet",
            .label = Get(StrId::ToggleHelmetVis, L"Toggle Helmet Visibility"),
            .key = dao::input::Key::H,
            .section = Get(StrId::TransmogTitle, L"Transmog"),
        },
        [] {
            spdlog::info("[DAOTransmog] Hotkey: Toggle Helmet Visibility pressed");
            if (g_onToggleHelmet)
            {
                g_onToggleHelmet();
            }
        });

    dao::input::RegisterAction(
        {
            .name = L"DAOTransmog_CyclePreset",
            .label = Get(StrId::CyclePreset, L"Cycle Outfit Preset"),
            .key = dao::input::Key::P,
            .section = Get(StrId::TransmogTitle, L"Transmog"),
        },
        [] {
            spdlog::info("[DAOTransmog] Hotkey: Cycle Outfit Preset pressed");
            if (g_onCyclePreset)
            {
                g_onCyclePreset();
            }
        });

    dao::input::RegisterAction(
        {
            .name = L"DAOTransmog_ToggleTransmog",
            .label = Get(StrId::TransmogTitle, L"Toggle Transmog Menu"),
            .key = dao::input::Key::T,
            .modes = {dao::GameMode::Explore, dao::GameMode::Combat, dao::GameMode::Gui},
            .section = Get(StrId::TransmogTitle, L"Transmog"),
        },
        [] {
            spdlog::info("[DAOTransmog] Hotkey: Toggle Transmog Menu (T) pressed");
            if (g_onToggleTransmog)
            {
                g_onToggleTransmog();
            }
        });

    spdlog::info("[DAOTransmog] Config options and keybindings registered");
}

} // namespace transmog::config
