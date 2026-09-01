#pragma once

namespace crosspoint_plugin_strings {

static constexpr char TERMINAL_TITLE[] = "Terminal";
static constexpr char TERMINAL_COMMAND[] = "Command";
static constexpr char BLE_STARTING[] = "Starting Bluetooth...";
static constexpr char BLE_WAITING[] = "Waiting for a Bluetooth LE client...";
static constexpr char BLE_SECURING[] = "Securing Bluetooth connection...";
static constexpr char BLE_PAIRING[] = "Pairing code: %06lu";
static constexpr char BLE_PAIRING_HINT[] = "Enter this code on the other device.";
static constexpr char BLE_CONNECTED[] = "Connected. Waiting for terminal text...";
static constexpr char BLE_ERROR[] = "Bluetooth could not start. Press Back and reopen this screen.";
static constexpr char BLE_RESYNC[] =
    "Some terminal data was rejected. Waiting "
    "for the client to resynchronize...";
static constexpr char COMMAND_SEND_FAILED[] = "Command was not sent. Check the Bluetooth connection and try again.";
static constexpr char INSTALL_BLUETOOTH[] = "Install via Bluetooth";
static constexpr char INSTALL_STARTING[] = "Starting Bluetooth...";
static constexpr char INSTALL_WAITING[] = "Waiting for a Bluetooth client...";
static constexpr char INSTALL_CONNECTED[] = "Connected. Waiting for a plugin package...";
static constexpr char INSTALLING[] = "Installing %s: %lu / %lu bytes";
static constexpr char INSTALL_COMPLETE[] = "Installed %s";
static constexpr char INSTALL_FAILED[] = "Plugin installation failed. Retry from the client.";

}  // namespace crosspoint_plugin_strings
