#include "Config.h"
#include "ContextRules.h"
#include "SafeMode.h"
#include "State.h"

#include <catch2/catch_test_macros.hpp>

using namespace transmog::context;
using namespace transmog::state;

TEST_CASE("Helmet visibility rules", "[context]")
{
    const dao::ObjectId testChar = 0x2001;

    SECTION("Always Hide slot mode")
    {
        CHECK(ShouldHideHelmet(testChar, SlotMode::HideSlot) == true);
    }

    SECTION("Real or Override slot mode in normal exploration")
    {
        // Outside dialogue/cutscene
        CHECK(ShouldHideHelmet(testChar, SlotMode::Real) == false);
        CHECK(ShouldHideHelmet(testChar, SlotMode::Override) == false);
    }

    SECTION("Hide in Dialogue mode outside dialogue")
    {
        // If not in dialogue, helmet should not be hidden
        if (!IsInDialogueOrCutscene())
        {
            CHECK(ShouldHideHelmet(testChar, SlotMode::HideDialogue) == false);
        }
    }
}

TEST_CASE("Transmog bypass rules", "[context]")
{
    const dao::ObjectId testChar = 0x2002;

    SECTION("Global enabled state")
    {
        // When enabled and not disguised or naked, bypass is false
        if (transmog::config::Enabled())
        {
            CHECK(ShouldBypassTransmog(testChar) == false);
        }
    }
}
