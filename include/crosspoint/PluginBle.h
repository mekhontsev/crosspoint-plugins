#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "crosspoint/PluginAbi.h"

namespace crosspoint_plugin {

class PluginBle final {
 public:
  using Status = PluginBleStatusV4;

  struct IncomingPacket {
    size_t length = 0;
    std::array<uint8_t, PLUGIN_BLE_MAX_PACKET_BYTES> bytes{};
  };

  bool start() const { return crosspoint_plugin_ble_start_v4() != 0; }
  void stop() const { crosspoint_plugin_ble_stop_v4(); }

  bool poll(IncomingPacket& packet) const {
    packet.length = 0;
    return crosspoint_plugin_ble_poll_v4(packet.bytes.data(), packet.bytes.size(), &packet.length) != 0;
  }

  bool send(const uint8_t* packet, const size_t length) const {
    return crosspoint_plugin_ble_send_v4(packet, length) != 0;
  }

  bool readyToSend() const { return crosspoint_plugin_ble_ready_v4() != 0; }
  size_t maxPacketBytes() const { return crosspoint_plugin_ble_max_packet_bytes_v4(); }
  void setTransferActive(const bool active) const {
    crosspoint_plugin_ble_set_transfer_active_v4(active ? 1 : 0);
  }
  Status status() const { return crosspoint_plugin_ble_status_v4(); }
  uint32_t statusRevision() const { return crosspoint_plugin_ble_status_revision_v4(); }
  uint32_t droppedPackets() const { return crosspoint_plugin_ble_dropped_packets_v4(); }
  uint32_t pairingPasskey() const { return crosspoint_plugin_ble_pairing_passkey_v4(); }
};

}  // namespace crosspoint_plugin
