#pragma once

#include "Catalog.h"
#include "State.h"
#include "dao/ObjectId.h"

namespace transmog::portrait
{

// Initializes portrait rendering detour
void Init();

// Detaches portrait detour cleanly
void Detach();

// Triggers immediate live refresh of creature's 2D UI portrait
void RefreshPortrait(dao::ObjectId creatureId);

} // namespace transmog::portrait
