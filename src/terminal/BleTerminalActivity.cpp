#include "BleTerminalActivity.h"

#if defined(ENABLE_BLE_TERMINAL) && ENABLE_BLE_TERMINAL

#include <Arduino.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <crosspoint/PluginAbi.h>
#include <crosspoint/PluginStrings.h>
#include <esp_memory_utils.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MappedInputManager.h"
#include "TerminalFontIds.h"
#include "components/HeaderKeyboardButton.h"
#include "components/UITheme.h"

namespace {

constexpr size_t HEADER_CONTROL_COUNT = 4;
constexpr int HEADER_TITLE_GAP = 20;
constexpr unsigned long CONTROL_RETRY_MS = 100;
constexpr unsigned long CONNECTION_WAKE_MS = 250;
constexpr unsigned long LONG_PRESS_MS = 700;

constexpr std::array<int, 9> TERMINAL_FONT_IDS = {
    TERMINAL_MONO_8_FONT_ID,  TERMINAL_MONO_10_FONT_ID, TERMINAL_MONO_12_FONT_ID,
    TERMINAL_MONO_14_FONT_ID, TERMINAL_MONO_16_FONT_ID, TERMINAL_MONO_18_FONT_ID,
    TERMINAL_MONO_20_FONT_ID, TERMINAL_MONO_22_FONT_ID, TERMINAL_MONO_24_FONT_ID,
};

struct VisualLine {
  size_t nextStart = 0;
};

bool isUtf8Continuation(const char value) { return (static_cast<uint8_t>(value) & 0xC0U) == 0x80U; }

bool pointInRect(const int x, const int y, const Rect& rect) {
  return x >= rect.x && x < rect.x + rect.width && y >= rect.y && y < rect.y + rect.height;
}

void drawRefreshButton(const GfxRenderer& renderer, const Rect& button, const bool history) {
  constexpr int glyphWidth = 24;
  constexpr int halfGap = 3;
  constexpr int arrowSize = 5;
  const int left = button.x + (button.width - glyphWidth) / 2;
  const int right = left + glyphWidth;
  const int middle = button.y + button.height / 2;

  renderer.drawRect(button.x, button.y, button.width, button.height);
  renderer.drawLine(left, middle - halfGap, right, middle - halfGap);
  renderer.drawLine(right - arrowSize, middle - halfGap - arrowSize, right, middle - halfGap);
  renderer.drawLine(left, middle + halfGap, right, middle + halfGap);
  renderer.drawLine(left, middle + halfGap, left + arrowSize, middle + halfGap + arrowSize);
  if (history) renderer.fillRect(button.x + button.width - 7, button.y + 4, 4, 4);
}

VisualLine prepareVisualLine(const GfxRenderer& renderer, const int fontId, const char* source,
                             const size_t textLength, const size_t start, const int maxWidth,
                             std::array<char, 256>& line) {
  size_t logicalEnd = start;
  while (logicalEnd < textLength && source[logicalEnd] != '\n') ++logicalEnd;

  const size_t available = logicalEnd - start;
  size_t length = std::min(available, line.size() - 1);
  while (length > 0 && start + length < logicalEnd && isUtf8Continuation(source[start + length])) --length;

  std::memcpy(line.data(), source + start, length);
  for (size_t index = 0; index < length; ++index) {
    if (line[index] == '\t') line[index] = ' ';
  }
  line[length] = '\0';

  while (length > 0 && renderer.getTextWidth(fontId, line.data()) > maxWidth) {
    size_t nextLength = length - 1;
    while (nextLength > 0 && isUtf8Continuation(line[nextLength])) --nextLength;
    length = nextLength;
    line[length] = '\0';
  }

  if (length == 0 && available > 0) {
    length = 1;
    while (start + length < logicalEnd && isUtf8Continuation(source[start + length])) ++length;
    std::memcpy(line.data(), source + start, length);
    if (line[0] == '\t') line[0] = ' ';
    line[length] = '\0';
  }

  VisualLine result;
  if (length < available) {
    result.nextStart = start + length;
  } else if (logicalEnd < textLength) {
    result.nextStart = logicalEnd + 1;
  } else {
    result.nextStart = textLength + 1;
  }
  return result;
}

size_t countVisualLines(const GfxRenderer& renderer, const int fontId, const char* text, const size_t textLength,
                        const int maxWidth, std::array<char, 256>& line) {
  size_t count = 0;
  size_t cursor = 0;
  while (cursor < textLength) {
    cursor = prepareVisualLine(renderer, fontId, text, textLength, cursor, maxWidth, line).nextStart;
    ++count;
  }
  return count;
}

size_t visualLineStart(const GfxRenderer& renderer, const int fontId, const char* text, const size_t textLength,
                       const int maxWidth, const size_t targetIndex,
                       std::array<char, 256>& line) {
  size_t index = 0;
  size_t cursor = 0;
  while (cursor < textLength && index < targetIndex) {
    cursor = prepareVisualLine(renderer, fontId, text, textLength, cursor, maxWidth, line).nextStart;
    ++index;
  }
  return std::min(cursor, textLength);
}

}  // namespace

