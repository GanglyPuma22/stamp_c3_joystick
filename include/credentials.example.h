#pragma once
// Copy this file to include/credentials.h (which git ignores) and fill it in to
// build Wi-Fi firmware updates (OTA) into both firmwares. Without
// credentials.h the project still builds; updates are then by USB cable only.
//
// These strings are compiled into the firmware image in plain text. Do not
// share a built firmware.bin.

#define WIFI_SSID "your-network-name"
#define WIFI_PASSWORD "your-network-password"

// Required to push firmware over Wi-Fi. Pick your own; it must not be empty.
// tools/pio_ota.py reads it from here when you upload to an IP address.
#define OTA_PASSWORD "choose-an-update-password"
