#pragma once

#include "Catalog.h"
#include "State.h"
#include "dao/ObjectId.h"

#include <cstdint>
#include <string_view>

namespace transmog::context
{

// Checks whether transmog should be completely bypassed for a creature
// (e.g. plot disguise active, naked/captured party member, global toggle off)
bool ShouldBypassTransmog(dao::ObjectId creatureId);

// Checks if the game is currently rendering a dialogue conversation or cinematic cutscene
bool IsInDialogueOrCutscene();

// Determines if a helmet should be hidden based on slot mode and game context
bool ShouldHideHelmet(dao::ObjectId creatureId, state::SlotMode mode);

// Checks if the creature is currently wearing a plot disguise
bool IsDisguised(dao::ObjectId creatureId);

// Checks if party is currently in player camp and auto-switches to camp preset
void CheckCampAutoSwitch();

// Registers lifecycle and area listeners with DAOLib
void Init();

} // namespace transmog::context