#if defined(CROSSPOINT_PLUGIN_BUILD) && CROSSPOINT_PLUGIN_BUILD
void releaseTerminalPluginFonts(GfxRenderer& renderer);
#endif

BleTerminalActivity::BleTerminalActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("BleTerminal", renderer, mappedInput),
      transport_(pagewire::sharedPageWireTransport()),
      receiver_(stagingDocument_.data(), DOCUMENT_CAPACITY_BYTES),
      documentMutex_(xSemaphoreCreateMutexStatic(&documentMutexStorage_)) {}

BleTerminalActivity::~BleTerminalActivity() {
#if defined(CROSSPOINT_PLUGIN_BUILD) && CROSSPOINT_PLUGIN_BUILD
  releaseTerminalPluginFonts(renderer);
#endif
}

void BleTerminalActivity::onEnter() {
  Activity::onEnter();
  transport_.start();
  observedStatusRevision_ = transport_.statusRevision();
  pendingStatusGeneration_ = 0;
  pendingStatusRevision_ = 0;
  pendingRequestGeneration_ = 0;
  pendingRequestRevision_ = 0;
  pendingRequestAnchor_ = 0;
  readyAfterRenderGeneration_ = 0;
  readyAfterRenderRevision_ = 0;
  lastPacketAt_ = 0;
  lastTransferActivityAt_ = 0;
  nextControlAttemptAt_ = 0;
  hasPendingStatus_ = false;
  hasPendingRequest_ = false;
  helloPending_ = false;
  initialDocumentRequested_ = false;
  needsReset_ = false;
  commandSendFailed_ = false;
  pendingCommandLength_ = 0;
  pendingCommand_[0] = '\0';
  pendingCommandReady_ = false;
  commandKeyboardRequested_ = false;
  commandKeyboardPending_ = false;
  pendingNavigation_ = 0;
  followLatest_ = true;
  cleanRefreshPending_ = false;
  receiver_.clear();
  clearDocument();
  LOG_INF("PAGEWIRE", "Document buffers: 2 x %u bytes in %s; free heap=%u, PSRAM=%u",
          static_cast<unsigned>(DOCUMENT_CAPACITY_BYTES),
          esp_ptr_external_ram(document_.data()) ? "PSRAM" : "internal RAM", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getFreePsram()));
  requestUpdate();
}

void BleTerminalActivity::onExit() {
  LOG_INF("PAGEWIRE", "Terminal exit: stopping plugin BLE");
  transport_.stop();
  if (documentMutex_ && xSemaphoreTake(documentMutex_, portMAX_DELAY) == pdTRUE) {
    receiver_.clear();
    clearDocument();
    xSemaphoreGive(documentMutex_);
  }
  Activity::onExit();
}

void BleTerminalActivity::clearDocument() {
  generation_ = 0;
  revision_ = 0;
  windowStart_ = 0;
  documentLength_ = 0;
  documentBytes_ = 0;
  viewStart_ = 0;
  viewEnd_ = 0;
  document_[0] = '\0';
  receiver_.setBase(document_.data(), 0, 0, 0);
  followLatest_ = true;
  latestDocument_ = false;
}

