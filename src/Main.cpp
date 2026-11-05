#include "AppearanceHook.h"
#include "Catalog.h"
#include "Config.h"
#include "ContextRules.h"
#include "GuiBridge.h"
#include "PortraitHook.h"
#include "SafeMode.h"
#include "State.h"
#include "Strings.h"
#include "WeaponHook.h"
#include "dao/ClientExoApp.h"
#include "dao/Events.h"
#include "dao/Frame.h"
#include "dao/GameModes.h"
#include "dao/Log.h"
#include "dao/Party.h"
#include "dao/Plugin.h"
#include "dao/generated/ScriptApi.h"

extern "C" __declspec(dllexport) bool __cdecl DAOLib_Load(const dao::LoaderInfo *info)
{
    dao::OpenLog("DAOTransmog");

    spdlog::info("==================================================");
    spdlog::info("DAOTransmog v1.0.0 initializing...");
    spdlog::info("==================================================");

    if (info == nullptr || info->apiVersion != dao::kPluginApiVersion || !info->knownExecutable)
    {
        spdlog::error("[DAOTransmog] Loader API mismatch or unknown game executable! Aborting load.");
        return false;
    }

    spdlog::info("[DAOTransmog] Loader API version: {:#x}, Known executable: {}", info->apiVersion,
                 info->knownExecutable);

    // 1. Initialize Localization
    transmog::strings::Init(*info);

    // 2. Run SafeMode Address Validation & Prologue Self-Test
    transmog::safemode::RunSelfTest();

    // 3. Register Config Options & Hotkeys
    transmog::config::Init();

    // 4. Initialize State & Co-Save Listeners
    transmog::state::Init();

    // 5. Initialize Context Rules & Event Listeners
    transmog::context::Init();

    // 6. Install Visual Detours (Appearance, Weapon, Portrait)
    transmog::appearance::Init();
    transmog::weapon::Init();
    transmog::portrait::Init();

    // 7. Initialize Scaleform GFx UI Bridge & Hotkeys
    transmog::gui::Init();

    // 8. Automatically refresh 3D appearance in world after load or area transition
    dao::OnFrame([] {
        if (!transmog::state::NeedsWorldRefresh())
        {
            return;
        }

        const auto mode = dao::CClientExoApp::CurrentMode();
        if (!mode || *mode == dao::GameMode::Loading || *mode == dao::GameMode::Pregame)
        {
            return; // Still in loading screen or main menu
        }

        const dao::ObjectId heroId = dao::script::api::GetHero();
        const dao::ObjectId controlledId = dao::party::Controlled();
        const dao::ObjectId mainChar = (heroId != dao::kInvalidObject) ? heroId : controlledId;

        if (mainChar == dao::kInvalidObject)
        {
            return;
        }

        auto *app = dao::CClientExoApp::Get();
        if (app == nullptr || app->GetObjectById(mainChar) == nullptr)
        {
            return; // 3D creature not yet spawned in world
        }

        transmog::state::SetNeedsWorldRefresh(false);
        transmog::state::ResolvePending(mainChar);
        transmog::appearance::RefreshAppearance(mainChar, true);

        // Also refresh party members if present
        const auto party = dao::script::api::GetPartyList(mainChar);
        for (std::size_t i = 0; i < party.Size(); ++i)
        {
            const dao::ObjectId member = party.Object(i);
            if (member != dao::kInvalidObject && member != mainChar)
            {
                transmog::state::ResolvePending(member);
                transmog::appearance::RefreshAppearance(member, true);
            }
        }

        spdlog::info(
            "[DAOTransmog::Appearance] Automatically applied transmog appearance in world after load (hero: {:#x})",
            mainChar);
    });

    // 9. On control changed (e.g. switching between party members in explore/combat)
    dao::party::OnControlChanged([](dao::ObjectId /*before*/, dao::ObjectId now) {
        if (now != dao::kInvalidObject)
        {
            transmog::state::ResolvePending(now);
            transmog::appearance::RefreshAppearance(now, true);
        }
    });

    // 10. On area transition (door / world map), queue appearance refresh
    dao::events::OnScriptEvent(dao::script::EventType::ModuleLoad, [](dao::ObjectId, const dao::CScriptEvent &) {
        transmog::state::SetNeedsWorldRefresh(true);
    });

    spdlog::info("[DAOTransmog] Initialization complete. SafeMode active.");
    return true;
}
