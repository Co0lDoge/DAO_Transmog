#include "Catalog.h"

#include <catch2/catch_test_macros.hpp>

using namespace transmog::catalog;

TEST_CASE("Catalog stem builder creates lowercase stems", "[catalog]")
{
    REQUIRE(BuildMeshStem(L"ARM", L"MED", L"D") == L"arm_medd");
    REQUIRE(BuildMeshStem(L"LSW", L"", L"LS03A") == L"lswls03a");
    REQUIRE(BuildMeshStem(L"W_LSW", L"01", L"A") == L"w_lsw_01a");
    REQUIRE(BuildMeshStem(L"", L"", L"") == L"");
}

TEST_CASE("Catalog prefix mask mapping", "[catalog]")
{
    REQUIRE(PrefixToMask(L"hm") == RaceGender::HM);
    REQUIRE(PrefixToMask(L"hf") == RaceGender::HF);
    REQUIRE(PrefixToMask(L"em") == RaceGender::EM);
    REQUIRE(PrefixToMask(L"ef") == RaceGender::EF);
    REQUIRE(PrefixToMask(L"dm") == RaceGender::DM);
    REQUIRE(PrefixToMask(L"df") == RaceGender::DF);
    REQUIRE(PrefixToMask(L"qm") == RaceGender::QM);
    REQUIRE(PrefixToMask(L"qf") == RaceGender::QF);
    REQUIRE(PrefixToMask(L"unknown") == 0);

    REQUIRE(MaskToPrefix(RaceGender::HM) == L"hm");
    REQUIRE(MaskToPrefix(RaceGender::EF) == L"ef");
}

TEST_CASE("Catalog scanner parses mock tables correctly", "[catalog]")
{
    // Mock getInt
    auto mockGetInt = [](std::int32_t table, std::int32_t row, std::string_view col) -> std::optional<std::int32_t> {
        if (table == 6 && row == 1) // Longsword
        {
            if (col == "EquippableSlots") return (1 << 0) | (1 << 1); // Main and Offhand
            if (col == "Variation_Worksheet") return 30; // longsword_variation
            if (col == "DefaultMaterial") return 2;
            if (col == "WeaponWield") return 2; // 1H
            if (col == "Type") return 2; // Melee
        }
        else if (table == 6 && row == 20) // Heavy Armor
        {
            if (col == "EquippableSlots") return (1 << 4); // Chest
            if (col == "Variation_Worksheet") return 29;
            if (col == "DefaultMaterial") return 3;
            if (col == "ArmorType") return 3; // Heavy
            if (col == "Type") return 6;
        }
        else if (table == 29 && row == 1) // Heavy Armor Variation 1
        {
            if (col == "DefaultMaterial") return 4;
        }
        else if (table == 89 && row == 1) // Material 1
        {
            return 1;
        }
        return std::nullopt;
    };

    // Mock getString
    auto mockGetString = [](std::int32_t table, std::int32_t row, std::string_view col) -> std::optional<std::wstring> {
        if (table == 6 && row == 1)
        {
            if (col == "Label") return L"Longsword";
            if (col == "DefaultIcon") return L"ico_lsw";
        }
        else if (table == 6 && row == 20)
        {
            if (col == "Label") return L"Heavy Armor";
            if (col == "DefaultIcon") return L"ico_hvy";
        }
        else if (table == 30 && row == 1)
        {
            if (col == "ModelType") return L"LSW";
            if (col == "ModelVariation") return L"01A";
            if (col == "Label") return L"Longsword Var 1";
        }
        else if (table == 29 && row == 1)
        {
            if (col == "ModelType") return L"ARM";
            if (col == "ModelSubType") return L"HVY";
            if (col == "ModelVariation") return L"A";
            if (col == "Label") return L"Heavy Plate Var A";
        }
        else if (table == 89 && row == 1)
        {
            if (col == "TintFile") return L"Iron.tnt";
            if (col == "Label") return L"Iron";
        }
        else if (table == 99 && row == 1)
        {
            if (col == "HeraldryFile") return L"warden.dds";
            if (col == "Label") return L"Grey Warden";
        }
        return std::nullopt;
    };

    // Mock exists: return true for human male and elf female for armor, and true for weapon
    auto mockExists = [](std::wstring_view filename) -> bool {
        if (filename == L"w_lsw01a_0.mmh") return true;
        if (filename == L"hm_arm_hvya_0.mmh") return true;
        if (filename == L"ef_arm_hvya_0.mmh") return true;
        return false;
    };

    ScanWith(mockGetInt, mockGetString, mockExists);

    REQUIRE(IsScanned());

    // Verify Chest entries
    auto chestEntries = EntriesForSlot(Slot::Chest);
    REQUIRE(chestEntries.size() == 1);
    CHECK(chestEntries[0].baseItem == 20);
    CHECK(chestEntries[0].variation == 1);
    CHECK(chestEntries[0].meshStem == L"arm_hvya");
    CHECK((chestEntries[0].raceGenderMask & RaceGender::HM) != 0);
    CHECK((chestEntries[0].raceGenderMask & RaceGender::EF) != 0);
    CHECK((chestEntries[0].raceGenderMask & RaceGender::DM) == 0);

    // Verify Main Hand entries
    auto mainEntries = EntriesForSlot(Slot::Main);
    REQUIRE(mainEntries.size() == 1);
    CHECK(mainEntries[0].baseItem == 1);
    CHECK(mainEntries[0].meshStem == L"lsw01a");
    CHECK(mainEntries[0].raceGenderMask == RaceGender::All);

    // Verify Materials & Heraldry
    REQUIRE(Materials().size() == 1);
    CHECK(Materials()[0].tintFile == L"Iron.tnt");
    REQUIRE(Heraldries().size() == 1);
    CHECK(Heraldries()[0].textureFile == L"warden.dds");

    // Test FilteredEntries
    auto filteredForDwarf = FilteredEntries(Slot::Chest, RaceGender::DM, 0, 0, 0, false);
    CHECK(filteredForDwarf.empty());

    auto filteredForHuman = FilteredEntries(Slot::Chest, RaceGender::HM, 0, 0, 0, false);
    REQUIRE(filteredForHuman.size() == 1);
    CHECK(filteredForHuman[0]->baseItem == 20);
}