void BleTerminalActivity::openCommandKeyboard() {
  if (commandKeyboardPending_) return;
  const size_t maxLength = transport_.maxCommandBytes();
  if (maxLength == 0) return;

  commandSendFailed_ = false;
  pendingCommandLength_ = 0;
  pendingCommand_[0] = '\0';
  const uint32_t flags =
      crosspoint_plugin::KEYBOARD_FLAG_HEADER_TOGGLE | crosspoint_plugin::KEYBOARD_FLAG_SYSTEM_LANGUAGE;
  commandKeyboardPending_ = crosspoint_plugin_open_text_keyboard_v2(crosspoint_plugin_strings::TERMINAL_COMMAND,
                                                                    std::min(maxLength, pendingCommand_.size() - 1),
                                                                    flags, &renderer, &mappedInput);
  if (!commandKeyboardPending_) {
    LOG_ERR("PAGEWIRE", "Firmware keyboard could not be opened");
    commandSendFailed_ = true;
    requestUpdate();
  }
}

void BleTerminalActivity::takeCommandKeyboardResult() {
  size_t length = 0;
  uint8_t cancelled = false;
  if (!crosspoint_plugin_take_text_keyboard_result_v2(pendingCommand_.data(), pendingCommand_.size(), &length,
                                                      &cancelled)) {
    return;
  }
  commandKeyboardPending_ = false;
  initialDocumentRequested_ = false;
  nextControlAttemptAt_ = 0;
  if (cancelled) {
    pendingCommandLength_ = 0;
    pendingCommand_[0] = '\0';
    requestUpdate();
    return;
  }
  pendingCommandLength_ = std::min(length, pendingCommand_.size() - 1);
  pendingCommand_[pendingCommandLength_] = '\0';
  pendingCommandReady_ = true;
}

int BleTerminalActivity::terminalFontId() const { return TERMINAL_FONT_IDS[fontSizeIndex_]; }

int BleTerminalActivity::bodyWidth() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  return renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
}

int BleTerminalActivity::visibleLineCount() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - metrics.buttonHintsHeight;
  const int lineHeight = renderer.getLineHeight(terminalFontId());
  return lineHeight > 0 ? std::max(0, height / lineHeight) : 0;
}

size_t BleTerminalActivity::pageStartEndingAt(const size_t end) {
  const size_t boundedEnd = std::min(end, documentBytes_);
  const size_t lines = countVisualLines(renderer, terminalFontId(), document_.data(), boundedEnd, bodyWidth(),
                                        displayLine_);
  const size_t visible = static_cast<size_t>(visibleLineCount());
  return visualLineStart(renderer, terminalFontId(), document_.data(), boundedEnd, bodyWidth(),
                         lines > visible ? lines - visible : 0, displayLine_);
}

size_t BleTerminalActivity::pageEndStartingAt(const size_t start) {
  size_t cursor = std::min(start, documentBytes_);
  const int lines = visibleLineCount();
  for (int line = 0; line < lines && cursor < documentBytes_; ++line) {
    cursor = prepareVisualLine(renderer, terminalFontId(), document_.data(), documentBytes_, cursor, bodyWidth(),
                               displayLine_)
                 .nextStart;
  }
  return std::min(cursor, documentBytes_);
}

void BleTerminalActivity::changeFontSize(const int direction) {
  const int nextIndex =
      std::clamp(static_cast<int>(fontSizeIndex_) + direction, 0, static_cast<int>(TERMINAL_FONT_IDS.size()) - 1);
  if (nextIndex == fontSizeIndex_) return;
  fontSizeIndex_ = static_cast<uint8_t>(nextIndex);
  if (documentMutex_ && xSemaphoreTake(documentMutex_, portMAX_DELAY) == pdTRUE) {
    if (followLatest_) {
      viewEnd_ = documentBytes_;
      viewStart_ = pageStartEndingAt(viewEnd_);
    } else {
      viewStart_ = std::min(viewStart_, documentBytes_);
      viewEnd_ = pageEndStartingAt(viewStart_);
    }
    xSemaphoreGive(documentMutex_);
  }
  requestUpdate();
}

