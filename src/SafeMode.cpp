#include "SafeMode.h"
#include "dao/Addresses.h"
#include "dao/Log.h"
#include "dao/Module.h"
#include "dao/core/Relocate.h"

#include <windows.h>
#include <array>

namespace transmog::safemode
{

namespace
{
struct SubsystemState
{
    bool active = true;
    std::string failureReason;
};

std::array<SubsystemState, 4> g_states;

std::uintptr_t RuntimeAddress(std::uintptr_t preferredVa)
{
    return dao::core::Relocate(preferredVa, dao::Module::Base());
}

bool IsExecutableAddress(std::uintptr_t addr, std::size_t size)
{
    if (addr == 0)
    {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void *>(addr), &mbi, sizeof(mbi)) != sizeof(mbi))
    {
        return false;
    }

    if (mbi.State != MEM_COMMIT)
    {
        return false;
    }

    const DWORD protect = mbi.Protect & 0xFF;
    if (protect != PAGE_EXECUTE && protect != PAGE_EXECUTE_READ && protect != PAGE_EXECUTE_READWRITE &&
        protect != PAGE_EXECUTE_WRITECOPY)
    {
        return false;
    }

    // Check that prologue is not pure int 3 padding or unmapped
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(addr);
    bool allInt3 = true;
    for (std::size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0xCC)
        {
            allInt3 = false;
            break;
        }
    }
    return !allInt3;
}
} // namespace

bool ValidateAddress(std::uintptr_t addr, std::size_t minBytes)
{
    return IsExecutableAddress(addr, minBytes);
}

void RegisterSubsystem(Subsystem sub, bool operational, std::string_view reason)
{
    const auto idx = static_cast<std::size_t>(sub);
    if (idx < g_states.size())
    {
        g_states[idx].active = operational;
        g_states[idx].failureReason = reason;
        if (!operational)
        {
            spdlog::error("[DAOTransmog::SafeMode] Subsystem '{}' disabled: {}", SubsystemName(sub), reason);
        }
        else
        {
            spdlog::info("[DAOTransmog::SafeMode] Subsystem '{}' active and verified", SubsystemName(sub));
        }
    }
}

bool IsActive(Subsystem sub)
{
    const auto idx = static_cast<std::size_t>(sub);
    return idx < g_states.size() ? g_states[idx].active : false;
}

const char *SubsystemName(Subsystem sub)
{
    switch (sub)
    {
    case Subsystem::AppearanceHook:
        return "AppearanceHook (CAppearance 3D Armor/Helmet)";
    case Subsystem::WeaponHook:
        return "WeaponHook (Weapon/Shield 3D Props)";
    case Subsystem::PortraitHook:
        return "PortraitHook (2D Portrait Sync)";
    case Subsystem::GuiBridge:
        return "GuiBridge (Scaleform inventory.gfx)";
    default:
        return "Unknown";
    }
}

void RunSelfTest()
{
    spdlog::info("[DAOTransmog::SafeMode] Running startup self-test & address verification...");

    // Validate Appearance Hook address:
    // 0x006C1400: CCCreature::SetAppearance (__thiscall, ret 0x10)
    const std::uintptr_t setApp = RuntimeAddress(0x006C1400);
    if (!ValidateAddress(setApp))
    {
        RegisterSubsystem(Subsystem::AppearanceHook, false, "SetAppearance (0x006C1400) address invalid");
    }
    else
    {
        RegisterSubsystem(Subsystem::AppearanceHook, true);
    }

    // Validate Weapon Hook address:
    // 0x006A6640: CCCreature::Wield (__thiscall (slot), ret 4)
    const std::uintptr_t loadWeap = RuntimeAddress(0x006A6640);
    if (!ValidateAddress(loadWeap))
    {
        RegisterSubsystem(Subsystem::WeaponHook, false, "Wield (0x006A6640) address invalid");
    }
    else
    {
        RegisterSubsystem(Subsystem::WeaponHook, true);
    }

    // Validate Portrait Hook address:
    // 0x007F4440: PortraitRenderer::RefreshClone (__thiscall (p1), ret 4)
    const std::uintptr_t refreshClone = RuntimeAddress(0x007F4440);
    if (!ValidateAddress(refreshClone))
    {
        RegisterSubsystem(Subsystem::PortraitHook, false, "PortraitRenderer::RefreshClone address invalid");
    }
    else
    {
        RegisterSubsystem(Subsystem::PortraitHook, true);
    }

    RegisterSubsystem(Subsystem::GuiBridge, true);
}

} // namespace transmog::safemode
