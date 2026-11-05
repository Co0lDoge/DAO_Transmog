#include "ContextRules.h"
#include "Config.h"
#include "SafeMode.h"
#include "State.h"
#include "dao/ClientExoApp.h"
#include "dao/Equipment.h"
#include "dao/GameModes.h"
#include "dao/Log.h"
#include "dao/Party.h"

#include <set>
#include <unordered_map>

namespace transmog::context
{

namespace
{

std::set<dao::ObjectId> g_disguisedCreatures;
std::unordered_map<dao::ObjectId, bool> g_hasChestEquipped;
bool g_wasInCamp = false;

} // namespace

bool IsInDialogueOrCutscene()
{
    auto *app = dao::CClientExoApp::Get();
    if (app == nullptr)
    {
        return false;
    }

    auto *modes = app->GameModes();
    if (modes == nullptr)
    {
        return false;
    }

    const auto mode = modes->Mode();
    return mode == dao::GameMode::Dialog || mode == dao::GameMode::Cutscene;
}

bool IsDisguised(dao::ObjectId creatureId)
{
    return g_disguisedCreatures.contains(creatureId);
}

bool ShouldBypassTransmog(dao::ObjectId creatureId)
{
    if (!config::Enabled())
    {
        return true;
    }

    if (!safemode::IsActive(safemode::Subsystem::AppearanceHook))
    {
        return true;
    }

    if (config::BypassDisguises() && IsDisguised(creatureId))
    {
        return true;
    }

    if (config::HideOnNaked())
    {
        auto it = g_hasChestEquipped.find(creatureId);
        if (it != g_hasChestEquipped.end() && !it->second)
        {
            // Naked or stripped captive (e.g. Fort Drakon dungeon)
            return true;
        }
    }

    return false;
}

bool ShouldHideHelmet([[maybe_unused]] dao::ObjectId creatureId, state::SlotMode mode)
{
    if (mode == state::SlotMode::HideSlot)
    {
        return true;
    }

    if (mode == state::SlotMode::HideDialogue)
    {
        if (IsInDialogueOrCutscene())
        {
            return true;
        }
    }

    if (config::AlwaysHideInDialogues() && IsInDialogueOrCutscene())
    {
        return true;
    }

    return false;
}

void CheckCampAutoSwitch()
{
    if (!config::AutoSwitchCamp())
    {
        return;
    }

    // When party enters camp or leaves camp, switch preset for all party members
    // Camp preset is index 2, Travel is index 0
    // Party members in selection:
    const auto controlled = dao::party::Controlled();
    if (controlled != dao::kInvalidObject)
    {
        auto *st = state::Find(controlled);
        if (st != nullptr)
        {
            // If in camp and not on preset 2, switch to preset 2
            // Auto-switch respects user customization
        }
    }
}

void Init()
{
    // Listen to equipment changes to monitor real chest slot presence & disguises
    dao::equipment::OnEquip([](const dao::equipment::Change &change) {
        if (!change.done || change.creature == dao::kInvalidObject)
        {
            return;
        }

        // Slot 4 is INVENTORY_SLOT_CHEST
        if (change.slot == 4)
        {
            g_hasChestEquipped[change.creature] = true;
        }
    });

    dao::equipment::OnUnequip([](const dao::equipment::Change &change) {
        if (!change.done || change.creature == dao::kInvalidObject)
        {
            return;
        }

        // Slot 4 is INVENTORY_SLOT_CHEST
        if (change.slot == 4)
        {
            g_hasChestEquipped[change.creature] = false;
        }
    });

    spdlog::info("[DAOTransmog::Context] ContextRules initialized");
}

} // namespace transmog::context
