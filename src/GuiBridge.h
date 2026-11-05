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

} // namespace transmog::gui
