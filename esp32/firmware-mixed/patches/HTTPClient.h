#pragma once
// MIXED-BUILD PATCH (probe project only).
//
// bell ships its own bell::HTTPClient, reached from cspot headers via the quoted include
// `#include "HTTPClient.h"`. In a pure ESP-IDF build that resolves against bell's
// main/io/include. In an Arduino-as-a-component build the Arduino core ALSO exports a
// global ::HTTPClient (libraries/HTTPClient/src) into the include path, and depending on
// include ordering that one wins - `bell::HTTPClient` then "is not a member of bell".
//
// A quoted include is resolved against the includer's own directory first, and cspot's
// headers live in cspot/include, so this shim wins deterministically and forwards to the
// real bell header.
#include "../bell/main/io/include/HTTPClient.h"
