#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "activities/Activity.h"
#include "pagewire/PageWireTransport.h"
#include "crosspoint/PluginAbi.h"

class PluginsActivity final : public Activity {
 public:
  PluginsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool handleHomeGesture() override;

 private:
  enum class Screen : uint8_t { LIST, INSTALL };
  enum class InstallResult : uint8_t { IDLE, INSTALLING, COMPLETE, ERROR };
  enum class UpdateStatus : uint8_t { READY = 1, COMPLETE = 2, ERROR = 3 };

  pagewire::PageWireTransport& transport_;
  std::array<crosspoint_plugin::PluginInfoV3, crosspoint_plugin::MAX_LISTED_MODULES> modules_{};
  std::array<char, crosspoint_plugin::MODULE_NAME_BYTES> installModule_{};
  size_t moduleCount_ = 0;
  size_t selectedRow_ = 0;
  size_t firstVisibleRow_ = 0;
  uint32_t observedStatusRevision_ = 0;
  uint32_t expectedBytes_ = 0;
  uint32_t writtenBytes_ = 0;
  uint32_t pendingStatusValue_ = 0;
  uint32_t lastUpdateSequence_ = 0;
  uint32_t lastUpdatePacketHash_ = 0;
  UpdateStatus pendingStatus_ = UpdateStatus::ERROR;
  pagewire::PageWireTransport::Status previousTransportStatus_ =
      pagewire::PageWireTransport::Status::STOPPED;
  Screen screen_ = Screen::LIST;
  InstallResult installResult_ = InstallResult::IDLE;
  bool loadFailed_ = false;
  bool helloSent_ = false;
  bool hasPendingStatus_ = false;
  bool hasLastUpdatePacket_ = false;

  void refreshModules();
  void openSelected();
  void enterInstaller();
  void leaveInstaller();
  void serviceInstaller();
  void acceptInstallerPacket(const pagewire::PageWireTransport::IncomingPacket& packet);
  void failInstall(uint32_t errorCode);
  void queueStatus(UpdateStatus status, uint32_t value);
  void formatInstallStatus(char* buffer, size_t capacity) const;
};