void BleTerminalActivity::queueRangeRequest(const pagewire::RangeRequest request, const uint32_t anchor,
                                            const int8_t navigation) {
  const unsigned long now = millis();
  const bool connectionWasIdle =
      lastTransferActivityAt_ == 0 && transport_.status() == pagewire::PageWireTransport::Status::CONNECTED;
  lastTransferActivityAt_ = now;
  transport_.setTransferActive(true);
  if (connectionWasIdle) nextControlAttemptAt_ = now + CONNECTION_WAKE_MS;
  pendingRangeRequest_ = request;
  pendingRequestGeneration_ = generation_;
  pendingRequestRevision_ = revision_;
  pendingRequestAnchor_ = anchor;
  pendingNavigation_ = navigation;
  hasPendingRequest_ = true;
  trySendPendingControl();
}

void BleTerminalActivity::trySendPendingControl() {
  const unsigned long now = millis();
  if (now < nextControlAttemptAt_ || !transport_.readyToSend()) return;

  bool sent = false;
  if (hasPendingStatus_) {
    sent =
        transport_.sendDocumentStatus(pendingStatusGeneration_, pendingStatusRevision_, pendingDocumentStatus_);
    if (sent) hasPendingStatus_ = false;
  } else if (helloPending_) {
    sent = transport_.sendReaderHello(static_cast<uint16_t>(DOCUMENT_CAPACITY_BYTES));
    if (sent) helloPending_ = false;
  } else if (hasPendingRequest_) {
    sent = transport_.sendRangeRequest(pendingRangeRequest_, pendingRequestGeneration_, pendingRequestRevision_,
                                       pendingRequestAnchor_, static_cast<uint16_t>(DOCUMENT_CAPACITY_BYTES));
    if (sent) hasPendingRequest_ = false;
  }
  nextControlAttemptAt_ = sent ? now : now + CONTROL_RETRY_MS;
}

bool BleTerminalActivity::commitDocument() {
  const bool hadDocument = generation_ != 0;
  const uint32_t oldAnchor = windowStart_ + static_cast<uint32_t>(viewStart_);
  generation_ = receiver_.generation();
  revision_ = receiver_.revision();
  windowStart_ = receiver_.windowStart();
  documentLength_ = receiver_.documentLength();
  documentBytes_ = receiver_.length();
  std::memcpy(document_.data(), receiver_.text(), documentBytes_ + 1);
  receiver_.setBase(document_.data(), documentBytes_, generation_, revision_);

  const bool latest = (receiver_.flags() & pagewire::DOCUMENT_FLAG_LATEST) != 0;
  const bool present = (receiver_.flags() & pagewire::DOCUMENT_FLAG_PRESENT) != 0;
  const bool shouldPresent = !hadDocument || present || (latest && followLatest_);
  if (!shouldPresent) return false;

  if (pendingNavigation_ < 0) {
    const uint32_t requestedAnchor = pendingRequestAnchor_ == 0 ? documentLength_ : pendingRequestAnchor_;
    const size_t end = static_cast<size_t>(
        std::clamp<uint32_t>(requestedAnchor, windowStart_, windowStart_ + static_cast<uint32_t>(documentBytes_)) -
        windowStart_);
    viewEnd_ = end;
    viewStart_ = pageStartEndingAt(viewEnd_);
  } else if (pendingNavigation_ > 0) {
    const size_t start = static_cast<size_t>(
        std::clamp<uint32_t>(pendingRequestAnchor_, windowStart_,
                             windowStart_ + static_cast<uint32_t>(documentBytes_)) -
        windowStart_);
    viewStart_ = start;
    viewEnd_ = pageEndStartingAt(viewStart_);
  } else if (latest || !hadDocument) {
    viewEnd_ = documentBytes_;
    viewStart_ = pageStartEndingAt(viewEnd_);
  } else {
    const size_t start =
        oldAnchor <= windowStart_ ? 0 : std::min<size_t>(oldAnchor - windowStart_, documentBytes_);
    viewStart_ = start;
    viewEnd_ = pageEndStartingAt(viewStart_);
  }
  pendingNavigation_ = 0;
  latestDocument_ = latest;
  followLatest_ = latest && windowStart_ + viewEnd_ >= documentLength_;
  needsReset_ = false;
  return true;
}

