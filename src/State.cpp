#include "State.h"
#include "dao/ClientExoApp.h"
#include "dao/Log.h"
#include "dao/Objects.h"
#include "dao/Party.h"
#include "dao/Saves.h"
#include "dao/generated/ScriptApi.h"

#include <windows.h>
#include <shlobj.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>

namespace transmog::state
{

namespace
{

std::unordered_map<dao::ObjectId, CharacterState> g_characters;
std::unordered_map<std::string, dao::ObjectId> g_tagToId;
std::set<std::pair<std::uint16_t, std::uint8_t>> g_unlockedAppearances;

std::optional<CharacterState> g_pendingHeroState;
std::unordered_map<dao::ObjectId, CharacterState> g_pendingById;
std::unordered_map<std::string, CharacterState> g_pendingByTag;
bool g_loadedSincePregame = false;

std::string WideToUtf8(std::wstring_view wstr)
{
    if (wstr.empty())
    {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return {};
    }
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), result.data(), size, nullptr, nullptr);
    return result;
}

void InitDefaultPresets(CharacterState &state)
{
    state.presets[0].name = L"Travel";
    state.presets[1].name = L"City";
    state.presets[2].name = L"Camp";
    state.stagedPreview = state.presets[0];
    state.hasStagedChanges = false;
}

std::filesystem::path ResolveResumeCoSavePath()
{
    // 1. Try CClientExoApp + 0x218 (kAskedPath)
    if (const auto *app = dao::CClientExoApp::Get())
    {
        dao::core::WStringRep rep;
        if (dao::TryCopy(&rep, reinterpret_cast<std::uintptr_t>(app) + 0x218, sizeof(rep)) && rep.size > 0 &&
            rep.size < 4096)
        {
            std::wstring pathStr;
            if (rep.capacity <= dao::core::WStringRep::kInlineCapacity)
            {
                pathStr = std::wstring(rep.View());
            }
            else
            {
                pathStr.resize(rep.size);
                dao::TryCopy(pathStr.data(), reinterpret_cast<std::uintptr_t>(rep.data.pointer),
                             rep.size * sizeof(wchar_t));
            }
            if (!pathStr.empty())
            {
                std::filesystem::path p(pathStr);
                auto candidate = p.parent_path() / "DAOTransmog.daolib";
                std::error_code ec;
                if (std::filesystem::exists(candidate, ec))
                {
                    spdlog::info("[DAOTransmog::State] Found Resume co-save via CClientExoApp path: {}",
                                 candidate.string());
                    return candidate;
                }
            }
        }
    }

    // 2. Try DragonAge.ini LastPlayedGame
    wchar_t docsPath[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, docsPath)))
    {
        std::filesystem::path iniPath =
            std::filesystem::path(docsPath) / L"BioWare" / L"Dragon Age" / L"Settings" / L"DragonAge.ini";
        std::error_code ec;
        if (std::filesystem::exists(iniPath, ec))
        {
            wchar_t lastPlayed[MAX_PATH] = {0};
            if (GetPrivateProfileStringW(L"GameOptions", L"LastPlayedGame", L"", lastPlayed, MAX_PATH,
                                         iniPath.wstring().c_str()) > 0)
            {
                std::filesystem::path p(lastPlayed);
                auto candidate = p.parent_path() / "DAOTransmog.daolib";
                if (std::filesystem::exists(candidate, ec))
                {
                    spdlog::info("[DAOTransmog::State] Found Resume co-save via DragonAge.ini LastPlayedGame: {}",
                                 candidate.string());
                    return candidate;
                }
            }
        }

        // 3. Try finding newest DAOTransmog.daolib across Characters folder
        std::filesystem::path charsDir =
            std::filesystem::path(docsPath) / L"BioWare" / L"Dragon Age" / L"Characters";
        std::filesystem::path newestFile;
        std::filesystem::file_time_type newestTime;

        for (const auto &entry : std::filesystem::recursive_directory_iterator(charsDir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().filename() == L"DAOTransmog.daolib")
            {
                const auto writeTime = entry.last_write_time(ec);
                if (newestFile.empty() || writeTime > newestTime)
                {
                    newestFile = entry.path();
                    newestTime = writeTime;
                }
            }
        }
        if (!newestFile.empty())
        {
            spdlog::info("[DAOTransmog::State] Found Resume co-save via newest save scan: {}",
                         newestFile.string());
            return newestFile;
        }
    }

    return {};
}

} // namespace

