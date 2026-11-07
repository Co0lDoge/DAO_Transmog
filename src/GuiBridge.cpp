#include "GuiBridge.h"
#include "AppearanceHook.h"
#include "Catalog.h"
#include "Config.h"
#include "ContextRules.h"
#include "PortraitHook.h"
#include "SafeMode.h"
#include "State.h"
#include "Strings.h"
#include "TransmogUiActions.h"
#include "WeaponHook.h"
#include "dao/ClientExoApp.h"
#include "dao/Frame.h"
#include "dao/Gui.h"
#include "dao/Input.h"
#include "dao/Log.h"
#include "dao/Party.h"
#include "dao/Portraits.h"
#include "dao/core/GfxMovie.h"
#include "dao/generated/ScriptApi.h"

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace transmog::gui
{

namespace
{

constexpr std::size_t kRowsPerPage = 8;

bool g_browserVisible = false;
catalog::Slot g_selectedSlot = catalog::Slot::Chest;
std::size_t g_currentPage = 0;
bool g_isHeadZoomed = false;

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

struct BrowserRow
{
    state::SlotMode mode = state::SlotMode::Real;
    std::uint16_t baseItem = 0;
    std::uint8_t variation = 0;
    std::uint8_t defaultMaterial = 0xFF;
    bool hasHeraldry = false;
    std::wstring label;
    std::wstring subtitle;
    std::wstring icon;
};

static dao::ObjectId s_lastScreenChar = dao::kInvalidObject;

dao::ObjectId CurrentCharacter()
{
    const auto screenChar = dao::party::ScreenCharacter();
    if (screenChar != dao::kInvalidObject)
    {
        return screenChar;
    }
    return dao::party::Controlled();
}

bool IsDog(dao::ObjectId charId)
{
    if (charId == dao::kInvalidObject)
    {
        return false;
    }
    const auto appType = dao::script::api::GetAppearanceType(charId);
    const auto racial = dao::script::api::GetCreatureRacialType(charId);
    return (appType == 35 || racial == 5);
}

const wchar_t *SlotDisplayName(catalog::Slot slot, bool isDog = false)
{
    if (isDog)
    {
        if (slot == catalog::Slot::Chest)
        {
            return L"Dog Collar";
        }
        if (slot == catalog::Slot::Gloves)
        {
            return L"Dog Warpaint";
        }
        return L"Dog Item";
    }

    switch (slot)
    {
    case catalog::Slot::Chest:
        return L"Chest Armor";
    case catalog::Slot::Gloves:
        return L"Gloves";
    case catalog::Slot::Boots:
        return L"Boots";
    case catalog::Slot::Main:
        return L"Main Hand";
    case catalog::Slot::Offhand:
        return L"Offhand / Shield";
    case catalog::Slot::Helmet:
        return L"Helmet";
    case catalog::Slot::Cloak:
        return L"Cloak";
    }
    return L"Armor";
}

state::SlotOverride GetStagedSlotOverride(dao::ObjectId charId, catalog::Slot slot)
{
    const auto &st = state::GetOrCreate(charId);
    const auto &preset = st.CurrentPreset();
    const auto idx = static_cast<std::size_t>(slot);
    if (idx < preset.armorSlots.size())
    {
        return preset.armorSlots[idx];
    }
    return {};
}

void ApplyStagedSlotOverride(dao::ObjectId charId, catalog::Slot slot, const state::SlotOverride &ovr)
{
    state::StageArmorSlot(charId, slot, ovr);
    if (slot == catalog::Slot::Main)
    {
        state::StageWeaponSlot(charId, 0, true, false, ovr);
    }
    else if (slot == catalog::Slot::Offhand)
    {
        state::StageWeaponSlot(charId, 0, false, false, ovr);
        state::StageWeaponSlot(charId, 0, false, true, ovr);
    }
}

std::vector<BrowserRow> BuildBrowserRows(catalog::Slot slot, bool isDog = false)
{
    std::vector<BrowserRow> rows;

    // 1. Vanilla Real Look row
    rows.push_back(BrowserRow{
        .mode = state::SlotMode::Real,
        .baseItem = 0xFFFF,
        .variation = 0xFF,
        .defaultMaterial = 0xFF,
        .hasHeraldry = false,
        .label = L"<Equipped Item Real Look>",
        .subtitle = L"Authentic Equipment",
        .icon = L"",
    });

    // 2. Hide Slot row (for non-Helmet slots; Helmet visibility is managed via the bottom cycle stepper)
    if (slot != catalog::Slot::Helmet || isDog)
    {
        rows.push_back(BrowserRow{
            .mode = state::SlotMode::HideSlot,
            .baseItem = 0,
            .variation = 0,
            .defaultMaterial = 0xFF,
            .hasHeraldry = false,
            .label = L"<Hide Slot / Bare Body>",
            .subtitle = isDog ? L"Bare Coat / No Accessory" : L"Bare Model",
            .icon = L"",
        });
    }

    if (isDog)
    {
        return rows;
    }

    // 4. Scanned Catalog Appearances
    const auto entries = catalog::FilteredEntries(slot, catalog::RaceGender::All, -1, 0, 0, false);
    for (const auto *entry : entries)
    {
        if (entry == nullptr)
        {
            continue;
        }
        if (config::Get().unlockMode == config::UnlockMode::UnlockedOnly &&
            !state::IsUnlocked(entry->baseItem, entry->variation))
        {
            continue;
        }

        std::wstring display = entry->name;
        std::wstring sub;
        if (!entry->label.empty() && entry->label != entry->name)
        {
            sub = entry->label;
        }
        else if (entry->armorType > 0)
        {
            switch (entry->armorType)
            {
            case 1:
                sub = L"Light Armor";
                break;
            case 2:
                sub = L"Medium Armor";
                break;
            case 3:
                sub = L"Heavy Armor";
                break;
            case 4:
                sub = L"Massive Armor";
                break;
            default:
                break;
            }
        }
        else if (const auto *mat = catalog::FindMaterial(entry->defaultMaterial))
        {
            sub = mat->name;
        }

        rows.push_back(BrowserRow{
            .mode = state::SlotMode::Override,
            .baseItem = entry->baseItem,
            .variation = entry->variation,
            .defaultMaterial = entry->defaultMaterial,
            .hasHeraldry = entry->hasHeraldry,
            .label = std::move(display),
            .subtitle = std::move(sub),
            .icon = entry->icon,
        });
    }

    return rows;
}

void EnsureUiInitialized(dao::gui::Movie &movie)
{
    if (!movie.IsAvailable("_root.DAOTransmog_Browser") || !movie.IsAvailable("_root.DAOTransmog_mc"))
    {
        movie.Invoke("_root.DAOTransmog_InitUI");
    }
}

void RefreshBrowserUI(dao::gui::Movie &movie, dao::ObjectId charId)
{
    EnsureUiInitialized(movie);

    const bool isDog = IsDog(charId);
    if (isDog)
    {
        // Dog only has access to slot 0 (Collar) and slot 1 (Warpaint)
        if (g_selectedSlot != catalog::Slot::Chest && g_selectedSlot != catalog::Slot::Gloves)
        {
            g_selectedSlot = catalog::Slot::Chest;
        }

        movie.SetVariable("_root.DAOTransmog_Browser.slot0.lbl.TextField.text", dao::gui::Value::WString(L"Collar"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot0._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.slot1.lbl.TextField.text", dao::gui::Value::WString(L"Warpaint"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot1._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.slot2._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.slot3._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.slot4._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.slot5._visible", dao::gui::Value::Bool(false));

        movie.SetVariable("_root.DAOTransmog_Browser.syncBtn._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.zoomBtn._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.matPrev._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.matInfo._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.matNext._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.herPrev._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.herInfo._visible", dao::gui::Value::Bool(false));
        movie.SetVariable("_root.DAOTransmog_Browser.herNext._visible", dao::gui::Value::Bool(false));
    }
    else
    {
        // Humanoid character: restore all 6 standard slot tabs
        movie.SetVariable("_root.DAOTransmog_Browser.slot0.lbl.TextField.text", dao::gui::Value::WString(L"Chest"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot0._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.slot1.lbl.TextField.text", dao::gui::Value::WString(L"Gloves"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot1._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.slot2.lbl.TextField.text", dao::gui::Value::WString(L"Boots"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot2._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.slot3.lbl.TextField.text", dao::gui::Value::WString(L"Main"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot3._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.slot4.lbl.TextField.text", dao::gui::Value::WString(L"Offhand"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot4._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.slot5.lbl.TextField.text", dao::gui::Value::WString(L"Helmet"));
        movie.SetVariable("_root.DAOTransmog_Browser.slot5._visible", dao::gui::Value::Bool(true));

        movie.SetVariable("_root.DAOTransmog_Browser.syncBtn._visible", dao::gui::Value::Bool(true));
        movie.SetVariable("_root.DAOTransmog_Browser.zoomBtn._visible", dao::gui::Value::Bool(true));
        movie.SetVariable("_root.DAOTransmog_Browser.matPrev._visible", dao::gui::Value::Bool(true));
        movie.SetVariable("_root.DAOTransmog_Browser.matInfo._visible", dao::gui::Value::Bool(true));
        movie.SetVariable("_root.DAOTransmog_Browser.matNext._visible", dao::gui::Value::Bool(true));
        movie.SetVariable("_root.DAOTransmog_Browser.herPrev._visible", dao::gui::Value::Bool(true));
        movie.SetVariable("_root.DAOTransmog_Browser.herInfo._visible", dao::gui::Value::Bool(true));
        movie.SetVariable("_root.DAOTransmog_Browser.herNext._visible", dao::gui::Value::Bool(true));
    }

    const auto &st = state::GetOrCreate(charId);
    const auto &preset = st.CurrentPreset();
    const auto curSlot = GetStagedSlotOverride(charId, g_selectedSlot);
    const auto rows = BuildBrowserRows(g_selectedSlot, isDog);

    const std::size_t totalPages = std::max<std::size_t>(1, (rows.size() + kRowsPerPage - 1) / kRowsPerPage);
    if (g_currentPage >= totalPages)
    {
        g_currentPage = totalPages - 1;
    }

    // 1. Update Header & Preset Indicator
    std::wstring hdrText = std::wstring(SlotDisplayName(g_selectedSlot, isDog)) + L" [" + preset.name;
    if (st.hasStagedChanges)
    {
        hdrText += L" *";
    }
    hdrText += L"]";
    movie.SetVariable("_root.DAOTransmog_Browser.hdr.lbl.TextField.text", dao::gui::Value::WString(hdrText.c_str()));

    // Highlight active preset button label (Roman numerals I, II, III)
    for (std::uint8_t p = 0; p < 3; ++p)
    {
        const std::string prePath = "_root.DAOTransmog_Browser.pre" + std::to_string(p) + ".lbl.TextField.text";
        const std::wstring numeral = (p == 0) ? L"I" : ((p == 1) ? L"II" : L"III");
        const std::wstring preLbl = (st.activePreset % 3 == p) ? (L"[" + numeral + L"*]") : numeral;
        movie.SetVariable(prePath.c_str(), dao::gui::Value::WString(preLbl.c_str()));
    }

    // 2. Update the 8 Catalog Rows on Current Page
    for (std::size_t i = 0; i < kRowsPerPage; ++i)
    {
        const std::size_t idx = g_currentPage * kRowsPerPage + i;
        const std::string rowBase = "_root.DAOTransmog_Browser.row" + std::to_string(i);
        const std::string visPath = rowBase + "._visible";
        const std::string namePath = rowBase + ".lbl.TextField.text";
        const std::string subPath = rowBase + ".lblSub.TextField.text";
        const std::string hlPath = rowBase + ".hl._visible";
        const std::string iconFrameVisPath = rowBase + ".icon_frame._visible";
        const std::string iconMcPath = rowBase + ".icon_frame.icon_mc";

        if (idx < rows.size())
        {
            const auto &r = rows[idx];
            bool isSelected = false;
            if (g_selectedSlot == catalog::Slot::Helmet)
            {
                if (curSlot.mode == state::SlotMode::HideSlot)
                {
                    isSelected = (r.mode == state::SlotMode::HideSlot);
                }
                else if (curSlot.mode == state::SlotMode::HideDialogue && (curSlot.baseItem == 0xFFFF || curSlot.baseItem == 0))
                {
                    isSelected = (r.mode == state::SlotMode::HideDialogue);
                }
                else if (curSlot.mode == state::SlotMode::Real)
                {
                    isSelected = (r.mode == state::SlotMode::Real);
                }
                else
                {
                    isSelected = (r.mode == state::SlotMode::Override && r.baseItem == curSlot.baseItem &&
                                  r.variation == curSlot.variation);
                }
            }
            else
            {
                isSelected = (r.mode == curSlot.mode) &&
                             (r.mode != state::SlotMode::Override ||
                              (r.baseItem == curSlot.baseItem && r.variation == curSlot.variation));
            }

            const std::wstring rowText = isSelected ? (L">> " + r.label) : r.label;
            const std::uint32_t titleColor = isSelected ? 0xFFE6A0 : 0xEBDDC2;
            const std::uint32_t subColor = isSelected ? 0xD8C29D : 0xA69375;

            movie.SetVariable(visPath.c_str(), dao::gui::Value::Bool(true));
            movie.SetVariable(namePath.c_str(), dao::gui::Value::WString(rowText.c_str()));
            movie.SetVariable(subPath.c_str(), dao::gui::Value::WString(r.subtitle.c_str()));
            movie.SetVariable((rowBase + ".lbl.TextField.textColor").c_str(), dao::gui::Value::Number(titleColor));
            movie.SetVariable((rowBase + ".lblSub.TextField.textColor").c_str(), dao::gui::Value::Number(subColor));
            movie.SetVariable((rowBase + ".lbl.TextField.html").c_str(), dao::gui::Value::Bool(false));
            movie.SetVariable((rowBase + ".lblSub.TextField.html").c_str(), dao::gui::Value::Bool(false));
            movie.SetVariable(hlPath.c_str(), dao::gui::Value::Bool(isSelected));

            if (!r.icon.empty())
            {
                movie.SetVariable(iconFrameVisPath.c_str(), dao::gui::Value::Bool(true));
                const std::string imgUrl = "img://" + WideToUtf8(r.icon) + ".dds?Width=28&Height=28";
                const dao::gui::Value loadArgs[] = {dao::gui::Value::String(imgUrl.c_str())};
                movie.Invoke((iconMcPath + ".loadMovie").c_str(), std::span<const dao::gui::Value>(loadArgs));
            }
            else
            {
                movie.SetVariable(iconFrameVisPath.c_str(), dao::gui::Value::Bool(false));
                movie.Invoke((iconMcPath + ".unloadMovie").c_str());
            }
        }
        else
        {
            movie.SetVariable(visPath.c_str(), dao::gui::Value::Bool(false));
            movie.SetVariable(namePath.c_str(), dao::gui::Value::WString(L""));
            movie.SetVariable(subPath.c_str(), dao::gui::Value::WString(L""));
            movie.SetVariable(hlPath.c_str(), dao::gui::Value::Bool(false));
            movie.SetVariable(iconFrameVisPath.c_str(), dao::gui::Value::Bool(false));
        }
    }

    // 3. Update Pagination Label
    const std::wstring pgText = L"Page " + std::to_wstring(g_currentPage + 1) + L" / " + std::to_wstring(totalPages) +
                                L" (" + std::to_wstring(rows.size()) + L" looks)";
    movie.SetVariable("_root.DAOTransmog_Browser.pgInfo.lbl.TextField.text", dao::gui::Value::WString(pgText.c_str()));

    // 4. Update Material Label
    std::wstring matText = L"Mat: Equipped / Default";
    if (curSlot.material != 0xFF && curSlot.material != 0)
    {
        if (const auto *mat = catalog::FindMaterial(curSlot.material))
        {
            matText = L"Mat: " + mat->name;
        }
        else
        {
            matText = L"Mat: ID " + std::to_wstring(curSlot.material);
        }
    }
    movie.SetVariable("_root.DAOTransmog_Browser.matInfo.lbl.TextField.text", dao::gui::Value::WString(matText.c_str()));

    // 5. Update Heraldry / Helmet Visibility Label
    if (g_selectedSlot == catalog::Slot::Helmet)
    {
        std::wstring visText;
        switch (curSlot.mode)
        {
        case state::SlotMode::HideDialogue:
            visText = L"Helmet: Hide in Dialogue";
            break;
        case state::SlotMode::HideSlot:
            visText = L"Helmet: Always Hide";
            break;
        default:
            visText = L"Helmet: Always Show";
            break;
        }
        movie.SetVariable("_root.DAOTransmog_Browser.herInfo.lbl.TextField.text", dao::gui::Value::WString(visText.c_str()));
    }
    else
    {
        std::wstring herText = L"Heraldry: Default / None";
        if (curSlot.heraldry != 0)
        {
            if (const auto *her = catalog::FindHeraldry(curSlot.heraldry))
            {
                herText = L"Heraldry: " + her->label;
            }
            else
            {
                herText = L"Heraldry: ID " + std::to_wstring(curSlot.heraldry);
            }
        }
        movie.SetVariable("_root.DAOTransmog_Browser.herInfo.lbl.TextField.text", dao::gui::Value::WString(herText.c_str()));
    }
}

} // namespace

bool IsBrowserVisible()
{
    return g_browserVisible;
}

void SetBrowserVisible(bool visible)
{
    g_browserVisible = visible;
    auto movie = dao::gui::ScreenMovie(L"Inventory");
    if (movie)
    {
        EnsureUiInitialized(*movie);
        movie->SetVariable("_root.DAOTransmog_Browser._visible", dao::gui::Value::Bool(visible));
        movie->SetVariable("_root.DAOTransmog_mc._visible", dao::gui::Value::Bool(true));
        movie->SetVariable("_root.InventoryContents_mc._visible", dao::gui::Value::Bool(!visible));
        movie->SetVariable("_root.ItemCategories_mc._visible", dao::gui::Value::Bool(!visible));
        movie->SetVariable("_root.ItemSortPicker_mc._visible", dao::gui::Value::Bool(!visible));
        movie->SetVariable("_root.DAOTransmog_mc.lbl.TextField.text",
                           dao::gui::Value::WString(visible ? L"Backpack [T]" : L"Transmog [T]"));
    }
}

void ToggleWardrobeBrowser()
{
    static ULONGLONG s_lastToggleTime = 0;
    const ULONGLONG now = GetTickCount64();
    if (now - s_lastToggleTime < 250)
    {
        spdlog::debug("[DAOTransmog::Gui] ToggleWardrobeBrowser debounced ({}ms since last toggle)",
                      now - s_lastToggleTime);
        return;
    }
    s_lastToggleTime = now;

    SetBrowserVisible(!g_browserVisible);
    const auto charId = CurrentCharacter();
    if (charId != dao::kInvalidObject)
    {
        if (g_browserVisible)
        {
            state::BeginStaging(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
        else
        {
            // Closing Wardrobe Browser without applying discards uncommitted staged changes
            auto *st = state::Find(charId);
            if (st != nullptr && st->hasStagedChanges)
            {
                state::DiscardStaged(charId);
                RefreshPaperdoll(charId);
            }
            if (g_isHeadZoomed)
            {
                ZoomPaperdollHead(false);
            }
        }
    }
}

void RefreshPaperdoll(dao::ObjectId charId, bool fullRebuild)
{
    appearance::RefreshAppearance(charId, fullRebuild);
    weapon::RefreshCreatureWeapons(charId);
}

void ZoomPaperdollHead(bool zoomIn)
{
    g_isHeadZoomed = zoomIn;
    const auto charId = CurrentCharacter();
    if (charId == dao::kInvalidObject)
    {
        return;
    }

    if (zoomIn)
    {
        dao::portraits::SetCamera(charId, {
                                              .pitch = 0.0f,
                                              .rotation = 0.0f,
                                              .distance = 1.1f,
                                              .zAdjust = 0.1f,
                                          });
    }
    else
    {
        dao::portraits::ClearCamera(charId);
    }
}

void Init()
{
    if (!safemode::IsActive(safemode::Subsystem::GuiBridge))
    {
        spdlog::warn("[DAOTransmog::Gui] Subsystem disabled by SafeMode");
        return;
    }

    // Patch inventory.gfx on load with native Transmog UI bytecode
    dao::gui::OnMovieLoad(L"inventory", [](std::span<const std::byte> movie) {
        spdlog::info("[DAOTransmog::Gui] Patching inventory.gfx with Transmog UI DoAction tag");
        auto patched = dao::core::gfx::AppendDoAction(movie, kTransmogUiActions);
        if (patched)
        {
            spdlog::info("[DAOTransmog::Gui] Successfully patched inventory.gfx (size: {} -> {})", movie.size(),
                         patched->size());
        }
        else
        {
            spdlog::error("[DAOTransmog::Gui] Failed to patch inventory.gfx with AppendDoAction");
        }
        return patched;
    });

    // Monitor full-screen transitions for Inventory
    dao::gui::OnFullScreen([](std::wstring_view name, bool shown) {
        if (name != L"Inventory")
        {
            return;
        }

        const auto charId = CurrentCharacter();
        if (shown)
        {
            s_lastScreenChar = dao::kInvalidObject;
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                EnsureUiInitialized(*movie);
                movie->SetVariable("_root.DAOTransmog_mc._visible", dao::gui::Value::Bool(true));
                movie->SetVariable("_root.DAOTransmog_mc.lbl.TextField.text", dao::gui::Value::WString(L"Transmog [T]"));
                movie->SetVariable("_root.DAOTransmog_Browser._visible", dao::gui::Value::Bool(false));
                movie->SetVariable("_root.InventoryContents_mc._visible", dao::gui::Value::Bool(true));
                movie->SetVariable("_root.ItemCategories_mc._visible", dao::gui::Value::Bool(true));
                movie->SetVariable("_root.ItemSortPicker_mc._visible", dao::gui::Value::Bool(true));
                const bool hasBtn = movie->IsAvailable("_root.DAOTransmog_mc");
                const bool hasBrowser = movie->IsAvailable("_root.DAOTransmog_Browser");
                spdlog::info("[DAOTransmog::Gui] Inventory opened; button={}, browser={}", hasBtn, hasBrowser);
            }
        }
        else
        {
            state::DiscardAllStaged();
            if (s_lastScreenChar != dao::kInvalidObject)
            {
                RefreshPaperdoll(s_lastScreenChar, /*fullRebuild=*/true);
            }
            if (charId != dao::kInvalidObject && charId != s_lastScreenChar)
            {
                RefreshPaperdoll(charId, /*fullRebuild=*/true);
            }
            for (const auto member : dao::party::Selected())
            {
                RefreshPaperdoll(member, /*fullRebuild=*/true);
            }
            s_lastScreenChar = dao::kInvalidObject;
            g_browserVisible = false;
            if (g_isHeadZoomed)
            {
                ZoomPaperdollHead(false);
            }
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                movie->SetVariable("_root.InventoryContents_mc._visible", dao::gui::Value::Bool(true));
                movie->SetVariable("_root.ItemCategories_mc._visible", dao::gui::Value::Bool(true));
                movie->SetVariable("_root.ItemSortPicker_mc._visible", dao::gui::Value::Bool(true));
                movie->SetVariable("_root.DAOTransmog_Browser._visible", dao::gui::Value::Bool(false));
                movie->SetVariable("_root.DAOTransmog_mc.lbl.TextField.text", dao::gui::Value::WString(L"Transmog [T]"));
            }
        }
    });

    // FSCommand: Toggle Wardrobe Browser
    dao::gui::OnFSCommand("DAOTransmog_Toggle", [](void *, std::string_view) {
        ToggleWardrobeBrowser();
    });

    // FSCommand: Select Slot Tab (0..5)
    dao::gui::OnFSCommand("DAOTransmog_SelectSlot", [](void *, std::string_view args) {
        try
        {
            const int slotNum = std::stoi(std::string(args));
            const auto charId = CurrentCharacter();
            const bool isDog = IsDog(charId);
            if (isDog && slotNum != 0 && slotNum != 1)
            {
                return;
            }
            g_selectedSlot = static_cast<catalog::Slot>(std::clamp(slotNum, 0, 6));
            g_currentPage = 0;

            ZoomPaperdollHead(!isDog && g_selectedSlot == catalog::Slot::Helmet);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie && charId != dao::kInvalidObject)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
        catch (...)
        {
        }
    });

    // FSCommand: Click Appearance Row (0..9)
    dao::gui::OnFSCommand("DAOTransmog_ClickRow", [](void *, std::string_view args) {
        try
        {
            const int rowIdx = std::stoi(std::string(args));
            if (rowIdx < 0 || static_cast<std::size_t>(rowIdx) >= kRowsPerPage)
            {
                return;
            }

            const auto charId = CurrentCharacter();
            if (charId == dao::kInvalidObject)
            {
                return;
            }

            const auto rows = BuildBrowserRows(g_selectedSlot);
            const std::size_t itemIdx = g_currentPage * kRowsPerPage + static_cast<std::size_t>(rowIdx);
            if (itemIdx >= rows.size())
            {
                return;
            }

            const auto &picked = rows[itemIdx];
            auto cur = GetStagedSlotOverride(charId, g_selectedSlot);
            if (picked.mode == state::SlotMode::Real)
            {
                cur.mode = state::SlotMode::Real;
                cur.baseItem = 0xFFFF;
                cur.variation = 0xFF;
                cur.material = 0xFF;
                cur.heraldry = 0;
            }
            else if (g_selectedSlot == catalog::Slot::Helmet)
            {
                if (picked.mode == state::SlotMode::Override)
                {
                    cur.mode = (cur.mode == state::SlotMode::HideDialogue) ? state::SlotMode::HideDialogue
                                                                          : state::SlotMode::Override;
                    cur.baseItem = picked.baseItem;
                    cur.variation = picked.variation;
                    cur.material = picked.defaultMaterial;
                }
                else if (picked.mode == state::SlotMode::HideDialogue)
                {
                    cur.mode = state::SlotMode::HideDialogue;
                }
                else if (picked.mode == state::SlotMode::HideSlot)
                {
                    cur.mode = state::SlotMode::HideSlot;
                }
            }
            else
            {
                cur.mode = picked.mode;
                cur.baseItem = picked.baseItem;
                cur.variation = picked.variation;
                cur.material = picked.defaultMaterial;
            }

            ApplyStagedSlotOverride(charId, g_selectedSlot, cur);
            RefreshPaperdoll(charId);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
        catch (...)
        {
        }
    });

    // FSCommand: Page Prev/Next (-1 or 1)
    dao::gui::OnFSCommand("DAOTransmog_Page", [](void *, std::string_view args) {
        try
        {
            const int delta = std::stoi(std::string(args));
            const auto rows = BuildBrowserRows(g_selectedSlot);
            const std::size_t totalPages = std::max<std::size_t>(1, (rows.size() + kRowsPerPage - 1) / kRowsPerPage);

            if (delta < 0)
            {
                g_currentPage = (g_currentPage > 0) ? (g_currentPage - 1) : (totalPages - 1);
            }
            else if (delta > 0)
            {
                g_currentPage = (g_currentPage + 1 < totalPages) ? (g_currentPage + 1) : 0;
            }

            const auto charId = CurrentCharacter();
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie && charId != dao::kInvalidObject)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
        catch (...)
        {
        }
    });

    // FSCommand: Cycle Material (-1 or 1)
    dao::gui::OnFSCommand("DAOTransmog_Mat", [](void *, std::string_view args) {
        try
        {
            const int delta = std::stoi(std::string(args));
            const auto charId = CurrentCharacter();
            if (charId == dao::kInvalidObject)
            {
                return;
            }

            const auto mats = catalog::Materials();
            if (mats.empty())
            {
                return;
            }

            auto cur = GetStagedSlotOverride(charId, g_selectedSlot);
            int curIdx = -1;
            for (std::size_t i = 0; i < mats.size(); ++i)
            {
                if (mats[i].id == cur.material)
                {
                    curIdx = static_cast<int>(i);
                    break;
                }
            }

            const int total = static_cast<int>(mats.size());
            int nextIdx = curIdx + delta;
            if (nextIdx < -1)
            {
                nextIdx = total - 1;
            }
            else if (nextIdx >= total)
            {
                nextIdx = -1;
            }

            cur.material = (nextIdx < 0) ? 0xFF : mats[static_cast<std::size_t>(nextIdx)].id;
            ApplyStagedSlotOverride(charId, g_selectedSlot, cur);
            RefreshPaperdoll(charId);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
        catch (...)
        {
        }
    });

    // FSCommand: Cycle Heraldry (-1 or 1) OR Cycle Helmet Visibility when on Helmet slot
    dao::gui::OnFSCommand("DAOTransmog_Her", [](void *, std::string_view args) {
        try
        {
            const int delta = std::stoi(std::string(args));
            const auto charId = CurrentCharacter();
            if (charId == dao::kInvalidObject)
            {
                return;
            }

            if (g_selectedSlot == catalog::Slot::Helmet)
            {
                auto cur = GetStagedSlotOverride(charId, g_selectedSlot);
                // 3-way cycle:
                // 0: Always Show (Override if baseItem != 0xFFFF && baseItem != 0, else Real)
                // 1: Hide in Dialogue (HideDialogue)
                // 2: Always Hide (HideSlot)
                int currentModeIndex = 0;
                if (cur.mode == state::SlotMode::HideDialogue)
                {
                    currentModeIndex = 1;
                }
                else if (cur.mode == state::SlotMode::HideSlot)
                {
                    currentModeIndex = 2;
                }
                else
                {
                    currentModeIndex = 0;
                }

                int nextModeIndex = (currentModeIndex + delta) % 3;
                if (nextModeIndex < 0)
                {
                    nextModeIndex += 3;
                }

                if (nextModeIndex == 1)
                {
                    cur.mode = state::SlotMode::HideDialogue;
                }
                else if (nextModeIndex == 2)
                {
                    cur.mode = state::SlotMode::HideSlot;
                }
                else
                {
                    cur.mode = (cur.baseItem != 0xFFFF && cur.baseItem != 0) ? state::SlotMode::Override
                                                                           : state::SlotMode::Real;
                }

                ApplyStagedSlotOverride(charId, g_selectedSlot, cur);
                RefreshPaperdoll(charId, /*fullRebuild=*/false);

                auto movie = dao::gui::ScreenMovie(L"Inventory");
                if (movie)
                {
                    RefreshBrowserUI(*movie, charId);
                }
                return;
            }

            const auto hers = catalog::Heraldries();
            if (hers.empty())
            {
                return;
            }

            auto cur = GetStagedSlotOverride(charId, g_selectedSlot);
            int curIdx = -1;
            for (std::size_t i = 0; i < hers.size(); ++i)
            {
                if (hers[i].id == cur.heraldry)
                {
                    curIdx = static_cast<int>(i);
                    break;
                }
            }

            const int total = static_cast<int>(hers.size());
            int nextIdx = curIdx + delta;
            if (nextIdx < -1)
            {
                nextIdx = total - 1;
            }
            else if (nextIdx >= total)
            {
                nextIdx = -1;
            }

            cur.heraldry = (nextIdx < 0) ? 0 : hers[static_cast<std::size_t>(nextIdx)].id;
            ApplyStagedSlotOverride(charId, g_selectedSlot, cur);
            RefreshPaperdoll(charId);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
        catch (...)
        {
        }
    });

    // FSCommand: Select Outfit Preset (0, 1, 2)
    dao::gui::OnFSCommand("DAOTransmog_Preset", [](void *, std::string_view args) {
        try
        {
            const int presetIdx = std::clamp(std::stoi(std::string(args)), 0, 2);
            const auto charId = CurrentCharacter();
            if (charId != dao::kInvalidObject)
            {
                state::SwitchPreset(charId, presetIdx);
                RefreshPaperdoll(charId);

                auto movie = dao::gui::ScreenMovie(L"Inventory");
                if (movie)
                {
                    RefreshBrowserUI(*movie, charId);
                }
            }
        }
        catch (...)
        {
        }
    });

    // FSCommand: Sync Material & Heraldry to Chest
    dao::gui::OnFSCommand("DAOTransmog_SyncAll", [](void *, std::string_view) {
        const auto charId = CurrentCharacter();
        auto *st = state::Find(charId);
        if (st != nullptr)
        {
            st->stagedPreview.MatchMaterialToChest();
            st->hasStagedChanges = true;
            RefreshPaperdoll(charId);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
    });

    // FSCommand: Reset Current Slot to Real Look
    dao::gui::OnFSCommand("DAOTransmog_ResetSlot", [](void *, std::string_view) {
        const auto charId = CurrentCharacter();
        if (charId != dao::kInvalidObject)
        {
            ApplyStagedSlotOverride(charId, g_selectedSlot, state::SlotOverride{});
            RefreshPaperdoll(charId);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
    });

    // FSCommand: Apply / Commit Staged Outfit
    dao::gui::OnFSCommand("DAOTransmog_Apply", [](void *, std::string_view) {
        const auto charId = CurrentCharacter();
        if (charId != dao::kInvalidObject)
        {
            state::CommitStaged(charId);
            RefreshPaperdoll(charId, /*fullRebuild=*/true);
            portrait::RefreshPortrait(charId);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
    });

    // FSCommand: Discard Staged Changes
    dao::gui::OnFSCommand("DAOTransmog_Discard", [](void *, std::string_view) {
        const auto charId = CurrentCharacter();
        if (charId != dao::kInvalidObject)
        {
            state::DiscardStaged(charId);
            RefreshPaperdoll(charId);

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
    });

    // FSCommand: Toggle Head Zoom
    dao::gui::OnFSCommand("DAOTransmog_ZoomToggle", [](void *, std::string_view) {
        ZoomPaperdollHead(!g_isHeadZoomed);
    });

    // Register Hotkeys
    // P: Cycle presets
    config::SetOnCyclePresetCallback([]() {
        const auto charId = CurrentCharacter();
        if (charId != dao::kInvalidObject)
        {
            state::CyclePreset(charId);
            RefreshPaperdoll(charId, /*fullRebuild=*/true);
            portrait::RefreshPortrait(charId);
            spdlog::info("[DAOTransmog::Gui] Cycled outfit preset for character {:#x}", charId);
        }
    });

    // H: Toggle helmet mode (Show -> Hide in Dialogue -> Hide -> Show)
    config::SetOnToggleHelmetCallback([]() {
        const auto charId = CurrentCharacter();
        if (charId == dao::kInvalidObject || IsDog(charId))
        {
            return;
        }
        auto *st = state::Find(charId);
        if (st != nullptr)
        {
            auto &active = st->ActivePreset();
            auto &helm = active.armorSlots[static_cast<std::size_t>(catalog::Slot::Helmet)];
            switch (helm.mode)
            {
            case state::SlotMode::Real:
            case state::SlotMode::Override:
                helm.mode = state::SlotMode::HideDialogue;
                break;
            case state::SlotMode::HideDialogue:
                helm.mode = state::SlotMode::HideSlot;
                break;
            case state::SlotMode::HideSlot:
                helm.mode = (helm.baseItem != 0xFFFF && helm.baseItem != 0) ? state::SlotMode::Override
                                                                           : state::SlotMode::Real;
                break;
            }
            RefreshPaperdoll(charId, /*fullRebuild=*/false);
            portrait::RefreshPortrait(charId);
            spdlog::info("[DAOTransmog::Gui] Toggled helmet visibility mode to {}", static_cast<int>(helm.mode));

            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie && g_browserVisible)
            {
                RefreshBrowserUI(*movie, charId);
            }
        }
    });

    // T: Toggle Transmog wardrobe when inventory is open
    config::SetOnToggleTransmogCallback([]() {
        auto movie = dao::gui::ScreenMovie(L"Inventory");
        if (movie)
        {
            ToggleWardrobeBrowser();
            spdlog::info("[DAOTransmog::Gui] Toggled Transmog wardrobe via hotkey T");
        }
    });

    // Frame poll: background catalog scan & raw keystate edge-detection for 'T'
    dao::OnFrame([] {
        // 1. Initial background catalog scan when GDA tables are ready
        static bool s_scanned = false;
        if (!s_scanned)
        {
            if (catalog::EnsureScanned())
            {
                s_scanned = true;
                spdlog::info("[DAOTransmog::Catalog] Initial catalog scan completed on game thread");
            }
        }

        // 2. Poll hotkey 'T' during Inventory screen
        static bool s_tWasDown = false;
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg != nullptr)
        {
            GetWindowThreadProcessId(fg, &fgPid);
        }
        const bool gameFocused = (fgPid == GetCurrentProcessId());
        const bool tIsDown = dao::input::Down(dao::input::Key::T) ||
                             (gameFocused && ((GetAsyncKeyState('T') & 0x8000) != 0));
        if (tIsDown && !s_tWasDown)
        {
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                ToggleWardrobeBrowser();
                spdlog::info("[DAOTransmog::Gui] Toggled Transmog wardrobe via hotkey T (OnFrame, visible={})",
                             g_browserVisible);
            }
        }
        // 3. Detect character switch during inventory and refresh wardrobe browser
        const auto curChar = CurrentCharacter();
        if (curChar != s_lastScreenChar)
        {
            if (s_lastScreenChar != dao::kInvalidObject)
            {
                // Discard any uncommitted staging on the previous character before switching
                auto *st = state::Find(s_lastScreenChar);
                if (st != nullptr && st->hasStagedChanges)
                {
                    state::DiscardStaged(s_lastScreenChar);
                    RefreshPaperdoll(s_lastScreenChar);
                }
            }
            s_lastScreenChar = curChar;
            if (g_browserVisible && curChar != dao::kInvalidObject)
            {
                if (IsDog(curChar))
                {
                    if (g_selectedSlot != catalog::Slot::Chest && g_selectedSlot != catalog::Slot::Gloves)
                    {
                        g_selectedSlot = catalog::Slot::Chest;
                    }
                }
                state::BeginStaging(curChar);
                auto movie = dao::gui::ScreenMovie(L"Inventory");
                if (movie)
                {
                    RefreshBrowserUI(*movie, curChar);
                }
            }
        }
        s_tWasDown = tIsDown;
    });

    spdlog::info("[DAOTransmog::Gui] GuiBridge initialized");
}

bool SimulateUiAction(std::string_view command, std::string_view args)
{
    if (command == "DAOTransmog_Toggle")
    {
        ToggleWardrobeBrowser();
        return true;
    }
    if (command == "DAOTransmog_SelectSlot")
    {
        try
        {
            const int slotNum = std::stoi(std::string(args));
            const auto charId = CurrentCharacter();
            const bool isDog = IsDog(charId);
            if (isDog && slotNum != 0 && slotNum != 1)
            {
                return false;
            }
            g_selectedSlot = static_cast<catalog::Slot>(std::clamp(slotNum, 0, 6));
            g_currentPage = 0;
            ZoomPaperdollHead(!isDog && g_selectedSlot == catalog::Slot::Helmet);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie && charId != dao::kInvalidObject)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    if (command == "DAOTransmog_ClickRow")
    {
        try
        {
            const int rowIdx = std::stoi(std::string(args));
            if (rowIdx < 0 || static_cast<std::size_t>(rowIdx) >= kRowsPerPage)
            {
                return false;
            }
            const auto charId = CurrentCharacter();
            if (charId == dao::kInvalidObject)
            {
                return false;
            }
            const auto rows = BuildBrowserRows(g_selectedSlot);
            const std::size_t itemIdx = g_currentPage * kRowsPerPage + static_cast<std::size_t>(rowIdx);
            if (itemIdx >= rows.size())
            {
                return false;
            }

            const auto &picked = rows[itemIdx];
            auto cur = GetStagedSlotOverride(charId, g_selectedSlot);
            if (picked.mode == state::SlotMode::Real)
            {
                cur.mode = state::SlotMode::Real;
                cur.baseItem = 0xFFFF;
                cur.variation = 0xFF;
                cur.material = 0xFF;
                cur.heraldry = 0;
            }
            else if (g_selectedSlot == catalog::Slot::Helmet)
            {
                if (picked.mode == state::SlotMode::Override)
                {
                    cur.mode = (cur.mode == state::SlotMode::HideDialogue) ? state::SlotMode::HideDialogue
                                                                           : state::SlotMode::Override;
                    cur.baseItem = picked.baseItem;
                    cur.variation = picked.variation;
                    cur.material = picked.defaultMaterial;
                }
                else if (picked.mode == state::SlotMode::HideDialogue)
                {
                    cur.mode = state::SlotMode::HideDialogue;
                }
                else if (picked.mode == state::SlotMode::HideSlot)
                {
                    cur.mode = state::SlotMode::HideSlot;
                }
            }
            else
            {
                cur.mode = picked.mode;
                cur.baseItem = picked.baseItem;
                cur.variation = picked.variation;
                cur.material = picked.defaultMaterial;
            }
            ApplyStagedSlotOverride(charId, g_selectedSlot, cur);
            RefreshPaperdoll(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    if (command == "DAOTransmog_Page")
    {
        try
        {
            const int delta = std::stoi(std::string(args));
            const auto rows = BuildBrowserRows(g_selectedSlot);
            const std::size_t totalPages = std::max<std::size_t>(1, (rows.size() + kRowsPerPage - 1) / kRowsPerPage);
            if (delta > 0 && g_currentPage + 1 < totalPages)
            {
                ++g_currentPage;
            }
            else if (delta < 0 && g_currentPage > 0)
            {
                --g_currentPage;
            }
            const auto charId = CurrentCharacter();
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie && charId != dao::kInvalidObject)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    if (command == "DAOTransmog_Mat")
    {
        try
        {
            const int delta = std::stoi(std::string(args));
            const auto charId = CurrentCharacter();
            if (charId == dao::kInvalidObject)
            {
                return false;
            }
            const auto mats = catalog::Materials();
            if (mats.empty())
            {
                return false;
            }
            auto cur = GetStagedSlotOverride(charId, g_selectedSlot);
            int curIdx = -1;
            for (std::size_t i = 0; i < mats.size(); ++i)
            {
                if (mats[i].id == cur.material)
                {
                    curIdx = static_cast<int>(i);
                    break;
                }
            }
            const int total = static_cast<int>(mats.size());
            int nextIdx = curIdx + delta;
            if (nextIdx < -1)
            {
                nextIdx = total - 1;
            }
            else if (nextIdx >= total)
            {
                nextIdx = -1;
            }
            cur.material = (nextIdx < 0) ? 0xFF : mats[static_cast<std::size_t>(nextIdx)].id;
            ApplyStagedSlotOverride(charId, g_selectedSlot, cur);
            RefreshPaperdoll(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    if (command == "DAOTransmog_Her")
    {
        try
        {
            const int delta = std::stoi(std::string(args));
            const auto charId = CurrentCharacter();
            if (charId == dao::kInvalidObject)
            {
                return false;
            }
            if (g_selectedSlot == catalog::Slot::Helmet)
            {
                auto cur = GetStagedSlotOverride(charId, catalog::Slot::Helmet);
                int modeVal = 0;
                switch (cur.mode)
                {
                case state::SlotMode::Real:
                case state::SlotMode::Override:
                    modeVal = 0;
                    break;
                case state::SlotMode::HideDialogue:
                    modeVal = 1;
                    break;
                case state::SlotMode::HideSlot:
                    modeVal = 2;
                    break;
                }
                modeVal = (modeVal + delta + 3) % 3;
                switch (modeVal)
                {
                case 0:
                    cur.mode = (cur.baseItem != 0xFFFF) ? state::SlotMode::Override : state::SlotMode::Real;
                    break;
                case 1:
                    cur.mode = state::SlotMode::HideDialogue;
                    break;
                case 2:
                    cur.mode = state::SlotMode::HideSlot;
                    break;
                }
                ApplyStagedSlotOverride(charId, catalog::Slot::Helmet, cur);
                RefreshPaperdoll(charId);
                auto movie = dao::gui::ScreenMovie(L"Inventory");
                if (movie)
                {
                    RefreshBrowserUI(*movie, charId);
                }
                return true;
            }
            const auto hers = catalog::Heraldries();
            if (hers.empty())
            {
                return false;
            }
            auto cur = GetStagedSlotOverride(charId, g_selectedSlot);
            int curIdx = -1;
            for (std::size_t i = 0; i < hers.size(); ++i)
            {
                if (hers[i].id == cur.heraldry)
                {
                    curIdx = static_cast<int>(i);
                    break;
                }
            }
            const int total = static_cast<int>(hers.size());
            int nextIdx = curIdx + delta;
            if (nextIdx < -1)
            {
                nextIdx = total - 1;
            }
            else if (nextIdx >= total)
            {
                nextIdx = -1;
            }
            cur.heraldry = (nextIdx < 0) ? 0 : hers[static_cast<std::size_t>(nextIdx)].id;
            ApplyStagedSlotOverride(charId, g_selectedSlot, cur);
            RefreshPaperdoll(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
    if (command == "DAOTransmog_SyncAll")
    {
        const auto charId = CurrentCharacter();
        auto *st = state::Find(charId);
        if (st != nullptr)
        {
            st->stagedPreview.MatchMaterialToChest();
            st->hasStagedChanges = true;
            RefreshPaperdoll(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        return false;
    }
    if (command == "DAOTransmog_Preset")
    {
        try
        {
            const int presetIdx = std::clamp(std::stoi(std::string(args)), 0, 2);
            const auto charId = CurrentCharacter();
            if (charId != dao::kInvalidObject)
            {
                state::SwitchPreset(charId, presetIdx);
                RefreshPaperdoll(charId);
                auto movie = dao::gui::ScreenMovie(L"Inventory");
                if (movie)
                {
                    RefreshBrowserUI(*movie, charId);
                }
                return true;
            }
        }
        catch (...)
        {
            return false;
        }
    }
    if (command == "DAOTransmog_Apply")
    {
        const auto charId = CurrentCharacter();
        if (charId != dao::kInvalidObject)
        {
            state::CommitStaged(charId);
            RefreshPaperdoll(charId, /*fullRebuild=*/true);
            portrait::RefreshPortrait(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        return false;
    }
    if (command == "DAOTransmog_ResetSlot")
    {
        const auto charId = CurrentCharacter();
        if (charId != dao::kInvalidObject)
        {
            ApplyStagedSlotOverride(charId, g_selectedSlot, state::SlotOverride{});
            RefreshPaperdoll(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        return false;
    }
    if (command == "DAOTransmog_Discard")
    {
        const auto charId = CurrentCharacter();
        if (charId != dao::kInvalidObject)
        {
            state::DiscardStaged(charId);
            RefreshPaperdoll(charId);
            auto movie = dao::gui::ScreenMovie(L"Inventory");
            if (movie)
            {
                RefreshBrowserUI(*movie, charId);
            }
            return true;
        }
        return false;
    }
    if (command == "DAOTransmog_ZoomToggle")
    {
        ZoomPaperdollHead(!g_isHeadZoomed);
        return true;
    }
    return false;
}

UiVariableResult QueryInventoryUiVariable(const char *path)
{
    UiVariableResult res;
    auto movie = dao::gui::ScreenMovie(L"Inventory");
    if (!movie || !movie->IsAvailable(path))
    {
        return res;
    }
    res.found = true;
    auto ans = movie->Get(path);
    if (!ans)
    {
        res.type = "nullopt";
        return res;
    }
    switch (ans->type)
    {
    case dao::gui::Value::Type::Boolean:
        res.type = "bool";
        res.boolVal = ans->boolean;
        break;
    case dao::gui::Value::Type::Number:
        res.type = "number";
        res.numVal = ans->number;
        break;
    case dao::gui::Value::Type::String:
    case dao::gui::Value::Type::StringW:
        res.type = "string";
        res.strVal = ans->text;
        break;
    default:
        res.type = "undefined";
        break;
    }
    return res;
}

dao::ObjectId GetCurrentCharacterId()
{
    return CurrentCharacter();
}

} // namespace transmog::gui