void BleTerminalActivity::navigateDocument(const int direction) {
  bool changed = false;
  bool requestRemote = false;
  uint32_t anchor = 0;
  pagewire::RangeRequest request = pagewire::RangeRequest::CURRENT;

  if (documentMutex_ && xSemaphoreTake(documentMutex_, portMAX_DELAY) == pdTRUE) {
    if (generation_ == 0) {
      requestRemote = true;
    } else if (direction < 0) {
      anchor = windowStart_ + static_cast<uint32_t>(viewStart_);
      const size_t previousStart = pageStartEndingAt(viewStart_);
      if (viewStart_ == 0 && (windowStart_ > 0 || latestDocument_)) {
        requestRemote = true;
        request = pagewire::RangeRequest::BEFORE;
      } else if (viewStart_ != 0) {
        viewEnd_ = viewStart_;
        viewStart_ = previousStart;
        followLatest_ = false;
        changed = true;
      }
    } else {
      anchor = windowStart_ + static_cast<uint32_t>(viewEnd_);
      if (viewEnd_ < documentBytes_) {
        viewStart_ = viewEnd_;
        viewEnd_ = pageEndStartingAt(viewStart_);
        followLatest_ = windowStart_ + viewEnd_ >= documentLength_;
        changed = true;
      } else if (windowStart_ + documentBytes_ < documentLength_) {
        requestRemote = true;
        request = pagewire::RangeRequest::AFTER;
      } else {
        requestRemote = true;
        request = pagewire::RangeRequest::CURRENT;
      }
    }
    xSemaphoreGive(documentMutex_);
  }

  if (changed) requestUpdate();
  if (requestRemote) queueRangeRequest(request, anchor, request == pagewire::RangeRequest::CURRENT ? 0 : direction);
}

void BleTerminalActivity::jumpToLatest() {
  queueRangeRequest(pagewire::RangeRequest::CURRENT, windowStart_ + static_cast<uint32_t>(viewEnd_));
}

void BleTerminalActivity::loop() {
  if (commandKeyboardPending_) takeCommandKeyboardResult();

  if (commandKeyboardRequested_) {
    serviceTransport(false);
    commandKeyboardRequested_ = false;
    openCommandKeyboard();
    return;
  }

  if (pendingCommandReady_) {
    serviceTransport(false);
    if (!transport_.readyToSend()) return;
    const bool sent = pendingCommandLength_ == 0
                          ? transport_.sendAction(pagewire::Action::SUBMIT_INPUT)
                          : transport_.sendCommand(pendingCommand_.data(), pendingCommandLength_);
    pendingCommandLength_ = 0;
    pendingCommand_[0] = '\0';
    pendingCommandReady_ = false;
    commandSendFailed_ = !sent;
    if (sent) queueRangeRequest(pagewire::RangeRequest::CURRENT, 0);
    requestUpdate();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    transport_.stop();
    onGoHome(HomeMenuItem::PLUGINS);
    return;
  }

  {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const char* title = crosspoint_plugin_strings::TERMINAL_TITLE;
    const Rect keyboardButton =
        header_keyboard_button::layout(renderer, metrics, title, 0, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP);
    if (mappedInput.wasTapInRect(keyboardButton.x, keyboardButton.y, keyboardButton.width, keyboardButton.height)) {
      commandKeyboardRequested_ = true;
      return;
    }
    const Rect decreaseButton =
        header_keyboard_button::layout(renderer, metrics, title, 1, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP);
    if (mappedInput.wasTapInRect(decreaseButton.x, decreaseButton.y, decreaseButton.width, decreaseButton.height)) {
      changeFontSize(-1);
      return;
    }
    const Rect increaseButton =
        header_keyboard_button::layout(renderer, metrics, title, 2, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP);
    if (mappedInput.wasTapInRect(increaseButton.x, increaseButton.y, increaseButton.width, increaseButton.height)) {
      changeFontSize(1);
      return;
    }
    const Rect refreshButton =
        header_keyboard_button::layout(renderer, metrics, title, 3, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP);
    int touchX = 0;
    int touchY = 0;
    if (mappedInput.wasScreenLongPress(touchX, touchY)) {
      if (pointInRect(touchX, touchY, refreshButton)) {
        cleanRefreshPending_ = true;
        requestUpdate();
        queueRangeRequest(pagewire::RangeRequest::CURRENT, windowStart_ + static_cast<uint32_t>(viewEnd_));
      }
      return;
    }
    if (mappedInput.wasTapInRect(refreshButton.x, refreshButton.y, refreshButton.width, refreshButton.height)) {
      queueRangeRequest(pagewire::RangeRequest::CURRENT, windowStart_ + static_cast<uint32_t>(viewEnd_));
      return;
    }
  }

  if (mappedInput.wasLongPressed(MappedInputManager::Button::Confirm, LONG_PRESS_MS)) {
    cleanRefreshPending_ = true;
    requestUpdate();
    queueRangeRequest(pagewire::RangeRequest::CURRENT, windowStart_ + static_cast<uint32_t>(viewEnd_));
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    queueRangeRequest(pagewire::RangeRequest::CURRENT, windowStart_ + static_cast<uint32_t>(viewEnd_));
  }

  if (mappedInput.wasLongPressed(MappedInputManager::Button::Down, LONG_PRESS_MS)) {
    jumpToLatest();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    navigateDocument(-1);
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    navigateDocument(1);
  }

  serviceTransport(true);
}

