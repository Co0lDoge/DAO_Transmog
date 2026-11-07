#include "AppearanceHook.h"
#include "Catalog.h"
#include "Config.h"
#include "ContextRules.h"
#include "SafeMode.h"
#include "State.h"
#include "dao/Addresses.h"
#include "dao/ClientExoApp.h"
#include "dao/Hook.h"
#include "dao/Log.h"
#include "dao/Module.h"
#include "dao/Objects.h"
#include "dao/Party.h"
#include "dao/core/Relocate.h"
#include "dao/generated/ScriptApi.h"

namespace transmog::appearance
{

namespace
{

// CCCreature::SetAppearance: __thiscall on CCCreature (CAppearance *app, int keepCopy, int skipFull, int force), ret 0x10
using SetAppearanceFn = bool(__fastcall *)(void *creature, int edx, void *appearance, int a1, int a2, int a3);
dao::Detour<SetAppearanceFn> g_setAppearance;

// CAppearance slot setters & equipment access
// Appearance slot 0 (Chest)    -> Inventory slot 4
// Appearance slot 1 (Gloves)   -> Inventory slot 7
// Appearance slot 2 (Boots)    -> Inventory slot 6
// Appearance slot 3 (Main)     -> Inventory slot 0
// Appearance slot 4 (Offhand)  -> Inventory slot 1
// Appearance slot 5 (Helmet)   -> Inventory slot 5
// Appearance slot 6 (Cloak)    -> Inventory slot 8
constexpr int kSlotToInvSlot[7] = {4, 7, 6, 0, 1, 5, 8};

struct VanillaAppearanceSnapshot
{
    std::uint16_t baseItem[7]{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    std::uint8_t variation[7]{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    std::uint8_t heraldry[7]{0, 0, 0, 0, 0, 0, 0};
    std::uint32_t material[7]{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    std::uint32_t tint[7]{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    bool valid = false;
};

std::unordered_map<dao::ObjectId, VanillaAppearanceSnapshot> g_vanillaSnapshots;

void ApplyTransmogToAppearance(dao::ObjectId targetId, void *creature, void *appearance)
{
    if (appearance == nullptr)
    {
        return;
    }

    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr)
    {
        return;
    }

    // Canine creatures (Dog) do not use humanoid armor slots (0..6)
    if (targetId != dao::kInvalidObject)
    {
        const auto appType = dao::script::api::GetAppearanceType(targetId);
        const auto racial = dao::script::api::GetCreatureRacialType(targetId);
        if (appType == 35 || racial == 5)
        {
            return;
        }
    }

    // Only apply transmog / revert authentic appearance on party members or the hero.
    // Non-party creatures (NPCs, enemies) should not have their appearance touched.
    if (targetId != dao::kInvalidObject && !state::IsPartyMemberOrHero(targetId))
    {
        return;
    }

    // Resolve the real creature pointer for equipment queries
    void *targetCreature = creature;
    if (targetId != dao::kInvalidObject)
    {
        auto *found = app->GetCreatureById(targetId);
        if (found != nullptr)
        {
            targetCreature = found;
        }
    }

    // Engine function types for querying equipped items:
    // SmartData::Attribute::Get (Attribute at creature + 0xBE0 returns Equipment holder)
    // SlotItemLocation::ItemIn (holder + 0x24 returns CCItem pointer or 0)
    using GetAttrFn = std::uintptr_t(__fastcall *)(std::uintptr_t, int);
    using ItemInFn = std::uintptr_t(__fastcall *)(std::uintptr_t, int, std::int32_t);

    const auto getAttrAddr = dao::Module::Address(dao::addr::kSmartDataAttribute_Get);
    const auto itemInAddr = dao::Module::Address(dao::addr::kSlotItemLocation_ItemIn);

    // CAppearance struct offsets:
    // +0x12A: unsigned short slotBaseItem[7]
    // +0x138: unsigned char slotVariation[7]
    // +0x13F: unsigned char slotHeraldry[7]
    // +0x148: unsigned int slotMaterialType[7] (4-byte dwords)
    // +0x164: unsigned int slotTintOverride[7] (4-byte dwords)
    auto *baseItemWords = reinterpret_cast<std::uint16_t *>(static_cast<std::byte *>(appearance) + 0x12A);
    auto *variationBytes = reinterpret_cast<std::uint8_t *>(static_cast<std::byte *>(appearance) + 0x138);
    auto *heraldryBytes = reinterpret_cast<std::uint8_t *>(static_cast<std::byte *>(appearance) + 0x13F);
    auto *materialDwords = reinterpret_cast<std::uint32_t *>(static_cast<std::byte *>(appearance) + 0x148);
    auto *tintDwords = reinterpret_cast<std::uint32_t *>(static_cast<std::byte *>(appearance) + 0x164);

    const auto *st = state::Find(targetId);
    const auto *preset = (st != nullptr) ? &st->CurrentPreset() : nullptr;

    for (std::size_t i = 0; i < 7; ++i)
    {
        const int invSlot = kSlotToInvSlot[i];

        std::uintptr_t realItem = 0;
        bool hasEquipmentHolder = false;
        if (targetCreature != nullptr && getAttrAddr != 0 && itemInAddr != 0)
        {
            const std::uintptr_t holder =
                reinterpret_cast<GetAttrFn>(getAttrAddr)(reinterpret_cast<std::uintptr_t>(targetCreature) + 0xBE0, 0);
            if (holder != 0)
            {
                hasEquipmentHolder = true;
                realItem = reinterpret_cast<ItemInFn>(itemInAddr)(holder + 0x24, 0, invSlot);
            }
        }

        // Script API fallback if direct member query returned 0 and target is valid
        if (realItem == 0 && targetId != dao::kInvalidObject)
        {
            const auto scriptItemId = dao::script::api::GetItemInEquipSlot(invSlot, targetId);
            if (scriptItemId != dao::kInvalidObject && app->GetObjectById(scriptItemId) != nullptr)
            {
                realItem = reinterpret_cast<std::uintptr_t>(app->GetObjectById(scriptItemId));
            }
        }

        // A slot is genuinely unequipped if the creature has an equipment container but this slot is empty (0)
        const bool genuineUnequipped = hasEquipmentHolder && (realItem == 0);

        // An item is considered equipped if realItem is present OR if vanilla appearance already had an item
        // (crucial for preview paperdoll and initial load frames where equipment structure is detached/transitioning)
        const bool isEquipped = genuineUnequipped ? false : ((realItem != 0) || (baseItemWords[i] != 0xFFFF));
        const bool allowPreview = (st != nullptr && st->hasStagedChanges);
        const bool canDisplay = !config::RequireEquipped() || isEquipped || allowPreview;

        state::SlotOverride defaultReal;
        defaultReal.mode = state::SlotMode::Real;
        const auto &ovr = (preset != nullptr && i < preset->armorSlots.size()) ? preset->armorSlots[i] : defaultReal;

        if (ovr.mode == state::SlotMode::Real)
        {
            // Revert slot to real equipped item's authentic appearance via engine CAppearance::ApplyItem
            if (realItem != 0)
            {
                using ApplyItemFn = void(__thiscall *)(void *appearance, int invSlot, void *item);
                auto applyItem = reinterpret_cast<ApplyItemFn>(dao::core::Relocate(0x00748da0, dao::Module::Base()));
                applyItem(appearance, invSlot, reinterpret_cast<void *>(realItem));
            }
            else if (hasEquipmentHolder)
            {
                baseItemWords[i] = 0xFFFF;
                variationBytes[i] = 0xFF;
                heraldryBytes[i] = 0;
                materialDwords[i] = 0xFFFFFFFF;
                tintDwords[i] = 0xFFFFFFFF;
            }
            spdlog::info("[DAOTransmog::Appearance] Slot {} (invSlot {}): mode=Real, targetId={:#x}, realItem={:#x}, baseItem={}",
                         i, invSlot, targetId, realItem, baseItemWords[i]);
            continue;
        }
        else if (ovr.mode == state::SlotMode::Override)
        {
            if (canDisplay)
            {
                if (ovr.baseItem != 0xFFFF)
                {
                    baseItemWords[i] = ovr.baseItem;
                }
                if (ovr.variation != 0xFF)
                {
                    variationBytes[i] = ovr.variation;
                }
                if (ovr.material != 0xFF)
                {
                    materialDwords[i] = static_cast<std::uint32_t>(ovr.material);
                }
                if (ovr.heraldry != 0)
                {
                    heraldryBytes[i] = static_cast<std::uint8_t>(ovr.heraldry);
                }
            }
            else
            {
                // Slot is unequipped and RequireEquipped() is true: do NOT show transmog!
                baseItemWords[i] = 0xFFFF;
                variationBytes[i] = 0xFF;
                heraldryBytes[i] = 0;
                materialDwords[i] = 0xFFFFFFFF;
                tintDwords[i] = 0xFFFFFFFF;
            }
        }
        else if (ovr.mode == state::SlotMode::HideSlot)
        {
            baseItemWords[i] = 0xFFFF;
            variationBytes[i] = 0xFF;
            heraldryBytes[i] = 0;
            materialDwords[i] = 0xFFFFFFFF;
            tintDwords[i] = 0xFFFFFFFF;
        }
        else if (ovr.mode == state::SlotMode::HideDialogue)
        {
            if (context::ShouldHideHelmet(targetId, ovr.mode))
            {
                baseItemWords[i] = 0xFFFF;
                variationBytes[i] = 0xFF;
                heraldryBytes[i] = 0;
                materialDwords[i] = 0xFFFFFFFF;
                tintDwords[i] = 0xFFFFFFFF;
            }
            else if (ovr.baseItem != 0xFFFF && ovr.baseItem != 0)
            {
                if (canDisplay)
                {
                    baseItemWords[i] = ovr.baseItem;
                    if (ovr.variation != 0xFF)
                    {
                        variationBytes[i] = ovr.variation;
                    }
                    if (ovr.material != 0xFF)
                    {
                        materialDwords[i] = static_cast<std::uint32_t>(ovr.material);
                    }
                    if (ovr.heraldry != 0)
                    {
                        heraldryBytes[i] = static_cast<std::uint8_t>(ovr.heraldry);
                    }
                }
                else
                {
                    baseItemWords[i] = 0xFFFF;
                    variationBytes[i] = 0xFF;
                    heraldryBytes[i] = 0;
                    materialDwords[i] = 0xFFFFFFFF;
                    tintDwords[i] = 0xFFFFFFFF;
                }
            }
        }
    }

    // Cross-slot Robe (baseItem == 38): suppress gloves (1) and boots (2)
    if (baseItemWords[0] == 38)
    {
        baseItemWords[1] = 0xFFFF;
        variationBytes[1] = 0xFF;
        baseItemWords[2] = 0xFFFF;
        variationBytes[2] = 0xFF;
    }
}

bool __fastcall HookedSetAppearance(void *creature, int edx, void *appearance, int a1, int a2, int a3)
{
    if (creature == nullptr || appearance == nullptr)
    {
        return g_setAppearance.Original()(creature, edx, appearance, a1, a2, a3);
    }

    if (!safemode::IsActive(safemode::Subsystem::AppearanceHook))
    {
        return g_setAppearance.Original()(creature, edx, appearance, a1, a2, a3);
    }

    try
    {
        const auto *obj = static_cast<const dao::CGameObject *>(creature);
        auto id = obj->Id();

        // If this is the inventory paperdoll creature (id == -1), apply transmog of active screen character
        if (id == dao::kInvalidObject)
        {
            id = dao::party::ScreenCharacter();
            if (id == dao::kInvalidObject)
            {
                id = dao::party::Controlled();
            }
        }

        if (id != dao::kInvalidObject && !context::ShouldBypassTransmog(id))
        {
            ApplyTransmogToAppearance(id, creature, appearance);
        }
    }
    catch (...)
    {
        spdlog::error("[DAOTransmog::Appearance] Exception in HookedSetAppearance");
    }

    return g_setAppearance.Original()(creature, edx, appearance, a1, a2, a3);
}

void RefreshInventoryPaperdoll(void *creature)
{
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr || creature == nullptr)
    {
        return;
    }

    auto *guiSystem = *reinterpret_cast<void **>(reinterpret_cast<std::byte *>(app) + 0x158);
    if (guiSystem == nullptr)
    {
        return;
    }

    // Call GUISystem::Scene(8) to get GUIInventory scene
    using GetSceneFn = void *(__thiscall *)(void *sys, int sceneId);
    auto getScene = reinterpret_cast<GetSceneFn>(dao::core::Relocate(0x00523FD0, dao::Module::Base()));
    void *invScene = getScene(guiSystem, 8);
    if (invScene == nullptr)
    {
        return;
    }

    // +0xC0 in GUIInventory is the GUIPaperdoll pointer
    void *paperdoll = *reinterpret_cast<void **>(reinterpret_cast<std::byte *>(invScene) + 0xC0);
    if (paperdoll == nullptr)
    {
        return;
    }

    // Call GUIPaperdoll::Refresh(creature, 0, 0)
    using RefreshFn = void(__thiscall *)(void *pd, void *cr, int a2, int a3);
    auto refreshPd = reinterpret_cast<RefreshFn>(dao::core::Relocate(0x007B8470, dao::Module::Base()));
    refreshPd(paperdoll, creature, 0, 0);
}

} // namespace

void RefreshAppearance(dao::ObjectId creatureId, bool fullRebuild)
{
    if (creatureId == dao::kInvalidObject)
    {
        return;
    }

    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr)
    {
        return;
    }

