#include "PluginsActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <crosspoint/PluginStrings.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr size_t UPDATE_BEGIN_FIXED_BYTES = 1 + sizeof(uint32_t) + crosspoint_plugin::SHA256_BYTES;
constexpr uint32_t ERROR_INVALID_PACKET = 1;
constexpr uint32_t ERROR_INSTALL_BEGIN = 2;
constexpr uint32_t ERROR_INSTALL_WRITE = 3;
constexpr uint32_t ERROR_INSTALL_FINISH = 4;

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) | (static_cast<uint32_t>(data[3]) << 24U);
}

bool decodePacket(const pagewire::PageWireTransport::IncomingPacket& packet, pagewire::PacketType& type,
                  uint32_t& sequence, const uint8_t*& payload, size_t& payloadLength) {
  if (packet.length < pagewire::PACKET_HEADER_BYTES || packet.length > packet.bytes.size() ||
      packet.bytes[0] != pagewire::MAGIC_0 || packet.bytes[1] != pagewire::MAGIC_1 ||
      packet.bytes[2] != pagewire::PROTOCOL_VERSION) {
    return false;
  }
  payloadLength = readLe16(packet.bytes.data() + 8);
  if (payloadLength != packet.length - pagewire::PACKET_HEADER_BYTES) return false;
  type = static_cast<pagewire::PacketType>(packet.bytes[3]);
  sequence = readLe32(packet.bytes.data() + 4);
  payload = packet.bytes.data() + pagewire::PACKET_HEADER_BYTES;
  return true;
}

uint32_t packetHash(const pagewire::PageWireTransport::IncomingPacket& packet) {
  uint32_t hash = 2166136261U;
  for (size_t index = 0; index < packet.length; ++index) {
    hash = (hash ^ packet.bytes[index]) * 16777619U;
  }
  return hash;
}

}  // namespace

PluginsActivity::PluginsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Plugins", renderer, mappedInput), transport_(pagewire::sharedPageWireTransport()) {}

void PluginsActivity::onEnter() {
  Activity::onEnter();
  if (screen_ == Screen::LIST) refreshModules();
  requestUpdate();
}

void PluginsActivity::onExit() {
  if (screen_ == Screen::INSTALL) leaveInstaller();
  Activity::onExit();
}

void PluginsActivity::refreshModules() {
  moduleCount_ = crosspoint_plugin_list_v3(modules_.data(), modules_.size());
  const auto end = std::remove_if(modules_.begin(), modules_.begin() + moduleCount_, [](const auto& module) {
    return (module.descriptor.flags & crosspoint_plugin::PLUGIN_FLAG_SERVICE) != 0;
  });
  moduleCount_ = static_cast<size_t>(end - modules_.begin());
  std::sort(modules_.begin(), modules_.begin() + moduleCount_, [](const auto& first, const auto& second) {
    if (first.descriptor.order != second.descriptor.order) return first.descriptor.order < second.descriptor.order;
    return std::strcmp(first.descriptor.title, second.descriptor.title) < 0;
  });
  selectedRow_ = std::min(selectedRow_, moduleCount_);
  firstVisibleRow_ = std::min(firstVisibleRow_, selectedRow_);
}

void PluginsActivity::openSelected() {
  if (selectedRow_ == moduleCount_) {
    enterInstaller();
    return;
  }
  Activity* child = crosspoint_plugin_create_child(modules_[selectedRow_].module, &renderer, &mappedInput);
  if (!child) {
    loadFailed_ = true;
    requestUpdate();
    return;
  }
  loadFailed_ = false;
  startActivityForResult(std::unique_ptr<Activity>(child), [this](const ActivityResult&) {
    refreshModules();
    requestUpdate();
  });
}

void PluginsActivity::enterInstaller() {
  screen_ = Screen::INSTALL;
  installResult_ = InstallResult::IDLE;
  installModule_[0] = '\0';
  expectedBytes_ = 0;
  writtenBytes_ = 0;
  helloSent_ = false;
  hasPendingStatus_ = false;
  hasLastUpdatePacket_ = false;
  crosspoint_plugin_install_abort_v3();
  transport_.start();
  transport_.setTransferActive(true);
  observedStatusRevision_ = transport_.statusRevision();
  previousTransportStatus_ = transport_.status();
  requestUpdate();
}

