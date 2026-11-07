#pragma once

#include "Catalog.h"
#include "State.h"
#include "dao/ObjectId.h"

namespace transmog::gui
{

// Initializes GUI listeners, FSCommands, ExternalInterface methods, and hotkey callbacks
void Init();

// Toggles wardrobe browser visibility in inventory screen
void ToggleWardrobeBrowser();
void SetBrowserVisible(bool visible);
bool IsBrowserVisible();

// Refreshes the 3D paperdoll figure in the inventory screen
// fullRebuild = false: fast preview update; fullRebuild = true: full 3D world model rebuild
void RefreshPaperdoll(dao::ObjectId charId, bool fullRebuild = false);

// Zooms the paperdoll camera to head node for helmet inspection
void ZoomPaperdollHead(bool zoomIn);

// Test Bridge UI Inspection & Automation
struct UiVariableResult
{
    bool found = false;
    std::string type; // "bool", "number", "string", "undefined"
    bool boolVal = false;
    double numVal = 0.0;
    std::string strVal;
};

UiVariableResult QueryInventoryUiVariable(const char *path);
bool SimulateUiAction(std::string_view command, std::string_view arg = "");
dao::ObjectId GetCurrentCharacterId();

} // namespace transmog::gui
