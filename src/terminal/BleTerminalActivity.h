#pragma once

#if defined(ENABLE_BLE_TERMINAL) && ENABLE_BLE_TERMINAL

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "activities/Activity.h"
#include "pagewire/PageWireProtocol.h"
#include "pagewire/PageWireTransport.h"

class BleTerminalActivity final : public Activity {
 public:
  BleTerminalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~BleTerminalActivity() override;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool handleHomeGesture() override;

 private:
  static constexpr uint32_t DATA_KEEP_AWAKE_MS = 5000;
  static constexpr uint32_t TRANSFER_IDLE_MS = 5000;
  static constexpr size_t DISPLAY_LINE_BYTES = 256;
  static constexpr size_t DOCUMENT_CAPACITY_BYTES = 12 * 1024;

  pagewire::PageWireTransport& transport_;
  std::array<char, DOCUMENT_CAPACITY_BYTES + 1> document_{};
  std::array<char, DOCUMENT_CAPACITY_BYTES + 1> stagingDocument_{};
  std::array<char, DISPLAY_LINE_BYTES> displayLine_{};
  pagewire::DocumentReceiver receiver_;
  StaticSemaphore_t documentMutexStorage_{};
  SemaphoreHandle_t documentMutex_ = nullptr;

  uint32_t generation_ = 0;
  uint32_t revision_ = 0;
  uint32_t windowStart_ = 0;
  uint32_t documentLength_ = 0;
  size_t documentBytes_ = 0;
  size_t viewStart_ = 0;
  size_t viewEnd_ = 0;

  uint32_t observedStatusRevision_ = 0;
  uint32_t pendingStatusGeneration_ = 0;
  uint32_t pendingStatusRevision_ = 0;
  uint32_t pendingRequestGeneration_ = 0;
  uint32_t pendingRequestRevision_ = 0;
  uint32_t pendingRequestAnchor_ = 0;
  uint32_t readyAfterRenderGeneration_ = 0;
  uint32_t readyAfterRenderRevision_ = 0;
  unsigned long lastPacketAt_ = 0;
  unsigned long lastTransferActivityAt_ = 0;
  unsigned long nextControlAttemptAt_ = 0;
  uint8_t fontSizeIndex_ = 3;
  pagewire::DocumentStatus pendingDocumentStatus_ = pagewire::DocumentStatus::READY;
  pagewire::RangeRequest pendingRangeRequest_ = pagewire::RangeRequest::CURRENT;
  int8_t pendingNavigation_ = 0;
  bool hasPendingStatus_ = false;
  bool hasPendingRequest_ = false;
  bool helloPending_ = false;
  bool initialDocumentRequested_ = false;
  bool needsReset_ = false;
  bool commandSendFailed_ = false;
  std::array<char, pagewire::MAX_COMMAND_BYTES + 1> pendingCommand_{};
  size_t pendingCommandLength_ = 0;
  bool pendingCommandReady_ = false;
  bool commandKeyboardRequested_ = false;
  bool commandKeyboardPending_ = false;
  bool followLatest_ = true;
  bool latestDocument_ = false;
  bool cleanRefreshPending_ = false;

  void formatStatusText(char* buffer, size_t bufferSize) const;
  void openCommandKeyboard();
  void takeCommandKeyboardResult();
  void serviceTransport(bool terminalVisible);
  int terminalFontId() const;
  void changeFontSize(int direction);
  void navigateDocument(int direction);
  void jumpToLatest();
  void queueRangeRequest(pagewire::RangeRequest request, uint32_t anchor, int8_t navigation = 0);
  void trySendPendingControl();
  bool commitDocument();
  void clearDocument();
  size_t pageStartEndingAt(size_t end);
  size_t pageEndStartingAt(size_t start);
  int bodyWidth() const;
  int visibleLineCount() const;
};

#endif
