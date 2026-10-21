// The board-side glue between our controller and cspot (see spotify_cspot.cpp).
//
// Declared here so both the adapter and the boot code agree on the contract, and so the
// factory can be a plain function pointer (cspot's SpircHandler is held in the .cpp).
#pragma once
#include <memory>
#include <string>

#include "spotify_session.h"

namespace cspot { class SpircHandler; }

// Hand cspot's session to the adapter, then install cspotMakeSession as the factory.
void cspotAttach(std::shared_ptr<cspot::SpircHandler> handler);
std::unique_ptr<ISpotifySession> cspotMakeSession();

// Forward cspot's PLAY_PAUSE event here from the framework's own event handler.
void cspotNotifyPlayPause(bool paused);
