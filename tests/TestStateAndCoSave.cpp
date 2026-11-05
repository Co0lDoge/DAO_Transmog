#include "State.h"

#include <catch2/catch_test_macros.hpp>

using namespace transmog::state;
using namespace transmog::catalog;

TEST_CASE("State presets management and match material", "[state]")
{
    OutfitPreset preset;
    preset.Reset();
    preset.armorSlots[0] = {
        .mode = SlotMode::Override,
        .baseItem = 20,
        .variation = 3,
        .material = 7, // Silverite
        .heraldry = 12,
    };

    preset.MatchMaterialToChest();

    CHECK(preset.armorSlots[1].material == 7);
    CHECK(preset.armorSlots[2].material == 7);
    CHECK(preset.armorSlots[5].material == 7);
    CHECK(preset.armorSlots[5].heraldry == 12);
}

TEST_CASE("State staging and commit workflow", "[state]")
{
    ClearAll();
    const dao::ObjectId testChar = 0x1001;

    auto &charState = GetOrCreate(testChar, "alistair");
    CHECK(charState.activePreset == 0);
    CHECK_FALSE(charState.hasStagedChanges);

    // Stage changes on Chest
    SlotOverride stagedChest{
        .mode = SlotMode::Override,
        .baseItem = 22,
        .variation = 1,
        .material = 4,
    };
    StageArmorSlot(testChar, Slot::Chest, stagedChest);

    CHECK(charState.hasStagedChanges);
    CHECK(charState.CurrentPreset().armorSlots[0].baseItem == 22);
    CHECK(charState.ActivePreset().armorSlots[0].mode == SlotMode::Real); // Not yet committed!

    // Commit changes
    CommitStaged(testChar);
    CHECK_FALSE(charState.hasStagedChanges);
    CHECK(charState.ActivePreset().armorSlots[0].baseItem == 22);

    // Stage again and discard
    SlotOverride stagedHelmet{
        .mode = SlotMode::HideDialogue,
        .baseItem = 25,
        .variation = 2,
    };
    StageArmorSlot(testChar, Slot::Helmet, stagedHelmet);
    CHECK(charState.hasStagedChanges);

    DiscardStaged(testChar);
    CHECK_FALSE(charState.hasStagedChanges);
    CHECK(charState.ActivePreset().armorSlots[5].mode == SlotMode::Real);
}

TEST_CASE("State JSON serialization and co-save round-trip", "[state]")
{
    ClearAll();
    const dao::ObjectId hero = 0x5001;
    auto &state = GetOrCreate(hero, "player_hero");

    state.ActivePreset().armorSlots[0] = {
        .mode = SlotMode::Override,
        .baseItem = 30,
        .variation = 2,
        .material = 6,
        .heraldry = 5,
    };
    state.ActivePreset().weaponSets[0].mainHand = {
        .mode = SlotMode::Override,
        .baseItem = 1,
        .variation = 4,
        .suppressRuneVfx = true,
    };

    const std::string jsonStr = ToJson();
    REQUIRE_FALSE(jsonStr.empty());

    // Clear state and restore from JSON
    ClearAll();
    CHECK(Find(hero) == nullptr);

    const bool restored = FromJson(jsonStr);
    REQUIRE(restored);

    auto *restoredState = FindByTag("player_hero");
    REQUIRE(restoredState != nullptr);
    CHECK(restoredState->ActivePreset().armorSlots[0].baseItem == 30);
    CHECK(restoredState->ActivePreset().armorSlots[0].material == 6);
    CHECK(restoredState->ActivePreset().weaponSets[0].mainHand.suppressRuneVfx == true);
}