void PluginsActivity::leaveInstaller() {
  crosspoint_plugin_install_abort_v3();
  transport_.stop();
  screen_ = Screen::LIST;
  installResult_ = InstallResult::IDLE;
  helloSent_ = false;
  hasPendingStatus_ = false;
  refreshModules();
  requestUpdate();
}

void PluginsActivity::queueStatus(const UpdateStatus status, const uint32_t value) {
  pendingStatus_ = status;
  pendingStatusValue_ = value;
  hasPendingStatus_ = true;
}

void PluginsActivity::failInstall(const uint32_t errorCode) {
  crosspoint_plugin_install_abort_v3();
  installResult_ = InstallResult::ERROR;
  expectedBytes_ = 0;
  writtenBytes_ = 0;
  queueStatus(UpdateStatus::ERROR, errorCode);
  requestUpdate();
}

void PluginsActivity::acceptInstallerPacket(const pagewire::PageWireTransport::IncomingPacket& packet) {
  pagewire::PacketType type{};
  uint32_t sequence = 0;
  const uint8_t* payload = nullptr;
  size_t payloadLength = 0;
  if (!decodePacket(packet, type, sequence, payload, payloadLength)) {
    failInstall(ERROR_INVALID_PACKET);
    return;
  }
  const uint32_t hash = packetHash(packet);
  if (hasLastUpdatePacket_ && sequence == lastUpdateSequence_) {
    if (hash != lastUpdatePacketHash_) failInstall(ERROR_INVALID_PACKET);
    return;
  }
  lastUpdateSequence_ = sequence;
  lastUpdatePacketHash_ = hash;
  hasLastUpdatePacket_ = true;

  if (type == pagewire::PacketType::PLUGIN_UPDATE_BEGIN) {
    if (!payload || payloadLength < UPDATE_BEGIN_FIXED_BYTES) {
      failInstall(ERROR_INVALID_PACKET);
      return;
    }
    const size_t nameLength = payload[0];
    if (nameLength == 0 || nameLength >= installModule_.size() ||
        payloadLength != UPDATE_BEGIN_FIXED_BYTES + nameLength) {
      failInstall(ERROR_INVALID_PACKET);
      return;
    }
    std::memcpy(installModule_.data(), payload + 1, nameLength);
    installModule_[nameLength] = '\0';
    const uint8_t* size = payload + 1 + nameLength;
    expectedBytes_ = readLe32(size);
    writtenBytes_ = 0;
    if (!crosspoint_plugin_install_begin_v3(installModule_.data(), expectedBytes_, size + sizeof(uint32_t))) {
      failInstall(ERROR_INSTALL_BEGIN);
      return;
    }
    installResult_ = InstallResult::INSTALLING;
    requestUpdateAndWait();
    queueStatus(UpdateStatus::READY, 0);
    return;
  }

  if (type == pagewire::PacketType::PLUGIN_UPDATE_DATA) {
    if (installResult_ != InstallResult::INSTALLING || !payload || payloadLength <= sizeof(uint32_t)) {
      failInstall(ERROR_INVALID_PACKET);
      return;
    }
    const uint32_t offset = readLe32(payload);
    const size_t dataLength = payloadLength - sizeof(uint32_t);
    if (!crosspoint_plugin_install_write_v3(offset, payload + sizeof(uint32_t), dataLength)) {
      failInstall(ERROR_INSTALL_WRITE);
      return;
    }
    writtenBytes_ = offset + dataLength;
    return;
  }

  if (type == pagewire::PacketType::PLUGIN_UPDATE_END) {
    if (installResult_ != InstallResult::INSTALLING || !payload || payloadLength != sizeof(uint32_t) ||
        readLe32(payload) != expectedBytes_ || writtenBytes_ != expectedBytes_) {
      failInstall(ERROR_INVALID_PACKET);
      return;
    }
    if (!crosspoint_plugin_install_finish_v3()) {
      failInstall(ERROR_INSTALL_FINISH);
      return;
    }
    installResult_ = InstallResult::COMPLETE;
    requestUpdateAndWait();
    queueStatus(UpdateStatus::COMPLETE, writtenBytes_);
  }
}

