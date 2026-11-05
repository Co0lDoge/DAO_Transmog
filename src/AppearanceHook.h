#pragma once

#include "Catalog.h"
#include "State.h"
#include "dao/ObjectId.h"

namespace transmog::appearance
{

// Initializes appearance detours with SafeMode protection
void Init();

// Detaches appearance hooks cleanly
void Detach();

// Triggers appearance re-evaluation and paperdoll refresh for a creature
// fullRebuild = false: lightweight preview update (skipFull = 1, no position shift)
// fullRebuild = true: full 3D world model rebuild (skipFull = 0, with position protection)
void RefreshAppearance(dao::ObjectId creatureId, bool fullRebuild = false);

} // namespace transmog::appearance
