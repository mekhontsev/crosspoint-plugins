#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "crosspoint/PluginBle.h"
#include "pagewire/PageWireProtocol.h"

namespace pagewire {

class PageWireTransport final {
 public:
  using Status = crosspoint_plugin::PluginBle::Status;
  using IncomingPacket = crosspoint_plugin::PluginBle::IncomingPacket;

  bool start() { return ble_.start(); }
  void stop() { ble_.stop(); }
  bool poll(IncomingPacket& packet) { return ble_.poll(packet); }
  bool readyToSend() const { return ble_.readyToSend(); }
  void setTransferActive(const bool active) { ble_.setTransferActive(active); }
  Status status() const { return ble_.status(); }
  uint32_t statusRevision() const { return ble_.statusRevision(); }
  uint32_t droppedPackets() const { return ble_.droppedPackets(); }
  uint32_t pairingPasskey() const { return ble_.pairingPasskey(); }

  size_t maxCommandBytes() const {
    const size_t maximum = ble_.maxPacketBytes();
    return maximum > PACKET_HEADER_BYTES ? maximum - PACKET_HEADER_BYTES : 0;
  }

  bool sendAction(const Action action) {
    std::array<uint8_t, PACKET_HEADER_BYTES + 1> packet{};
    return sendEncoded(encodeActionPacket(action, sequence_, packet.data(), packet.size()), packet.data());
  }

  bool sendCommand(const char* command, const size_t length) {
    std::array<uint8_t, MAX_PACKET_BYTES> packet{};
    const size_t encoded = encodeCommandPacket(reinterpret_cast<const uint8_t*>(command), length, sequence_,
                                               packet.data(), packet.size());
    return length <= maxCommandBytes() && sendEncoded(encoded, packet.data());
  }

  bool sendRangeRequest(const RangeRequest request, const uint32_t generation, const uint32_t revision,
                        const uint32_t anchor, const uint16_t capacityBytes) {
    std::array<uint8_t, PACKET_HEADER_BYTES + 15> packet{};
    const size_t encoded = encodeRangeRequestPacket(request, generation, revision, anchor, capacityBytes, sequence_,
                                                    packet.data(), packet.size());
    return sendEncoded(encoded, packet.data());
  }

  bool sendDocumentStatus(const uint32_t generation, const uint32_t revision, const DocumentStatus status) {
    std::array<uint8_t, PACKET_HEADER_BYTES + 9> packet{};
    const size_t encoded =
        encodeDocumentStatusPacket(generation, revision, status, sequence_, packet.data(), packet.size());
    return sendEncoded(encoded, packet.data());
  }

  bool sendReaderHello(const uint16_t capacityBytes) {
    std::array<uint8_t, PACKET_HEADER_BYTES + 2> packet{};
    const size_t encoded = encodeReaderHelloPacket(capacityBytes, sequence_, packet.data(), packet.size());
    return sendEncoded(encoded, packet.data());
  }

  bool sendPluginUpdateHello(const uint32_t pluginAbi) {
    std::array<uint8_t, PACKET_HEADER_BYTES + 6> packet{};
    const size_t encoded =
        encodePluginUpdateHelloPacket(pluginAbi, MAX_UPDATE_DATA_BYTES, sequence_, packet.data(), packet.size());
    return sendEncoded(encoded, packet.data());
  }

  bool sendPluginUpdateStatus(const uint8_t status, const uint32_t value) {
    std::array<uint8_t, PACKET_HEADER_BYTES + 5> packet{};
    const size_t encoded =
        encodePluginUpdateStatusPacket(status, value, sequence_, packet.data(), packet.size());
    return sendEncoded(encoded, packet.data());
  }

 private:
  crosspoint_plugin::PluginBle ble_{};
  uint32_t sequence_ = 0;

  bool sendEncoded(const size_t length, const uint8_t* packet) {
    if (length == 0 || !ble_.send(packet, length)) return false;
    sequence_++;
    return true;
  }
};

extern PageWireTransport PAGEWIRE_TRANSPORT;

inline PageWireTransport& sharedPageWireTransport() { return PAGEWIRE_TRANSPORT; }

}  // namespace pagewire