void PluginsActivity::serviceInstaller() {
  const auto status = transport_.status();
  if (status != previousTransportStatus_) {
    if (previousTransportStatus_ == pagewire::PageWireTransport::Status::CONNECTED &&
        status != pagewire::PageWireTransport::Status::CONNECTED) {
      crosspoint_plugin_install_abort_v3();
      installResult_ = InstallResult::IDLE;
      expectedBytes_ = 0;
      writtenBytes_ = 0;
      helloSent_ = false;
      hasPendingStatus_ = false;
      hasLastUpdatePacket_ = false;
    }
    previousTransportStatus_ = status;
    requestUpdate();
  }

  pagewire::PageWireTransport::IncomingPacket packet{};
  if (status != pagewire::PageWireTransport::Status::CONNECTED) {
    while (transport_.poll(packet)) {
    }
    const uint32_t revision = transport_.statusRevision();
    if (revision != observedStatusRevision_) {
      observedStatusRevision_ = revision;
      requestUpdate();
    }
    return;
  }
  while (transport_.poll(packet)) acceptInstallerPacket(packet);

  if (transport_.readyToSend()) {
    if (hasPendingStatus_) {
      if (transport_.sendPluginUpdateStatus(static_cast<uint8_t>(pendingStatus_), pendingStatusValue_)) {
        hasPendingStatus_ = false;
      }
    } else if (!helloSent_ && transport_.sendPluginUpdateHello(crosspoint_plugin::ABI_VERSION)) {
      helloSent_ = true;
    }
  }

  const uint32_t revision = transport_.statusRevision();
  if (revision != observedStatusRevision_) {
    observedStatusRevision_ = revision;
    requestUpdate();
  }
}

void PluginsActivity::loop() {
  if (screen_ == Screen::INSTALL) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      leaveInstaller();
      return;
    }
    serviceInstaller();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::PLUGINS);
    return;
  }
  const size_t rowCount = moduleCount_ + 1;
  if (mappedInput.wasReleased(MappedInputManager::Button::Up) && selectedRow_ > 0) {
    selectedRow_--;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down) && selectedRow_ + 1 < rowCount) {
    selectedRow_++;
    requestUpdate();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openSelected();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyHeight = renderer.getScreenHeight() - rowTop - metrics.buttonHintsHeight;
  const size_t visibleRows = static_cast<size_t>(std::max(1, bodyHeight / metrics.listRowHeight));
  if (selectedRow_ < firstVisibleRow_) firstVisibleRow_ = selectedRow_;
  if (selectedRow_ >= firstVisibleRow_ + visibleRows) firstVisibleRow_ = selectedRow_ - visibleRows + 1;
  for (size_t visible = 0; visible < visibleRows && firstVisibleRow_ + visible < rowCount; ++visible) {
    const int y = rowTop + static_cast<int>(visible) * metrics.listRowHeight;
    if (mappedInput.wasTapInRect(metrics.contentSidePadding, y,
                                 renderer.getScreenWidth() - 2 * metrics.contentSidePadding, metrics.listRowHeight)) {
      selectedRow_ = firstVisibleRow_ + visible;
      openSelected();
      return;
    }
  }
}

bool PluginsActivity::preventAutoSleep() { return screen_ == Screen::INSTALL; }

bool PluginsActivity::handleHomeGesture() {
  if (screen_ == Screen::INSTALL) {
    leaveInstaller();
  } else {
    onGoHome(HomeMenuItem::PLUGINS);
  }
  return true;
}

