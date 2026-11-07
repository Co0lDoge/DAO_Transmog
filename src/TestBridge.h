#pragma once

#include <string>

namespace transmog::testbridge
{

// Initializes the in-game test bridge named pipe server and game-frame listener.
void Init();

// Shuts down the test bridge server thread.
void Shutdown();

} // namespace transmog::testbridge