void OutfitPreset::MatchMaterialToChest()
{
    const auto chestMat = armorSlots[0].material;
    const auto chestHeraldry = armorSlots[0].heraldry;

    // Apply to Gloves (1), Boots (2), Helmet (5)
    armorSlots[1].material = chestMat;
    armorSlots[2].material = chestMat;
    armorSlots[5].material = chestMat;
    armorSlots[5].heraldry = chestHeraldry;
}

void OutfitPreset::Reset()
{
    for (auto &slot : armorSlots)
    {
        slot = SlotOverride{};
    }
    for (auto &ws : weaponSets)
    {
        ws = WeaponSetOverride{};
    }
}

void Init()
{
    // Co-save hook: save state to <SaveSlot>\DAOTransmog.daolib
    dao::saves::OnSaved([](const dao::saves::Slot &save) {
        const std::string jsonStr = ToJson();
        if (save.Write("DAOTransmog", jsonStr))
        {
            spdlog::info("[DAOTransmog::State] Saved appearance state to {}", save.File("DAOTransmog").string());
        }
        else
        {
            spdlog::error("[DAOTransmog::State] Failed to write appearance state to {}",
                          save.File("DAOTransmog").string());
        }
    });

    // Co-save hook: load state from <SaveSlot>\DAOTransmog.daolib
    dao::saves::OnGameLoaded([](const std::optional<dao::saves::Slot> &save) {
        g_loadedSincePregame = true;
        ClearAll();

        // Ensure catalog is scanned now that game tables are loaded into memory
        catalog::EnsureScanned();

        std::filesystem::path coSaveFile;
        if (save)
        {
            coSaveFile = save->File("DAOTransmog");
        }

        std::error_code ec;
        if (!coSaveFile.empty() && std::filesystem::exists(coSaveFile, ec))
        {
            std::ifstream in(coSaveFile, std::ios::binary);
            if (in)
            {
                const std::string jsonStr((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                if (FromJson(jsonStr))
                {
                    spdlog::info("[DAOTransmog::State] Restored appearance state from {}", coSaveFile.string());
                    SetNeedsWorldRefresh(true);
                    return;
                }
                else
                {
                    spdlog::warn("[DAOTransmog::State] Failed to parse appearance state JSON from {}",
                                 coSaveFile.string());
                }
            }
        }

        spdlog::info("[DAOTransmog::State] No DAOTransmog.daolib found in save slot; keeping vanilla");
    });

    LoadUnlockedList();
}

void NotifyInPregame()
{
    g_loadedSincePregame = false;
}

void RestoreFromResumeIfNeeded()
{
    if (g_loadedSincePregame)
    {
        return;
    }
    g_loadedSincePregame = true;

    ClearAll();
    catalog::EnsureScanned();

    const std::filesystem::path coSaveFile = ResolveResumeCoSavePath();
    std::error_code ec;
    if (!coSaveFile.empty() && std::filesystem::exists(coSaveFile, ec))
    {
        std::ifstream in(coSaveFile, std::ios::binary);
        if (in)
        {
            const std::string jsonStr((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (FromJson(jsonStr))
            {
                spdlog::info("[DAOTransmog::State] Restored appearance state via Resume fallback from {}",
                             coSaveFile.string());
                SetNeedsWorldRefresh(true);
                return;
            }
            else
            {
                spdlog::warn("[DAOTransmog::State] Failed to parse Resume appearance state JSON from {}",
                             coSaveFile.string());
            }
        }
    }

    spdlog::info("[DAOTransmog::State] No DAOTransmog.daolib found for Resume fallback; keeping vanilla");
}

void ResolvePending(dao::ObjectId id, std::string_view tag)
{
    if (id == dao::kInvalidObject)
    {
        return;
    }

    auto it = g_characters.find(id);
    if (it != g_characters.end())
    {
        if (!tag.empty() && it->second.tag.empty())
        {
            it->second.tag = std::string(tag);
            g_tagToId[it->second.tag] = id;
        }
        return;
    }

    const dao::ObjectId heroId = dao::script::api::GetHero();
    const dao::ObjectId controlledId = dao::party::Controlled();
    const bool isHero = (heroId != dao::kInvalidObject && id == heroId) ||
                        (controlledId != dao::kInvalidObject && id == controlledId) ||
                        (tag == "hero") || (tag == "player_hero");

    // 1. Pending hero state
    if (isHero && g_pendingHeroState.has_value())
    {
        g_characters[id] = *g_pendingHeroState;
        g_characters[id].tag = "hero";
        g_tagToId["hero"] = id;
        g_tagToId["player_hero"] = id;
        g_pendingHeroState.reset();
        spdlog::info("[DAOTransmog::State] Bound pending hero transmog state to object {:#x}", id);
        return;
    }

    // 2. Pending by ID (e.g. numeric "130" from older saves)
    auto itId = g_pendingById.find(id);
    if (itId != g_pendingById.end())
    {
        g_characters[id] = itId->second;
        g_characters[id].tag = !tag.empty() ? std::string(tag) : (isHero ? "hero" : std::to_string(id));
        g_tagToId[g_characters[id].tag] = id;
        g_pendingById.erase(itId);
        spdlog::info("[DAOTransmog::State] Bound pending transmog state by numeric ID {:#x}", id);
        return;
    }

    // 3. Pending by tag
    std::string tagStr(tag);
    if (tagStr.empty())
    {
        const std::wstring engineTag = dao::script::api::GetTag(id);
        if (!engineTag.empty())
        {
            tagStr = WideToUtf8(engineTag);
        }
    }
    if (!tagStr.empty())
    {
        auto itTag = g_pendingByTag.find(tagStr);
        if (itTag != g_pendingByTag.end())
        {
            g_characters[id] = itTag->second;
            g_characters[id].tag = tagStr;
            g_tagToId[tagStr] = id;
            g_pendingByTag.erase(itTag);
            spdlog::info("[DAOTransmog::State] Bound pending transmog state by tag '{}' to object {:#x}", tagStr, id);
            return;
        }
    }

    // 4. Fallback: If this is the hero and there is any remaining pending state, bind it
    if (isHero)
    {
        if (!g_pendingByTag.empty())
        {
            auto pendingIt = g_pendingByTag.begin();
            g_characters[id] = pendingIt->second;
            g_characters[id].tag = "hero";
            g_tagToId["hero"] = id;
            spdlog::info("[DAOTransmog::State] Bound fallback pending transmog state '{}' to hero object {:#x}",
                         pendingIt->first, id);
            g_pendingByTag.erase(pendingIt);
            return;
        }
        if (!g_pendingById.empty())
        {
            auto pendingIt = g_pendingById.begin();
            g_characters[id] = pendingIt->second;
            g_characters[id].tag = "hero";
            g_tagToId["hero"] = id;
            spdlog::info("[DAOTransmog::State] Bound fallback pending numeric ID state to hero object {:#x}", id);
            g_pendingById.erase(pendingIt);
            return;
        }
    }
}

CharacterState &GetOrCreate(dao::ObjectId id, std::string_view tag)
{
    ResolvePending(id, tag);

    auto it = g_characters.find(id);
    if (it != g_characters.end())
    {
        if (!tag.empty() && it->second.tag.empty())
        {
            it->second.tag = std::string(tag);
            g_tagToId[it->second.tag] = id;
        }
        return it->second;
    }

    auto &created = g_characters[id];
    if (!tag.empty())
    {
        created.tag = std::string(tag);
        g_tagToId[created.tag] = id;
    }
    else
    {
        const dao::ObjectId heroId = dao::script::api::GetHero();
        const dao::ObjectId controlledId = dao::party::Controlled();
        if (id == heroId || id == controlledId)
        {
            created.tag = "hero";
            g_tagToId["hero"] = id;
        }
    }
    InitDefaultPresets(created);
    return created;
}

CharacterState *Find(dao::ObjectId id)
{
    ResolvePending(id);
    auto it = g_characters.find(id);
    return it != g_characters.end() ? &it->second : nullptr;
}

CharacterState *FindByTag(std::string_view tag)
{
    if (tag.empty())
    {
        return nullptr;
    }
    const std::string sTag(tag);
    auto it = g_tagToId.find(sTag);
    if (it != g_tagToId.end())
    {
        return Find(it->second);
    }
    // Fallback: search by tag in active characters
    for (auto &[id, state] : g_characters)
    {
        if (state.tag == tag)
        {
            return &state;
        }
    }
    // Also check pending states if not yet bound to an ObjectId
    auto itPending = g_pendingByTag.find(sTag);
    if (itPending != g_pendingByTag.end())
    {
        return &itPending->second;
    }
    if ((tag == "hero" || tag == "player_hero") && g_pendingHeroState.has_value())
    {
        return &(*g_pendingHeroState);
    }
    return nullptr;
}

void Remove(dao::ObjectId id)
{
    auto it = g_characters.find(id);
    if (it != g_characters.end())
    {
        if (!it->second.tag.empty())
        {
            g_tagToId.erase(it->second.tag);
        }
        g_characters.erase(it);
    }
}

void ClearAll()
{
    g_characters.clear();
    g_tagToId.clear();
    g_pendingHeroState.reset();
    g_pendingById.clear();
    g_pendingByTag.clear();
}

void BeginStaging(dao::ObjectId id)
{
    auto &state = GetOrCreate(id);
    state.stagedPreview = state.ActivePreset();
    state.hasStagedChanges = true;
}

void StageArmorSlot(dao::ObjectId id, catalog::Slot slot, const SlotOverride &ovr)
{
    auto &state = GetOrCreate(id);
    if (!state.hasStagedChanges)
    {
        BeginStaging(id);
    }
    const auto idx = static_cast<std::size_t>(slot);
    if (idx < state.stagedPreview.armorSlots.size())
    {
        state.stagedPreview.armorSlots[idx] = ovr;
    }
}

void StageWeaponSlot(dao::ObjectId id, std::int32_t weaponSet, bool isMainHand, bool isShield,
                     const SlotOverride &ovr)
{
    auto &state = GetOrCreate(id);
    if (!state.hasStagedChanges)
    {
        BeginStaging(id);
    }
    const auto setIdx = static_cast<std::size_t>(std::clamp(weaponSet, 0, 1));
    if (isMainHand)
    {
        state.stagedPreview.weaponSets[setIdx].mainHand = ovr;
    }
    else if (isShield)
    {
        state.stagedPreview.weaponSets[setIdx].offhandShield = ovr;
    }
    else
    {
        state.stagedPreview.weaponSets[setIdx].offhandWeapon = ovr;
    }
}

void CommitStaged(dao::ObjectId id)
{
    auto *state = Find(id);
    if (state != nullptr && state->hasStagedChanges)
    {
        state->ActivePreset() = state->stagedPreview;
        state->hasStagedChanges = false;
        spdlog::info("[DAOTransmog::State] Committed staged transmog changes for object {:#x}", id);
    }
}

void DiscardStaged(dao::ObjectId id)
{
    auto *state = Find(id);
    if (state != nullptr && state->hasStagedChanges)
    {
        state->stagedPreview = state->ActivePreset();
        state->hasStagedChanges = false;
        spdlog::info("[DAOTransmog::State] Discarded staged transmog changes for object {:#x}", id);
    }
}

void SwitchPreset(dao::ObjectId id, std::int32_t presetIndex)
{
    auto &state = GetOrCreate(id);
    state.activePreset = std::clamp(presetIndex, 0, 2);
    state.stagedPreview = state.ActivePreset();
    state.hasStagedChanges = false;
}

void CyclePreset(dao::ObjectId id)
{
    auto &state = GetOrCreate(id);
    SwitchPreset(id, (state.activePreset + 1) % 3);
}

// JSON Serialization helpers
namespace
{

nlohmann::json SlotToJson(const SlotOverride &s)
{
    std::string modeStr = "real";
    switch (s.mode)
    {
    case SlotMode::Override:
        modeStr = "override";
        break;
    case SlotMode::HideSlot:
        modeStr = "hide";
        break;
    case SlotMode::HideDialogue:
        modeStr = "hide_dialogue";
        break;
    default:
        modeStr = "real";
        break;
    }

    return {
        {"mode", modeStr},
        {"baseItem", s.baseItem},
        {"variation", s.variation},
        {"material", s.material},
        {"heraldry", s.heraldry},
        {"suppressVfx", s.suppressRuneVfx},
    };
}

SlotOverride SlotFromJson(const nlohmann::json &j)
{
    SlotOverride s;
    if (j.contains("mode"))
    {
        if (j["mode"].is_number())
        {
            const auto mNum = j["mode"].get<std::uint8_t>();
            switch (mNum)
            {
            case 1:
                s.mode = SlotMode::Override;
                break;
            case 2:
                s.mode = SlotMode::HideSlot;
                break;
            case 3:
                s.mode = SlotMode::HideDialogue;
                break;
            default:
                s.mode = SlotMode::Real;
                break;
            }
        }
        else if (j["mode"].is_string())
        {
            const auto mStr = j["mode"].get<std::string>();
            if (mStr == "override")
            {
                s.mode = SlotMode::Override;
            }
            else if (mStr == "hide")
            {
                s.mode = SlotMode::HideSlot;
            }
            else if (mStr == "hide_dialogue")
            {
                s.mode = SlotMode::HideDialogue;
            }
            else
            {
                s.mode = SlotMode::Real;
            }
        }
    }
    if (j.contains("baseItem"))
    {
        s.baseItem = j["baseItem"].get<std::uint16_t>();
    }
    if (j.contains("variation"))
    {
        s.variation = j["variation"].get<std::uint8_t>();
    }
    if (j.contains("material"))
    {
        s.material = j["material"].get<std::uint8_t>();
    }
    if (j.contains("heraldry"))
    {
        s.heraldry = j["heraldry"].get<std::uint32_t>();
    }
    if (j.contains("suppressVfx"))
    {
        s.suppressRuneVfx = j["suppressVfx"].get<bool>();
    }
    return s;
}

} // namespace

std::string ToJson()
{
    nlohmann::json root;
    root["schemaVersion"] = 1;

    nlohmann::json charsJson = nlohmann::json::object();
    const dao::ObjectId heroId = dao::script::api::GetHero();
    const dao::ObjectId controlledId = dao::party::Controlled();

    auto serializeCharacter = [&](std::string_view key, const CharacterState &state, bool isHero) {
        nlohmann::json charObj;
        charObj["isHero"] = isHero;
        charObj["activePreset"] = state.activePreset;

        nlohmann::json presetsArr = nlohmann::json::array();
        for (const auto &preset : state.presets)
        {
            nlohmann::json pObj;
            nlohmann::json armorObj = nlohmann::json::object();
            for (std::size_t slotIdx : {0, 1, 2, 5, 6})
            {
                armorObj[std::to_string(slotIdx)] = SlotToJson(preset.armorSlots[slotIdx]);
            }
            pObj["armorSlots"] = armorObj;

            nlohmann::json wpArr = nlohmann::json::array();
            for (const auto &ws : preset.weaponSets)
            {
                wpArr.push_back({
                    {"mainHand", SlotToJson(ws.mainHand)},
                    {"offhandWeapon", SlotToJson(ws.offhandWeapon)},
                    {"offhandShield", SlotToJson(ws.offhandShield)},
                });
            }
            pObj["weaponSets"] = wpArr;
            presetsArr.push_back(pObj);
        }
        charObj["presets"] = presetsArr;
        charsJson[std::string(key)] = charObj;
    };

    for (const auto &[id, state] : g_characters)
    {
        const bool isHero = (id != dao::kInvalidObject && (id == heroId || id == controlledId)) ||
                            state.tag == "hero" || state.tag == "player_hero";

        std::string key;
        if (!state.tag.empty())
        {
            key = state.tag;
        }
        else if (isHero)
        {
            key = "hero";
        }
        else
        {
            const std::wstring engineTag = dao::script::api::GetTag(id);
            if (!engineTag.empty())
            {
                key = WideToUtf8(engineTag);
            }
            else
            {
                key = std::to_string(id);
            }
        }

        serializeCharacter(key, state, isHero);
    }

    // Preserve any pending states that haven't been resolved yet
    if (g_pendingHeroState.has_value() && !charsJson.contains("hero"))
    {
        serializeCharacter("hero", *g_pendingHeroState, true);
    }
    for (const auto &[pTag, pState] : g_pendingByTag)
    {
        if (!charsJson.contains(pTag))
        {
            serializeCharacter(pTag, pState, pTag == "hero" || pTag == "player_hero");
        }
    }

    root["characters"] = charsJson;
    return root.dump(2);
}

bool FromJson(std::string_view jsonStr)
{
    try
    {
        auto root = nlohmann::json::parse(jsonStr);
        if (!root.contains("characters") || !root["characters"].is_object())
        {
            return false;
        }

        const auto &charsObj = root["characters"];
        const std::size_t charCount = charsObj.size();

        for (auto &[key, charObj] : charsObj.items())
        {
            CharacterState state;
            InitDefaultPresets(state);
            state.activePreset = charObj.value("activePreset", 0);
            state.tag = key;

            if (charObj.contains("presets") && charObj["presets"].is_array())
            {
                std::size_t pIdx = 0;
                for (const auto &pObj : charObj["presets"])
                {
                    if (pIdx >= state.presets.size())
                    {
                        break;
                    }
                    auto &p = state.presets[pIdx];
                    if (pObj.contains("armorSlots") && pObj["armorSlots"].is_object())
                    {
                        for (auto &[slotStr, sVal] : pObj["armorSlots"].items())
                        {
                            const auto sIdx = static_cast<std::size_t>(std::stoul(slotStr));
                            if (sIdx < p.armorSlots.size())
                            {
                                p.armorSlots[sIdx] = SlotFromJson(sVal);
                            }
                        }
                    }
                    if (pObj.contains("weaponSets") && pObj["weaponSets"].is_array())
                    {
                        std::size_t wIdx = 0;
                        for (const auto &wVal : pObj["weaponSets"])
                        {
                            if (wIdx >= p.weaponSets.size())
                            {
                                break;
                            }
                            if (wVal.contains("mainHand"))
                            {
                                p.weaponSets[wIdx].mainHand = SlotFromJson(wVal["mainHand"]);
                            }
                            if (wVal.contains("offhandWeapon"))
                            {
                                p.weaponSets[wIdx].offhandWeapon = SlotFromJson(wVal["offhandWeapon"]);
                            }
                            if (wVal.contains("offhandShield"))
                            {
                                p.weaponSets[wIdx].offhandShield = SlotFromJson(wVal["offhandShield"]);
                            }
                            ++wIdx;
                        }
                    }
                    ++pIdx;
                }
            }
            state.stagedPreview = state.ActivePreset();
            state.hasStagedChanges = false;

            const bool isHero = charObj.value("isHero", false) ||
                                (key == "hero" || key == "player_hero") ||
                                (charCount == 1);

            if (isHero)
            {
                g_pendingHeroState = state;
            }

            if (!key.empty() && std::all_of(key.begin(), key.end(), ::isdigit))
            {
                try
                {
                    const auto numId = static_cast<dao::ObjectId>(std::stoul(key));
                    g_pendingById[numId] = state;
                }
                catch (...)
                {
                }
            }

            g_pendingByTag[key] = state;
        }

        // Immediately attempt to resolve if hero/controlled is already available
        const dao::ObjectId heroId = dao::script::api::GetHero();
        if (heroId != dao::kInvalidObject)
        {
            ResolvePending(heroId, "hero");
        }
        const dao::ObjectId controlled = dao::party::Controlled();
        if (controlled != dao::kInvalidObject)
        {
            ResolvePending(controlled);
        }

        return true;
    }
    catch (const std::exception &ex)
    {
        spdlog::error("[DAOTransmog::State] JSON parse exception: {}", ex.what());
        return false;
    }
}

void RecordUnlocked(std::uint16_t baseItem, std::uint8_t variation)
{
    g_unlockedAppearances.insert({baseItem, variation});
}

bool IsUnlocked(std::uint16_t baseItem, std::uint8_t variation)
{
    return g_unlockedAppearances.contains({baseItem, variation});
}

void LoadUnlockedList()
{
    // Persistence path in documents daolib folder
}

void SaveUnlockedList()
{
}

bool g_needWorldRefresh = false;

bool NeedsWorldRefresh()
{
    return g_needWorldRefresh;
}

void SetNeedsWorldRefresh(bool need)
{
    g_needWorldRefresh = need;
}

} // namespace transmog::state