void BleTerminalActivity::serviceTransport(const bool terminalVisible) {
  const unsigned long now = millis();
  if (!terminalVisible && readyAfterRenderGeneration_ != 0) {
    pendingStatusGeneration_ = readyAfterRenderGeneration_;
    pendingStatusRevision_ = readyAfterRenderRevision_;
    pendingDocumentStatus_ = pagewire::DocumentStatus::READY;
    hasPendingStatus_ = true;
    readyAfterRenderGeneration_ = 0;
    readyAfterRenderRevision_ = 0;
  }

  pagewire::PageWireTransport::IncomingPacket packet{};
  while (transport_.poll(packet)) {
    if (!documentMutex_ || xSemaphoreTake(documentMutex_, portMAX_DELAY) != pdTRUE) continue;
    const pagewire::AcceptResult result = receiver_.accept(packet.bytes.data(), packet.length);
    const uint32_t incomingGeneration = receiver_.generation();
    const uint32_t incomingRevision = receiver_.revision();
    const bool validPacket = result == pagewire::AcceptResult::DOCUMENT_STARTED ||
                             result == pagewire::AcceptResult::DOCUMENT_DATA_ACCEPTED ||
                             result == pagewire::AcceptResult::DOCUMENT_COPY_ACCEPTED ||
                             result == pagewire::AcceptResult::DOCUMENT_COMMITTED ||
                             result == pagewire::AcceptResult::DUPLICATE_IGNORED;
    const bool baseMismatch = result == pagewire::AcceptResult::BASE_MISMATCH;
    const bool documentError =
        result == pagewire::AcceptResult::NEEDS_BEGIN || result == pagewire::AcceptResult::OUT_OF_ORDER ||
        result == pagewire::AcceptResult::TOO_LARGE || result == pagewire::AcceptResult::INVALID_TEXT ||
        result == pagewire::AcceptResult::INVALID_DOCUMENT || result == pagewire::AcceptResult::CRC_MISMATCH ||
        result == pagewire::AcceptResult::UNSUPPORTED_VERSION;
    bool present = false;
    if (result == pagewire::AcceptResult::DOCUMENT_COMMITTED) present = commitDocument();
    if (documentError) needsReset_ = true;
    xSemaphoreGive(documentMutex_);

    if (validPacket || baseMismatch || documentError) {
      lastPacketAt_ = now;
      lastTransferActivityAt_ = now;
      transport_.setTransferActive(true);
    }
    if (result == pagewire::AcceptResult::DOCUMENT_COMMITTED) {
      if (present && terminalVisible) {
        readyAfterRenderGeneration_ = incomingGeneration;
        readyAfterRenderRevision_ = incomingRevision;
        requestUpdate();
      } else {
        pendingStatusGeneration_ = incomingGeneration;
        pendingStatusRevision_ = incomingRevision;
        pendingDocumentStatus_ = pagewire::DocumentStatus::READY;
        hasPendingStatus_ = true;
      }
    } else if ((baseMismatch || documentError) && incomingGeneration != 0 && incomingRevision != 0) {
      pendingStatusGeneration_ = incomingGeneration;
      pendingStatusRevision_ = incomingRevision;
      pendingDocumentStatus_ =
          baseMismatch ? pagewire::DocumentStatus::NEED_FULL : pagewire::DocumentStatus::RETRY;
      hasPendingStatus_ = true;
      if (terminalVisible) requestUpdate();
    }
  }

  const uint32_t statusRevision = transport_.statusRevision();
  if (statusRevision != observedStatusRevision_) {
    observedStatusRevision_ = statusRevision;
    const auto status = transport_.status();
    if (status == pagewire::PageWireTransport::Status::CONNECTED) {
      lastTransferActivityAt_ = now;
      transport_.setTransferActive(true);
      helloPending_ = true;
      initialDocumentRequested_ = false;
    } else {
      initialDocumentRequested_ = false;
      helloPending_ = false;
    }
    if (terminalVisible &&
        (generation_ == 0 || status == pagewire::PageWireTransport::Status::PAIRING ||
         status == pagewire::PageWireTransport::Status::ERROR)) {
      requestUpdate();
    }
  }

  if (!pendingCommandReady_ && !initialDocumentRequested_ &&
      transport_.status() == pagewire::PageWireTransport::Status::CONNECTED) {
    initialDocumentRequested_ = true;
    helloPending_ = true;
    queueRangeRequest(pagewire::RangeRequest::CURRENT, windowStart_ + static_cast<uint32_t>(viewEnd_));
  }

  if (lastTransferActivityAt_ != 0 && now - lastTransferActivityAt_ >= TRANSFER_IDLE_MS) {
    lastTransferActivityAt_ = 0;
    transport_.setTransferActive(false);
  }
  trySendPendingControl();
}

