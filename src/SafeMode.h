#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace transmog::safemode
{

enum class Subsystem : std::uint8_t
{
    AppearanceHook,
    WeaponHook,
    PortraitHook,
    GuiBridge,
};

bool ValidateAddress(std::uintptr_t addr, std::size_t minBytes = 5);
void RegisterSubsystem(Subsystem sub, bool operational, std::string_view reason = {});
bool IsActive(Subsystem sub);
const char *SubsystemName(Subsystem sub);

// Called once during startup to check all critical addresses
void RunSelfTest();

} // namespace transmog::safemode
