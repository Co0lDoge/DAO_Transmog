#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace transmog::catalog
{

// Engine appearance slots (matching dao::core::character::Slot)
enum class Slot : std::uint8_t
{
    Chest = 0,
    Gloves = 1,
    Boots = 2,
    Main = 3,
    Offhand = 4,
    Helmet = 5,
    Cloak = 6,
};

// Race & Gender bitmask flags for humanoid models
namespace RaceGender
{
inline constexpr std::uint8_t HM = 1 << 0; // Human Male
inline constexpr std::uint8_t HF = 1 << 1; // Human Female
inline constexpr std::uint8_t EM = 1 << 2; // Elf Male
inline constexpr std::uint8_t EF = 1 << 3; // Elf Female
inline constexpr std::uint8_t DM = 1 << 4; // Dwarf Male
inline constexpr std::uint8_t DF = 1 << 5; // Dwarf Female
inline constexpr std::uint8_t QM = 1 << 6; // Qunari Male
inline constexpr std::uint8_t QF = 1 << 7; // Qunari Female
inline constexpr std::uint8_t All = 0xFF;
} // namespace RaceGender

struct CatalogEntry
{
    std::uint32_t id = 0; // Sequential UI index
    std::uint16_t baseItem = 0;
    std::uint8_t variation = 0;
    Slot slot = Slot::Chest;

    std::wstring label;
    std::wstring name; // Localized item/category display name
    std::wstring icon; // Texture name, e.g. "ico_arm_ch_a" or "img://..."
    std::wstring meshStem; // Model stem without prefix/extension, e.g. "arm_medd"

    std::uint8_t defaultMaterial = 1;
    bool hasHeraldry = false;
    std::uint8_t raceGenderMask = 0; // Which races/genders have a valid .mmh

    std::int32_t weaponWield = 0; // 1H, 2H, etc.
    std::int32_t itemType = 0;    // 1 Shield, 2 Melee, 3 Bow, 4 Ammo, 5 Staff, 6 Armor
    std::int32_t armorType = 0;   // 0 Clothing, 1 Light, 2 Med, 3 Heavy, 4 Massive
    bool fullFace = false;        // For helmets: hides face/hair/beard
};

struct MaterialEntry
{
    std::uint8_t id = 0;
    std::wstring label;
    std::wstring name;
    std::wstring tintFile;
};

struct HeraldryEntry
{
    std::uint32_t id = 0;
    std::wstring label;
    std::wstring textureFile;
};

// Functions to test resource existence and read tables (pluggable for tests)
using ExistsFn = std::function<bool(std::wstring_view filename)>;
using GetIntFn = std::function<std::optional<std::int32_t>(std::int32_t table, std::int32_t row, std::string_view column)>;
using GetStringFn = std::function<std::optional<std::wstring>(std::int32_t table, std::int32_t row, std::string_view column)>;

// Scans runtime game GDA tables (or custom providers)
void Scan();
void ScanWith(GetIntFn getInt, GetStringFn getString, ExistsFn exists);
bool EnsureScanned();

bool IsScanned();
std::size_t TotalEntries();

// Slot accessors
std::span<const CatalogEntry> EntriesForSlot(Slot slot);
const CatalogEntry *Find(Slot slot, std::uint16_t baseItem, std::uint8_t variation);

// Filtered list for UI display
std::vector<const CatalogEntry *> FilteredEntries(
    Slot slot,
    std::uint8_t charRaceGenderMask,
    std::int32_t subcatFilter,
    std::int32_t realWeaponWield,
    std::int32_t realItemType,
    bool strictGripMatching);

// Materials & Heraldry
std::span<const MaterialEntry> Materials();
std::span<const HeraldryEntry> Heraldries();
const MaterialEntry *FindMaterial(std::uint8_t id);
const HeraldryEntry *FindHeraldry(std::uint32_t id);

// Helper functions for stem construction & race/gender masks
std::wstring BuildMeshStem(std::wstring_view modelType, std::wstring_view modelSubType, std::wstring_view modelVariation);
std::uint8_t PrefixToMask(std::wstring_view prefix);
std::wstring_view MaskToPrefix(std::uint8_t singleBitMask);

} // namespace transmog::catalog