    auto *creature = app->GetCreatureById(creatureId);
    if (creature != nullptr && g_setAppearance.Original() != nullptr)
    {
        auto *curAppearance = *reinterpret_cast<void **>(reinterpret_cast<std::byte *>(creature) + 0x7B0);
        if (curAppearance != nullptr)
        {
            if (fullRebuild)
            {
                // Full 3D world model rebuild (skipFull = 0, force = 1)
                // Save and restore exact creature world position (0x120) and orientation (0x130)
                // to prevent engine position drift during entity reinstantiation
                alignas(16) float savedPos[4]{0.0f, 0.0f, 0.0f, 0.0f};
                alignas(16) float savedRot[4]{0.0f, 0.0f, 0.0f, 0.0f};
                std::memcpy(savedPos, reinterpret_cast<std::byte *>(creature) + 0x120, 16);
                std::memcpy(savedRot, reinterpret_cast<std::byte *>(creature) + 0x130, 16);

                HookedSetAppearance(creature, 0, curAppearance, 0, 0, 1);

                std::memcpy(reinterpret_cast<std::byte *>(creature) + 0x120, savedPos, 16);
                std::memcpy(reinterpret_cast<std::byte *>(creature) + 0x130, savedRot, 16);
            }
            else
            {
                // Lightweight preview update (skipFull = 1, force = 1)
                // Updates appearance struct & paperdoll without resetting creature physics
                HookedSetAppearance(creature, 0, curAppearance, 0, 1, 1);
            }
        }

        // 2. Also refresh the GUI Inventory paperdoll preview
        RefreshInventoryPaperdoll(creature);
    }
}

void Init()
{
    if (!safemode::IsActive(safemode::Subsystem::AppearanceHook))
    {
        spdlog::warn("[DAOTransmog::Appearance] Subsystem disabled by SafeMode");
        return;
    }

    const std::uintptr_t setAppVa = dao::core::Relocate(0x006C1400, dao::Module::Base());

    if (!g_setAppearance.AttachTo(reinterpret_cast<SetAppearanceFn>(setAppVa), &HookedSetAppearance))
    {
        spdlog::error("[DAOTransmog::Appearance] Failed to attach SetAppearance detour at {:#x}", setAppVa);
        safemode::RegisterSubsystem(safemode::Subsystem::AppearanceHook, false, "SetAppearance detour failed");
        return;
    }

    spdlog::info("[DAOTransmog::Appearance] Appearance detour (0x006C1400) successfully installed");
}

void Detach()
{
    g_setAppearance.Detach();
    spdlog::info("[DAOTransmog::Appearance] Appearance detours detached");
}

} // namespace transmog::appearance
