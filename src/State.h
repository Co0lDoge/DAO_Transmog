#pragma once

#include "Catalog.h"
#include "dao/ObjectId.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace transmog::state
{

enum class SlotMode : std::uint8_t
{
    Real = 0,         // Show real equipped item
    Override = 1,     // Override with transmog item
    HideSlot = 2,     // Hide slot (bare body, no helmet, etc.)
    HideDialogue = 3, // For helmets: show in combat/explore, hide in dialogues
};

struct SlotOverride
{
    SlotMode mode = SlotMode::Real;
    std::uint16_t baseItem = 0xFFFF;
    std::uint8_t variation = 0xFF;
    std::uint8_t material = 0xFF; // 0xFF = use real equipped item's material
    std::uint32_t heraldry = 0;   // 0 = use real equipped item's heraldry
    bool suppressRuneVfx = false;

    bool operator==(const SlotOverride &) const = default;
};

struct WeaponSetOverride
{
    SlotOverride mainHand;
    SlotOverride offhandWeapon;
    SlotOverride offhandShield;

    bool operator==(const WeaponSetOverride &) const = default;
};

struct OutfitPreset
{
    std::wstring name;
    // Slots 0:Chest, 1:Gloves, 2:Boots, 5:Helmet, 6:Cloak
    std::array<SlotOverride, 7> armorSlots{};
    // Weapon Set 0 (I) and Weapon Set 1 (II)
    std::array<WeaponSetOverride, 2> weaponSets{};

    void MatchMaterialToChest();
    void Reset();

    bool operator==(const OutfitPreset &) const = default;
};

struct CharacterState
{
    std::string tag;
    std::int32_t activePreset = 0; // 0: Travel, 1: City, 2: Camp
    std::array<OutfitPreset, 3> presets{};
    OutfitPreset stagedPreview{};
    bool hasStagedChanges = false;

    const OutfitPreset &CurrentPreset() const
    {
        return hasStagedChanges ? stagedPreview : presets[static_cast<std::size_t>(activePreset % 3)];
    }

    OutfitPreset &ActivePreset()
    {
        return presets[static_cast<std::size_t>(activePreset % 3)];
    }
};

void Init();

// Character state accessors
CharacterState &GetOrCreate(dao::ObjectId id, std::string_view tag = {});
CharacterState *Find(dao::ObjectId id);
CharacterState *FindByTag(std::string_view tag);
void ResolvePending(dao::ObjectId id, std::string_view tag = {});
void Remove(dao::ObjectId id);
void ClearAll();
// Resume-button fallback: restores the newest co-save if DAOLib never reported a loaded save this session.
void NotifyInPregame();
void RestoreFromResumeIfNeeded();

// Staging & preset workflows
void BeginStaging(dao::ObjectId id);
void StageArmorSlot(dao::ObjectId id, catalog::Slot slot, const SlotOverride &ovr);
void StageWeaponSlot(dao::ObjectId id, std::int32_t weaponSet, bool isMainHand, bool isShield,
                     const SlotOverride &ovr);
void CommitStaged(dao::ObjectId id);
void DiscardStaged(dao::ObjectId id);
void DiscardAllStaged();
void SwitchPreset(dao::ObjectId id, std::int32_t presetIndex);
void CyclePreset(dao::ObjectId id);

// Serialization
std::string ToJson();
bool FromJson(std::string_view jsonStr);

// Appearance unlock registry (persisted across saves)
void RecordUnlocked(std::uint16_t baseItem, std::uint8_t variation);
bool IsUnlocked(std::uint16_t baseItem, std::uint8_t variation);
void LoadUnlockedList();
void SaveUnlockedList();

// World appearance refresh coordination
bool NeedsWorldRefresh();
void SetNeedsWorldRefresh(bool need);

// Party & Hero identification
bool IsHeroObject(dao::ObjectId id, std::string_view tag = {});
bool IsPartyMemberOrHero(dao::ObjectId id);

} // namespace transmog::state
