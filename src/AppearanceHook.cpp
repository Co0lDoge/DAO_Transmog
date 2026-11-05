#include "AppearanceHook.h"
#include "Catalog.h"
#include "Config.h"
#include "ContextRules.h"
#include "SafeMode.h"
#include "State.h"
#include "dao/ClientExoApp.h"
#include "dao/Hook.h"
#include "dao/Log.h"
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

    // Native CAppearance and Equipment helper functions
    using GetItemInSlotFn = void *(__fastcall *)(void *slots, int edx, int invSlot);
    auto getItemInSlot = reinterpret_cast<GetItemInSlotFn>(dao::core::Relocate(0x007F4EA0, dao::Module::Base()));

    using SetSlotFromItemFn = void(__fastcall *)(void *appr, int edx, int invSlot, void *item);
    auto setSlotFromItem = reinterpret_cast<SetSlotFromItemFn>(dao::core::Relocate(0x00748DA0, dao::Module::Base()));

    // Equipment pointer at creature + 0xBE0 + 0xC; SlotItemLocation is at equipment + 0x24
    void *equipment = nullptr;
    void *slots = nullptr;
    if (targetCreature != nullptr)
    {
        equipment = *reinterpret_cast<void **>(reinterpret_cast<std::byte *>(targetCreature) + 0xBE0 + 0xC);
        if (equipment != nullptr)
        {
            slots = static_cast<std::byte *>(equipment) + 0x24;
        }
    }

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

    auto &snapshot = g_vanillaSnapshots[targetId];

    // For all 7 appearance slots:
    // 1. First restore baseline from actual equipped item if available, or snapshot if available
    for (std::size_t i = 0; i < 7; ++i)
    {
        const int invSlot = kSlotToInvSlot[i];

        // Step 1: Query actual real equipped item from SlotItemLocation (equipment + 0x24)
        void *realItem = nullptr;
        if (slots != nullptr)
        {
            realItem = getItemInSlot(slots, 0, invSlot);
        }

        // Script API fallback if SlotItemLocation query returned null
        if (realItem == nullptr && targetId != dao::kInvalidObject)
        {
            const auto realItemId = dao::script::api::GetItemInEquipSlot(invSlot, targetId);
            if (realItemId != dao::kInvalidObject)
            {
                realItem = app->GetObjectById(realItemId);
            }
        }

        if (realItem != nullptr)
        {
            setSlotFromItem(appearance, 0, invSlot, realItem);

            // Update vanilla snapshot from live equipped item
            snapshot.baseItem[i] = baseItemWords[i];
            snapshot.variation[i] = variationBytes[i];
            snapshot.heraldry[i] = heraldryBytes[i];
            snapshot.material[i] = materialDwords[i];
            snapshot.tint[i] = tintDwords[i];
            snapshot.valid = true;
        }
        else if (preset != nullptr && i < preset->armorSlots.size() &&
                 preset->armorSlots[i].mode == state::SlotMode::Real && snapshot.valid)
        {
            // Restore from known vanilla snapshot if live item lookup was empty
            baseItemWords[i] = snapshot.baseItem[i];
            variationBytes[i] = snapshot.variation[i];
            heraldryBytes[i] = snapshot.heraldry[i];
            materialDwords[i] = snapshot.material[i];
            tintDwords[i] = snapshot.tint[i];
        }

        // Step 2: Apply transmog override if present
        if (preset != nullptr && i < preset->armorSlots.size())
        {
            const auto &ovr = preset->armorSlots[i];
            if (ovr.mode == state::SlotMode::Override)
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
            else if (ovr.mode == state::SlotMode::HideSlot)
            {
                baseItemWords[i] = 0xFFFF;
                variationBytes[i] = 0xFF;
            }
            else if (ovr.mode == state::SlotMode::HideDialogue)
            {
                if (context::ShouldHideHelmet(targetId, ovr.mode))
                {
                    baseItemWords[i] = 0xFFFF;
                    variationBytes[i] = 0xFF;
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
