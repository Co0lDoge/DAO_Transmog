#pragma once

#include "Catalog.h"
#include "State.h"
#include "dao/ObjectId.h"

namespace transmog::weapon
{

// Initializes weapon & shield attachment detours
void Init();

// Detaches weapon detours cleanly
void Detach();

// Triggers weapon visual refresh for a creature
void RefreshCreatureWeapons(dao::ObjectId creatureId);

} // namespace transmog::weapon