void PluginsActivity::formatInstallStatus(char* buffer, const size_t capacity) const {
  const auto status = transport_.status();
  if (status == pagewire::PageWireTransport::Status::ERROR) {
    std::snprintf(buffer, capacity, "%s", crosspoint_plugin_strings::BLE_ERROR);
  } else if (status == pagewire::PageWireTransport::Status::STARTING) {
    std::snprintf(buffer, capacity, "%s", crosspoint_plugin_strings::INSTALL_STARTING);
  } else if (status == pagewire::PageWireTransport::Status::ADVERTISING) {
    std::snprintf(buffer, capacity, "%s", crosspoint_plugin_strings::INSTALL_WAITING);
  } else if (status == pagewire::PageWireTransport::Status::PAIRING) {
    const uint32_t passkey = transport_.pairingPasskey();
    if (passkey != 0) {
      std::snprintf(buffer, capacity, crosspoint_plugin_strings::BLE_PAIRING, static_cast<unsigned long>(passkey));
    } else {
      std::snprintf(buffer, capacity, "%s", crosspoint_plugin_strings::BLE_SECURING);
    }
  } else if (installResult_ == InstallResult::ERROR) {
    std::snprintf(buffer, capacity, "%s", crosspoint_plugin_strings::INSTALL_FAILED);
  } else if (installResult_ == InstallResult::COMPLETE) {
    std::snprintf(buffer, capacity, crosspoint_plugin_strings::INSTALL_COMPLETE, installModule_.data());
  } else if (installResult_ == InstallResult::INSTALLING) {
    std::snprintf(buffer, capacity, crosspoint_plugin_strings::INSTALLING, installModule_.data(),
                  static_cast<unsigned long>(writtenBytes_), static_cast<unsigned long>(expectedBytes_));
  } else if (status == pagewire::PageWireTransport::Status::CONNECTED) {
    std::snprintf(buffer, capacity, "%s", crosspoint_plugin_strings::INSTALL_CONNECTED);
  } else {
    std::snprintf(buffer, capacity, "%s", crosspoint_plugin_strings::INSTALL_STARTING);
  }
}

void PluginsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const int bodyTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyHeight = pageHeight - bodyTop - metrics.buttonHintsHeight;
  const int bodyWidth = pageWidth - 2 * metrics.contentSidePadding;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PLUGINS));

  if (screen_ == Screen::INSTALL) {
    std::array<char, 192> status{};
    formatInstallStatus(status.data(), status.size());
    UITheme::drawCenteredWrappedText(renderer, Rect{metrics.contentSidePadding, bodyTop, bodyWidth, bodyHeight},
                                     UI_10_FONT_ID, status.data(), 5);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const size_t rowCount = moduleCount_ + 1;
  const size_t visibleRows = static_cast<size_t>(std::max(1, bodyHeight / metrics.listRowHeight));
  for (size_t visible = 0; visible < visibleRows && firstVisibleRow_ + visible < rowCount; ++visible) {
    const size_t row = firstVisibleRow_ + visible;
    const int y = bodyTop + static_cast<int>(visible) * metrics.listRowHeight;
    if (row == selectedRow_) {
      renderer.drawRect(metrics.contentSidePadding, y, bodyWidth, metrics.listRowHeight, true);
    }
    const char* title =
        row < moduleCount_ ? modules_[row].descriptor.title : crosspoint_plugin_strings::INSTALL_BLUETOOTH;
    const int textY = y + (metrics.listRowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding + metrics.listSidePadding, textY, title);
    if (row < moduleCount_ && modules_[row].descriptor.version[0] != '\0') {
      const int versionWidth = renderer.getTextWidth(UI_10_FONT_ID, modules_[row].descriptor.version);
      renderer.drawText(UI_10_FONT_ID, pageWidth - metrics.contentSidePadding - metrics.listSidePadding - versionWidth,
                        y + (metrics.listRowHeight - renderer.getLineHeight(UI_10_FONT_ID)) / 2,
                        modules_[row].descriptor.version);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (loadFailed_) GUI.drawPopup(renderer, tr(STR_PLUGIN_MODULE_LOAD_FAILED));
  renderer.displayBuffer();
}
