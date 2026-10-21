// The radio, kept separate from provisioning so the parsing and NVS code stays testable on
// the host (this file needs WiFi.h, which only exists on the device).
#pragma once
#include <Arduino.h>

// Starts a connection with the stored credentials. False when none are stored, or when the
// radio could not be started.
bool wifiStart();
bool wifiConnected();
void wifiPoll();            // retries (every 15 s) while credentials exist and it is down
String wifiStatusLine();    // for the serial console
