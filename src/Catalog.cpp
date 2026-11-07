#include "Catalog.h"
#include "dao/Gda.h"
#include "dao/Log.h"
#include "dao/Resources.h"
#include "dao/Tlk.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <map>
#include <set>
#include <unordered_map>

namespace transmog::catalog
{

namespace
{

std::array<std::vector<CatalogEntry>, 7> g_slotEntries;
std::vector<MaterialEntry> g_materials;
std::vector<HeraldryEntry> g_heraldries;
bool g_scanned = false;

std::wstring Lower(std::wstring text)
{
    std::ranges::transform(text, text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

constexpr std::array<std::pair<std::uint8_t, const wchar_t *>, 8> kRacePrefixes = {{
    {RaceGender::HM, L"hm"},
    {RaceGender::HF, L"hf"},
    {RaceGender::EM, L"em"},
    {RaceGender::EF, L"ef"},
    {RaceGender::DM, L"dm"},
    {RaceGender::DF, L"df"},
    {RaceGender::QM, L"qm"},
    {RaceGender::QF, L"qf"},
}};

} // namespace

std::wstring BuildMeshStem(std::wstring_view modelType, std::wstring_view modelSubType,
                           std::wstring_view modelVariation)
{
    if (modelType.empty())
    {
        return {};
    }
    std::wstring stem = Lower(std::wstring(modelType));
    if (!modelSubType.empty())
    {
        stem += L"_" + Lower(std::wstring(modelSubType));
    }
    stem += Lower(std::wstring(modelVariation));
    return stem;
}

std::uint8_t PrefixToMask(std::wstring_view prefix)
{
    for (const auto &[mask, p] : kRacePrefixes)
    {
        if (prefix == p)
        {
            return mask;
        }
    }
    return 0;
}

std::wstring_view MaskToPrefix(std::uint8_t singleBitMask)
{
    for (const auto &[mask, p] : kRacePrefixes)
    {
        if (singleBitMask == mask)
        {
            return p;
        }
    }
    return L"hm";
}

void ScanWith(GetIntFn getInt, GetStringFn getString, ExistsFn exists)
{
    for (auto &bucket : g_slotEntries)
    {
        bucket.clear();
    }
    g_materials.clear();
    g_heraldries.clear();

    std::uint32_t nextId = 1;
    std::set<std::pair<Slot, std::wstring>> seenMeshStems;

    // 1. Scan bitm_base (Table 6)
    constexpr std::int32_t kTableItems = 6;
    for (std::int32_t baseId = 1; baseId <= 255; ++baseId)
    {
        const auto eqSlotsOpt = getInt(kTableItems, baseId, "EquippableSlots");
        if (!eqSlotsOpt)
        {
            continue;
        }
        const std::int32_t eqSlots = *eqSlotsOpt;
        if (eqSlots == 0)
        {
            continue;
        }

        const auto varSheetOpt = getInt(kTableItems, baseId, "Variation_Worksheet");
        const std::int32_t varSheet = varSheetOpt.value_or(0);
        if (varSheet <= 0)
        {
            continue;
        }

        const auto nameRef = getInt(kTableItems, baseId, "NameStrRef").value_or(0);
        const auto defMat = static_cast<std::uint8_t>(getInt(kTableItems, baseId, "DefaultMaterial").value_or(1));
        const auto heraldryVal = getInt(kTableItems, baseId, "Heraldry").value_or(0);
        const auto itemType = getInt(kTableItems, baseId, "Type").value_or(0);
        const auto armorType = getInt(kTableItems, baseId, "ArmorType").value_or(0);
        const auto weaponWield = getInt(kTableItems, baseId, "WeaponWield").value_or(0);
        const auto defIcon = getString(kTableItems, baseId, "DefaultIcon").value_or(L"");
        const auto baseLabel = getString(kTableItems, baseId, "Label").value_or(L"");

        // Determine which appearance slot(s) this baseItem maps to
        std::vector<Slot> targetSlots;
        if (eqSlots & (1 << 4))
        {
            targetSlots.push_back(Slot::Chest);
        }
        if (eqSlots & (1 << 5))
        {
            targetSlots.push_back(Slot::Helmet);
        }
        if (eqSlots & (1 << 6))
        {
            targetSlots.push_back(Slot::Boots);
        }
        if (eqSlots & (1 << 7))
        {
            targetSlots.push_back(Slot::Gloves);
        }
        if (eqSlots & (1 << 8))
        {
            targetSlots.push_back(Slot::Cloak);
        }
        if (eqSlots & (1 << 0))
        {
            targetSlots.push_back(Slot::Main);
        }
        if ((eqSlots & (1 << 1)) && !(eqSlots & (1 << 2))) // Ignore ammo
        {
            targetSlots.push_back(Slot::Offhand);
        }

        if (targetSlots.empty())
        {
            continue;
        }

        // 2. Scan rows of the Variation Worksheet
        for (std::int32_t varId = 1; varId <= 255; ++varId)
        {
            const auto modelType = getString(varSheet, varId, "ModelType").value_or(L"");
            if (modelType.empty())
            {
                continue;
            }
            const auto modelSub = getString(varSheet, varId, "ModelSubType").value_or(L"");
            const auto modelVar = getString(varSheet, varId, "ModelVariation").value_or(L"");
            const auto iconName = getString(varSheet, varId, "IconName").value_or(L"");
            const auto varLabel = getString(varSheet, varId, "Label").value_or(L"");
            auto containsCi = [](std::wstring_view str, std::wstring_view sub) {
                auto it = std::search(
                    str.begin(), str.end(),
                    sub.begin(), sub.end(),
                    [](wchar_t ch1, wchar_t ch2) { return std::towupper(ch1) == std::towupper(ch2); }
                );
                return it != str.end();
            };
            if (containsCi(varLabel, L"CUT") || containsCi(varLabel, L"Obsolete") ||
                containsCi(baseLabel, L"CUT") || containsCi(baseLabel, L"Obsolete"))
            {
                continue;
            }
            const auto rowMat = static_cast<std::uint8_t>(getInt(varSheet, varId, "DefaultMaterial").value_or(defMat));
            const auto fullFaceVal = getInt(varSheet, varId, "FullFace").value_or(0);

            const std::wstring stem = BuildMeshStem(modelType, modelSub, modelVar);
            if (stem.empty())
            {
                continue;
            }

            for (const Slot targetSlot : targetSlots)
            {
                if (seenMeshStems.contains({targetSlot, stem}))
                {
                    continue;
                }

                std::uint8_t raceMask = 0;
                if (targetSlot == Slot::Main || targetSlot == Slot::Offhand)
                {
                    // Weapon or Shield: "w_<stem>_0.mmh"
                    const std::wstring meshName = L"w_" + stem + L"_0.mmh";
                    if (exists && !exists(meshName))
                    {
                        continue;
                    }
                    raceMask = RaceGender::All;
                }
                else
                {
                    // Body / Helmet / Cloak: test 8 race/gender prefixes
                    // Fast path: Human male (hm) and Human female (hf) are standard for all DA:O armors.
                    // If neither hm nor hf mesh exists, skip checking the other 6 race prefixes entirely.
                    const std::wstring hmMesh = L"hm_" + stem + L"_0.mmh";
                    const std::wstring hfMesh = L"hf_" + stem + L"_0.mmh";
                    const bool hmExists = !exists || exists(hmMesh);
                    const bool hfExists = !exists || exists(hfMesh);
                    if (!hmExists && !hfExists)
                    {
                        continue;
                    }
                    raceMask = (hmExists ? RaceGender::HM : 0) | (hfExists ? RaceGender::HF : 0);
                    for (const auto &[mask, prefix] : kRacePrefixes)
                    {
                        if (mask == RaceGender::HM || mask == RaceGender::HF)
                        {
                            continue;
                        }
                        const std::wstring meshName = std::wstring(prefix) + L"_" + stem + L"_0.mmh";
                        if (!exists || exists(meshName))
                        {
                            raceMask |= mask;
                        }
                    }
                    if (raceMask == 0)
                    {
                        continue; // No valid mesh installed for any race
                    }
                }

                seenMeshStems.insert({targetSlot, stem});

                CatalogEntry entry;
                entry.id = nextId++;
                entry.baseItem = static_cast<std::uint16_t>(baseId);
                entry.variation = static_cast<std::uint8_t>(varId);
                entry.slot = targetSlot;
                entry.label = !varLabel.empty() ? varLabel : baseLabel;
                entry.meshStem = stem;
                entry.icon = !iconName.empty() ? iconName : defIcon;
                entry.defaultMaterial = rowMat;
                entry.hasHeraldry = (heraldryVal != 0);
                entry.raceGenderMask = raceMask;
                entry.weaponWield = weaponWield;
                entry.itemType = itemType;
                entry.armorType = armorType;
                entry.fullFace = (fullFaceVal != 0);

                // Localized display name
                if (nameRef != 0)
                {
                    entry.name = dao::tlk::String(nameRef);
                }
                if (entry.name.empty())
                {
                    entry.name = entry.label;
                }

                g_slotEntries[static_cast<std::size_t>(targetSlot)].push_back(std::move(entry));
            }
        }
    }

    // 3. Scan Material Types (Table 89)
    constexpr std::int32_t kTableMaterials = 89;
    for (std::int32_t matId = 1; matId <= 100; ++matId)
    {
        const auto tintFile = getString(kTableMaterials, matId, "TintFile").value_or(L"");
        if (tintFile.empty())
        {
            continue;
        }
        const auto label = getString(kTableMaterials, matId, "Label").value_or(L"");
        MaterialEntry mat;
        mat.id = static_cast<std::uint8_t>(matId);
        mat.label = label;
        mat.tintFile = tintFile;

        // Try getting localized material name from light/heavy armor strrefs
        const auto nameRef = getInt(kTableMaterials, matId, "HeavyArmorStrRef")
                                 .value_or(getInt(kTableMaterials, matId, "LightArmorStrRef").value_or(0));
        if (nameRef != 0)
        {
            mat.name = dao::tlk::String(nameRef);
        }
        if (mat.name.empty())
        {
            mat.name = label;
        }
        g_materials.push_back(std::move(mat));
    }

    // 4. Scan Heraldry (Table 99)
    constexpr std::int32_t kTableHeraldry = 99;
    for (std::int32_t hId = 1; hId <= 100; ++hId)
    {
        const auto file = getString(kTableHeraldry, hId, "HeraldryFile").value_or(L"");
        if (file.empty())
        {
            continue;
        }
        HeraldryEntry h;
        h.id = static_cast<std::uint32_t>(hId);
        h.label = getString(kTableHeraldry, hId, "Label").value_or(L"");
        h.textureFile = file;
        g_heraldries.push_back(std::move(h));
    }

    g_scanned = true;
    spdlog::info("[DAOTransmog::Catalog] Catalog scanned: {} chest, {} gloves, {} boots, {} main, {} offhand, {} "
                 "helmets, {} cloaks | {} materials, {} heraldries",
                 g_slotEntries[0].size(), g_slotEntries[1].size(), g_slotEntries[2].size(), g_slotEntries[3].size(),
                 g_slotEntries[4].size(), g_slotEntries[5].size(), g_slotEntries[6].size(), g_materials.size(),
                 g_heraldries.size());
}

void Scan()
{
    const auto testRows = dao::gda::Rows(6);
    if (!testRows.has_value() || *testRows <= 0)
    {
        // Tables not loaded into engine memory yet
        return;
    }

    ScanWith(
        [](std::int32_t table, std::int32_t row, std::string_view column) {
            return dao::gda::GetInt(table, row, dao::core::GdaColumnHash(column));
        },
        [](std::int32_t table, std::int32_t row, std::string_view column) {
            return dao::gda::GetString(table, row, dao::core::GdaColumnHash(column));
        },
        [](std::wstring_view file) { return dao::resources::Exists(file); });
}

bool EnsureScanned()
{
    if (g_scanned && TotalEntries() > 0)
    {
        return true;
    }
    Scan();
    return g_scanned && TotalEntries() > 0;
}

bool IsScanned()
{
    return g_scanned;
}

std::size_t TotalEntries()
{
    std::size_t total = 0;
    for (const auto &bucket : g_slotEntries)
    {
        total += bucket.size();
    }
    return total;
}

std::span<const CatalogEntry> EntriesForSlot(Slot slot)
{
    EnsureScanned();
    const auto idx = static_cast<std::size_t>(slot);
    return idx < g_slotEntries.size() ? g_slotEntries[idx] : std::span<const CatalogEntry>{};
}

const CatalogEntry *Find(Slot slot, std::uint16_t baseItem, std::uint8_t variation)
{
    for (const auto &entry : EntriesForSlot(slot))
    {
        if (entry.baseItem == baseItem && entry.variation == variation)
        {
            return &entry;
        }
    }
    return nullptr;
}

std::vector<const CatalogEntry *> FilteredEntries(Slot slot, std::uint8_t charRaceGenderMask, std::int32_t subcatFilter,
                                                  std::int32_t realWeaponWield, std::int32_t realItemType,
                                                  bool strictGripMatching)
{
    std::vector<const CatalogEntry *> result;
    for (const auto &entry : EntriesForSlot(slot))
    {
        // 1. Race / Gender mesh compatibility
        if (charRaceGenderMask != 0 && (entry.raceGenderMask & charRaceGenderMask) == 0)
        {
            continue;
        }

        // 2. Sub-category filter (if > 0: for armor represents armorType; for weapons represents itemType)
        if (subcatFilter > 0)
        {
            if (slot == Slot::Chest || slot == Slot::Gloves || slot == Slot::Boots || slot == Slot::Helmet)
            {
                if (entry.armorType != subcatFilter)
                {
                    continue;
                }
            }
            else if (slot == Slot::Main || slot == Slot::Offhand)
            {
                if (entry.itemType != subcatFilter)
                {
                    continue;
                }
            }
        }

        // 3. Weapon grip / style compatibility
        if (slot == Slot::Main || slot == Slot::Offhand)
        {
            if (realItemType > 0)
            {
                // Offhand: Shields vs Weapons disambiguation
                if (realItemType == 1 && entry.itemType != 1)
                {
                    continue; // Real item is shield, transmog must be shield
                }
                if (realItemType != 1 && entry.itemType == 1)
                {
                    continue; // Real item is weapon, transmog cannot be shield
                }
            }

            if (strictGripMatching && realWeaponWield > 0)
            {
                if (entry.weaponWield != realWeaponWield)
                {
                    continue;
                }
            }
        }

        result.push_back(&entry);
    }
    return result;
}

std::span<const MaterialEntry> Materials()
{
    return g_materials;
}

std::span<const HeraldryEntry> Heraldries()
{
    return g_heraldries;
}

const MaterialEntry *FindMaterial(std::uint8_t id)
{
    for (const auto &mat : g_materials)
    {
        if (mat.id == id)
        {
            return &mat;
        }
    }
    return nullptr;
}

const HeraldryEntry *FindHeraldry(std::uint32_t id)
{
    for (const auto &h : g_heraldries)
    {
        if (h.id == id)
        {
            return &h;
        }
    }
    return nullptr;
}

} // namespace transmog::catalog
