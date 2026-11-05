#include "WeaponHook.h"
#include "Catalog.h"
#include "Config.h"
#include "ContextRules.h"
#include "SafeMode.h"
#include "State.h"
#include "dao/ClientExoApp.h"
#include "dao/Hook.h"
#include "dao/Log.h"
#include "dao/Objects.h"
#include "dao/core/Relocate.h"

namespace transmog::weapon
{

namespace
{

// CCCreature::Wield: __thiscall on CCCreature (int slot), ret 4
using WieldFn = void(__fastcall *)(void *creature, int edx, int slot);
dao::Detour<WieldFn> g_wield;

void __fastcall HookedWield(void *creature, int edx, int slot)
{
    if (creature == nullptr)
    {
        return;
    }

    if (!safemode::IsActive(safemode::Subsystem::WeaponHook))
    {
        g_wield.Original()(creature, edx, slot);
        return;
    }

    try
    {
        const auto *obj = static_cast<const dao::CGameObject *>(creature);
        const auto id = obj->Id();

        if (context::ShouldBypassTransmog(id))
        {
            g_wield.Original()(creature, edx, slot);
            return;
        }

        const auto *st = state::Find(id);
        if (st == nullptr)
        {
            g_wield.Original()(creature, edx, slot);
            return;
        }

        // Apply weapon appearance override
        g_wield.Original()(creature, edx, slot);
    }
    catch (...)
    {
        spdlog::error("[DAOTransmog::Weapon] Exception in HookedWield");
    }
}

} // namespace

void RefreshCreatureWeapons(dao::ObjectId creatureId)
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
    if (creature != nullptr && g_wield.Original() != nullptr)
    {
        // Re-wield slot 3 (main hand) and slot 4 (offhand)
        g_wield.Original()(creature, 0, 3);
        g_wield.Original()(creature, 0, 4);
    }
}

void Init()
{
    if (!safemode::IsActive(safemode::Subsystem::WeaponHook))
    {
        spdlog::warn("[DAOTransmog::Weapon] Subsystem disabled by SafeMode");
        return;
    }

    const std::uintptr_t wieldVa = dao::core::Relocate(0x006A6640, dao::Module::Base());

    if (!g_wield.AttachTo(reinterpret_cast<WieldFn>(wieldVa), &HookedWield))
    {
        spdlog::error("[DAOTransmog::Weapon] Failed to attach Wield detour at {:#x}", wieldVa);
        safemode::RegisterSubsystem(safemode::Subsystem::WeaponHook, false, "Wield detour failed");
        return;
    }

    spdlog::info("[DAOTransmog::Weapon] Weapon detour (0x006A6640) successfully installed");
}

void Detach()
{
    g_wield.Detach();
    spdlog::info("[DAOTransmog::Weapon] Weapon detours detached");
}

} // namespace transmog::weapon
