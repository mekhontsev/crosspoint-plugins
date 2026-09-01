#pragma once

#include <cstddef>
#include <cstdint>

namespace pagewire {

constexpr size_t PACKET_HEADER_BYTES = 10;
constexpr size_t MAX_PACKET_BYTES = 244;
constexpr size_t MAX_COMMAND_BYTES = MAX_PACKET_BYTES - PACKET_HEADER_BYTES;
constexpr size_t MAX_UPDATE_DATA_BYTES = MAX_PACKET_BYTES - PACKET_HEADER_BYTES - sizeof(uint32_t);
constexpr uint8_t PROTOCOL_VERSION = 5;
constexpr uint8_t MAGIC_0 = 'P';
constexpr uint8_t MAGIC_1 = 'W';

enum class PacketType : uint8_t {
  DOCUMENT_BEGIN = 1,
  DOCUMENT_DATA = 2,
  ACTION = 3,
  COMMAND = 4,
  DOCUMENT_COMMIT = 5,
  RANGE_REQUEST = 6,
  DOCUMENT_STATUS = 7,
  READER_HELLO = 8,
  PLUGIN_UPDATE_HELLO = 9,
  PLUGIN_UPDATE_BEGIN = 10,
  PLUGIN_UPDATE_DATA = 11,
  PLUGIN_UPDATE_END = 12,
  PLUGIN_UPDATE_STATUS = 13,
  DOCUMENT_COPY = 14,
};

enum class Action : uint8_t {
  INTERRUPT_SESSION = 1,
  APPROVE_REQUEST = 2,
  REJECT_REQUEST = 3,
  SUBMIT_INPUT = 4,
};

enum class RangeRequest : uint8_t { CURRENT = 0, BEFORE = 1, AFTER = 2 };
enum class DocumentStatus : uint8_t { READY = 0, RETRY = 1, NEED_FULL = 2 };

constexpr uint8_t DOCUMENT_FLAG_LATEST = 1U;
constexpr uint8_t DOCUMENT_FLAG_PRESENT = 1U << 1U;
constexpr uint8_t DOCUMENT_FLAGS_MASK = DOCUMENT_FLAG_LATEST | DOCUMENT_FLAG_PRESENT;

struct PacketView {
  PacketType type = PacketType::DOCUMENT_BEGIN;
  uint32_t sequence = 0;
  const uint8_t* payload = nullptr;
  size_t payloadLength = 0;
};

enum class AcceptResult : uint8_t {
  DOCUMENT_STARTED,
  DOCUMENT_DATA_ACCEPTED,
  DOCUMENT_COPY_ACCEPTED,
  DOCUMENT_COMMITTED,
  DUPLICATE_IGNORED,
  INVALID_PACKET,
  UNSUPPORTED_VERSION,
  UNEXPECTED_TYPE,
  NEEDS_BEGIN,
  OUT_OF_ORDER,
  TOO_LARGE,
  INVALID_TEXT,
  INVALID_DOCUMENT,
  BASE_MISMATCH,
  CRC_MISMATCH,
};

bool isValidDisplayText(const uint8_t* data, size_t length);
bool isValidCommandText(const uint8_t* data, size_t length);
bool decodePacket(const uint8_t* packet, size_t length, PacketView& view);
size_t encodeActionPacket(Action action, uint32_t sequence, uint8_t* output, size_t capacity);
size_t encodeCommandPacket(const uint8_t* command, size_t commandLength, uint32_t sequence, uint8_t* output,
                           size_t capacity);
size_t encodeRangeRequestPacket(RangeRequest request, uint32_t generation, uint32_t revision, uint32_t anchor,
                                uint16_t capacityBytes, uint32_t sequence, uint8_t* output, size_t capacity);
size_t encodeDocumentStatusPacket(uint32_t generation, uint32_t revision, DocumentStatus status, uint32_t sequence,
                                  uint8_t* output, size_t capacity);
size_t encodeReaderHelloPacket(uint16_t capacityBytes, uint32_t sequence, uint8_t* output, size_t capacity);
size_t encodePluginUpdateHelloPacket(uint32_t pluginAbi, uint16_t maxDataBytes, uint32_t sequence, uint8_t* output,
                                     size_t capacity);
size_t encodePluginUpdateStatusPacket(uint8_t status, uint32_t value, uint32_t sequence, uint8_t* output,
                                      size_t capacity);

class DocumentReceiver final {
 public:
  DocumentReceiver(char* buffer, size_t capacity);

  void setBase(const char* text, size_t length, uint32_t generation, uint32_t revision);
  AcceptResult accept(const uint8_t* packet, size_t length);
  void clear();

  const char* text() const { return buffer_ ? buffer_ : ""; }
  size_t length() const { return currentLength_; }
  uint32_t generation() const { return generation_; }
  uint32_t revision() const { return revision_; }
  uint32_t windowStart() const { return windowStart_; }
  uint32_t documentLength() const { return documentLength_; }
  uint8_t flags() const { return flags_; }
  bool receiving() const { return receiving_; }

 private:
  char* buffer_;
  size_t capacity_;
  const char* baseText_ = nullptr;
  size_t baseLength_ = 0;
  uint32_t baseGeneration_ = 0;
  uint32_t baseRevision_ = 0;
  size_t currentLength_ = 0;
  size_t expectedLength_ = 0;
  uint32_t expectedCrc_ = 0;
  uint32_t generation_ = 0;
  uint32_t revision_ = 0;
  uint32_t requestedBaseRevision_ = 0;
  uint32_t windowStart_ = 0;
  uint32_t documentLength_ = 0;
  uint8_t flags_ = 0;
  uint32_t expectedSequence_ = 0;
  uint32_t committedGeneration_ = 0;
  uint32_t committedRevision_ = 0;
  bool receiving_ = false;
  bool baseMismatch_ = false;

  AcceptResult acceptBegin(uint32_t sequence, const uint8_t* payload, size_t payloadLength);
  AcceptResult acceptData(uint32_t sequence, const uint8_t* payload, size_t payloadLength);
  AcceptResult acceptCopy(uint32_t sequence, const uint8_t* payload, size_t payloadLength);
  AcceptResult acceptCommit(uint32_t sequence, const uint8_t* payload, size_t payloadLength);
  bool append(const uint8_t* data, size_t length);
};

}  // namespace pagewire