bool BleTerminalActivity::preventAutoSleep() {
  return commandKeyboardRequested_ || commandKeyboardPending_ || pendingCommandReady_ ||
         (lastPacketAt_ != 0 && millis() - lastPacketAt_ < DATA_KEEP_AWAKE_MS);
}

bool BleTerminalActivity::handleHomeGesture() {
  transport_.stop();
  onGoHome(HomeMenuItem::PLUGINS);
  return true;
}

void BleTerminalActivity::formatStatusText(char* buffer, const size_t bufferSize) const {
  if (commandSendFailed_) {
    std::snprintf(buffer, bufferSize, "%s", crosspoint_plugin_strings::COMMAND_SEND_FAILED);
    return;
  }
  if (needsReset_) {
    std::snprintf(buffer, bufferSize, "%s", crosspoint_plugin_strings::BLE_RESYNC);
    return;
  }
  switch (transport_.status()) {
    case pagewire::PageWireTransport::Status::ADVERTISING:
      std::snprintf(buffer, bufferSize, "%s", crosspoint_plugin_strings::BLE_WAITING);
      return;
    case pagewire::PageWireTransport::Status::PAIRING: {
      const uint32_t passkey = transport_.pairingPasskey();
      if (passkey == 0) {
        std::snprintf(buffer, bufferSize, "%s", crosspoint_plugin_strings::BLE_SECURING);
      } else {
        std::snprintf(buffer, bufferSize, crosspoint_plugin_strings::BLE_PAIRING,
                      static_cast<unsigned long>(passkey));
      }
      return;
    }
    case pagewire::PageWireTransport::Status::CONNECTED:
      std::snprintf(buffer, bufferSize, "%s", crosspoint_plugin_strings::BLE_CONNECTED);
      return;
    case pagewire::PageWireTransport::Status::ERROR:
      std::snprintf(buffer, bufferSize, "%s", crosspoint_plugin_strings::BLE_ERROR);
      return;
    case pagewire::PageWireTransport::Status::STARTING:
    case pagewire::PageWireTransport::Status::STOPPED:
    default:
      std::snprintf(buffer, bufferSize, "%s", crosspoint_plugin_strings::BLE_STARTING);
      return;
  }
}

void BleTerminalActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 crosspoint_plugin_strings::TERMINAL_TITLE);
  const char* title = crosspoint_plugin_strings::TERMINAL_TITLE;
  header_keyboard_button::draw(
      renderer, header_keyboard_button::layout(renderer, metrics, title, 0, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP));
  header_keyboard_button::draw(
      renderer, header_keyboard_button::layout(renderer, metrics, title, 1, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP),
      header_keyboard_button::Glyph::FONT_DECREASE);
  header_keyboard_button::draw(
      renderer, header_keyboard_button::layout(renderer, metrics, title, 2, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP),
      header_keyboard_button::Glyph::FONT_INCREASE);
  drawRefreshButton(renderer,
                    header_keyboard_button::layout(renderer, metrics, title, 3, HEADER_CONTROL_COUNT, HEADER_TITLE_GAP),
                    !followLatest_);

  const int bodyTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bodyHeight = pageHeight - bodyTop - metrics.buttonHintsHeight;
  const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;
  const int fontId = terminalFontId();
  const int lineHeight = renderer.getLineHeight(fontId);
  const int maxLines = lineHeight > 0 ? bodyHeight / lineHeight : 0;

  if (documentMutex_ && xSemaphoreTake(documentMutex_, portMAX_DELAY) == pdTRUE) {
    const auto status = transport_.status();
    const bool securityStatusNeedsScreen = status == pagewire::PageWireTransport::Status::PAIRING ||
                                           status == pagewire::PageWireTransport::Status::ERROR;
    if (commandSendFailed_ || needsReset_ || generation_ == 0 || maxLines <= 0 || securityStatusNeedsScreen) {
      std::array<char, 192> statusMessage{};
      formatStatusText(statusMessage.data(), statusMessage.size());
      const uint32_t pairingPasskey = transport_.pairingPasskey();
      if (status == pagewire::PageWireTransport::Status::PAIRING && pairingPasskey != 0) {
        const int codeLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
        const int hintLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
        const int hintHeight = hintLineHeight * 2;
        const int blockHeight = codeLineHeight + metrics.verticalSpacing + hintHeight;
        const int codeY = bodyTop + (bodyHeight - blockHeight) / 2;
        UITheme::drawCenteredText(renderer, Rect{metrics.contentSidePadding, bodyTop, contentWidth, bodyHeight},
                                  UI_12_FONT_ID, codeY, statusMessage.data(), true, EpdFontFamily::BOLD);
        UITheme::drawCenteredWrappedText(
            renderer,
            Rect{metrics.contentSidePadding, codeY + codeLineHeight + metrics.verticalSpacing, contentWidth,
                 hintHeight},
            UI_10_FONT_ID, crosspoint_plugin_strings::BLE_PAIRING_HINT, 2);
      } else {
        UITheme::drawCenteredWrappedText(
            renderer, Rect{metrics.contentSidePadding, bodyTop, contentWidth, bodyHeight}, UI_10_FONT_ID,
            statusMessage.data(), 5);
      }
    } else {
      size_t cursor = std::min(viewStart_, documentBytes_);
      const size_t renderEnd = std::min(viewEnd_, documentBytes_);
      int y = bodyTop;
      for (int displayedLine = 0; displayedLine < maxLines && cursor < renderEnd; ++displayedLine) {
        const VisualLine line =
            prepareVisualLine(renderer, fontId, document_.data(), renderEnd, cursor, contentWidth, displayLine_);
        if (displayLine_[0] != '\0') renderer.drawText(fontId, metrics.contentSidePadding, y, displayLine_.data());
        y += lineHeight;
        cursor = line.nextStart;
      }
      viewEnd_ = std::min(cursor, renderEnd);
    }
    xSemaphoreGive(documentMutex_);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_FORCE_REFRESH), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  const HalDisplay::RefreshMode refreshMode =
      cleanRefreshPending_ ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH;
  cleanRefreshPending_ = false;
  renderer.displayBuffer(refreshMode);

  if (readyAfterRenderGeneration_ != 0) {
    pendingStatusGeneration_ = readyAfterRenderGeneration_;
    pendingStatusRevision_ = readyAfterRenderRevision_;
    pendingDocumentStatus_ = pagewire::DocumentStatus::READY;
    hasPendingStatus_ = true;
    readyAfterRenderGeneration_ = 0;
    readyAfterRenderRevision_ = 0;
  }
}

#endif
