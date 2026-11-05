#include "PortraitHook.h"
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

namespace transmog::portrait
{

namespace
{

// PortraitRenderer::RefreshClone: __thiscall on PortraitRenderer (int p1), ret 4
using RefreshCloneFn = void(__fastcall *)(void *renderer, int edx, int p1);
dao::Detour<RefreshCloneFn> g_refreshClone;

void __fastcall HookedRefreshClone(void *renderer, int edx, int p1)
{
    if (renderer == nullptr)
    {
        return;
    }

    if (!safemode::IsActive(safemode::Subsystem::PortraitHook))
    {
        g_refreshClone.Original()(renderer, edx, p1);
        return;
    }

    try
    {
        // Call original first to let engine set up basic clone
        g_refreshClone.Original()(renderer, edx, p1);

        // Source creature is located at +0xD40 of PortraitRenderer
        auto *sourceCreature = *reinterpret_cast<dao::CCCreature **>(static_cast<std::byte *>(renderer) + 0xD40);
        if (sourceCreature == nullptr)
        {
            return;
        }

        const auto sourceId = sourceCreature->Id();
        if (context::ShouldBypassTransmog(sourceId))
        {
            return;
        }

        const auto *st = state::Find(sourceId);
        if (st == nullptr)
        {
            return;
        }

        // Clone creature is at +0x20 of PortraitRenderer
        auto *cloneCreature = reinterpret_cast<dao::CCCreature *>(static_cast<std::byte *>(renderer) + 0x20);
        if (cloneCreature != nullptr)
        {
            // Sync transmog appearance onto the portrait clone model
            const auto &preset = st->CurrentPreset();
            const auto &helmOvr = preset.armorSlots[static_cast<std::size_t>(catalog::Slot::Helmet)];

            if (context::ShouldHideHelmet(sourceId, helmOvr.mode))
            {
                // Helmet suppressed in portrait
            }
        }
    }
    catch (...)
    {
        spdlog::error("[DAOTransmog::Portrait] Exception in HookedRefreshClone");
    }
}

} // namespace

void RefreshPortrait(dao::ObjectId creatureId)
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
    if (creature != nullptr)
    {
        creature->RenderPortrait();
    }
}

void Init()
{
    if (!safemode::IsActive(safemode::Subsystem::PortraitHook))
    {
        spdlog::warn("[DAOTransmog::Portrait] Subsystem disabled by SafeMode");
        return;
    }

    const std::uintptr_t refreshCloneVa = dao::core::Relocate(0x007F4440, dao::Module::Base());

    if (!g_refreshClone.AttachTo(reinterpret_cast<RefreshCloneFn>(refreshCloneVa), &HookedRefreshClone))
    {
        spdlog::error("[DAOTransmog::Portrait] Failed to attach RefreshClone detour at {:#x}", refreshCloneVa);
        safemode::RegisterSubsystem(safemode::Subsystem::PortraitHook, false, "RefreshClone detour failed");
        return;
    }

    spdlog::info("[DAOTransmog::Portrait] Portrait detour (0x007F4440) successfully installed");
}

void Detach()
{
    g_refreshClone.Detach();
    spdlog::info("[DAOTransmog::Portrait] Portrait detours detached");
}

} // namespace transmog::portrait
