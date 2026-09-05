#include "pagewire/PageWireProtocol.h"

#include <cstring>

#include "pagewire/PageWireTransport.h"

namespace pagewire {

PageWireTransport PAGEWIRE_TRANSPORT;

namespace {

constexpr size_t DOCUMENT_BEGIN_PAYLOAD_BYTES = 27;
constexpr size_t DOCUMENT_COMMIT_PAYLOAD_BYTES = 8;

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8U);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) | (static_cast<uint32_t>(data[3]) << 24U);
}

void writeLe16(uint8_t* data, const uint16_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8U);
}

void writeLe32(uint8_t* data, const uint32_t value) {
  data[0] = static_cast<uint8_t>(value);
  data[1] = static_cast<uint8_t>(value >> 8U);
  data[2] = static_cast<uint8_t>(value >> 16U);
  data[3] = static_cast<uint8_t>(value >> 24U);
}

bool allowedCodepoint(const uint32_t codepoint) {
  if (codepoint == '\n' || codepoint == '\t') return true;
  return codepoint >= 0x20 && codepoint != 0x7F && !(codepoint >= 0x80 && codepoint <= 0x9F);
}

uint32_t crc32(const uint8_t* data, const size_t length) {
  uint32_t crc = 0xFFFFFFFFU;
  for (size_t index = 0; index < length; ++index) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
  }
  return crc ^ 0xFFFFFFFFU;
}

size_t encodePacket(const PacketType type, const uint8_t* payload, const size_t payloadLength, const uint32_t sequence,
                    uint8_t* output, const size_t capacity) {
  const size_t packetLength = PACKET_HEADER_BYTES + payloadLength;
  if (!output || capacity < packetLength || packetLength > MAX_PACKET_BYTES || payloadLength > UINT16_MAX ||
      (!payload && payloadLength != 0)) {
    return 0;
  }
  output[0] = MAGIC_0;
  output[1] = MAGIC_1;
  output[2] = PROTOCOL_VERSION;
  output[3] = static_cast<uint8_t>(type);
  writeLe32(output + 4, sequence);
  writeLe16(output + 8, static_cast<uint16_t>(payloadLength));
  if (payloadLength != 0) std::memcpy(output + PACKET_HEADER_BYTES, payload, payloadLength);
  return packetLength;
}

bool isAllowedAction(const Action action) {
  return action == Action::INTERRUPT_SESSION || action == Action::APPROVE_REQUEST ||
         action == Action::REJECT_REQUEST || action == Action::SUBMIT_INPUT;
}

bool isAllowedRangeRequest(const RangeRequest request) {
  return request == RangeRequest::CURRENT || request == RangeRequest::BEFORE || request == RangeRequest::AFTER;
}

}  // namespace

bool isValidDisplayText(const uint8_t* data, const size_t length) {
  if (!data && length != 0) return false;
  size_t index = 0;
  while (index < length) {
    const uint8_t first = data[index++];
    uint32_t codepoint = 0;
    size_t continuation = 0;
    if (first < 0x80) {
      codepoint = first;
    } else if ((first & 0xE0U) == 0xC0U) {
      codepoint = first & 0x1FU;
      continuation = 1;
      if (codepoint < 2) return false;
    } else if ((first & 0xF0U) == 0xE0U) {
      codepoint = first & 0x0FU;
      continuation = 2;
    } else if ((first & 0xF8U) == 0xF0U) {
      codepoint = first & 0x07U;
      continuation = 3;
    } else {
      return false;
    }
    if (continuation > length - index) return false;
    for (size_t part = 0; part < continuation; ++part) {
      const uint8_t value = data[index++];
      if ((value & 0xC0U) != 0x80U) return false;
      codepoint = (codepoint << 6U) | (value & 0x3FU);
    }
    if ((continuation == 2 && codepoint < 0x800) || (continuation == 3 && codepoint < 0x10000) ||
        codepoint > 0x10FFFF || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || !allowedCodepoint(codepoint)) {
      return false;
    }
  }
  return true;
}

bool isValidCommandText(const uint8_t* data, const size_t length) {
  if (!data || length == 0 || !isValidDisplayText(data, length)) return false;
  for (size_t index = 0; index < length; ++index) {
    if (data[index] == '\n' || data[index] == '\t') return false;
  }
  return true;
}

bool decodePacket(const uint8_t* packet, const size_t length, PacketView& view) {
  if (!packet || length < PACKET_HEADER_BYTES || length > MAX_PACKET_BYTES || packet[0] != MAGIC_0 ||
      packet[1] != MAGIC_1 || packet[2] != PROTOCOL_VERSION) {
    return false;
  }
  const size_t payloadLength = readLe16(packet + 8);
  if (payloadLength != length - PACKET_HEADER_BYTES) return false;
  view.type = static_cast<PacketType>(packet[3]);
  view.sequence = readLe32(packet + 4);
  view.payload = packet + PACKET_HEADER_BYTES;
  view.payloadLength = payloadLength;
  return true;
}

size_t encodeActionPacket(const Action action, const uint32_t sequence, uint8_t* output, const size_t capacity) {
  if (!isAllowedAction(action)) return 0;
  const uint8_t payload = static_cast<uint8_t>(action);
  return encodePacket(PacketType::ACTION, &payload, sizeof(payload), sequence, output, capacity);
}

size_t encodeCommandPacket(const uint8_t* command, const size_t commandLength, const uint32_t sequence, uint8_t* output,
                           const size_t capacity) {
  if (commandLength > MAX_COMMAND_BYTES || !isValidCommandText(command, commandLength)) return 0;
  return encodePacket(PacketType::COMMAND, command, commandLength, sequence, output, capacity);
}

size_t encodeRangeRequestPacket(const RangeRequest request, const uint32_t generation, const uint32_t revision,
                                const uint32_t anchor, const uint16_t capacityBytes, const uint32_t sequence,
                                uint8_t* output, const size_t capacity) {
  if (!isAllowedRangeRequest(request) || capacityBytes == 0) return 0;
  uint8_t payload[15]{};
  payload[0] = static_cast<uint8_t>(request);
  writeLe32(payload + 1, generation);
  writeLe32(payload + 5, revision);
  writeLe32(payload + 9, anchor);
  writeLe16(payload + 13, capacityBytes);
  return encodePacket(PacketType::RANGE_REQUEST, payload, sizeof(payload), sequence, output, capacity);
}

size_t encodeDocumentStatusPacket(const uint32_t generation, const uint32_t revision, const DocumentStatus status,
                                  const uint32_t sequence, uint8_t* output, const size_t capacity) {
  if (generation == 0 || revision == 0 || static_cast<uint8_t>(status) > static_cast<uint8_t>(DocumentStatus::NEED_FULL)) {
    return 0;
  }
  uint8_t payload[9]{};
  writeLe32(payload, generation);
  writeLe32(payload + 4, revision);
  payload[8] = static_cast<uint8_t>(status);
  return encodePacket(PacketType::DOCUMENT_STATUS, payload, sizeof(payload), sequence, output, capacity);
}

size_t encodeReaderHelloPacket(const uint16_t capacityBytes, const uint32_t sequence, uint8_t* output,
                               const size_t capacity) {
  if (capacityBytes == 0) return 0;
  uint8_t payload[2]{};
  writeLe16(payload, capacityBytes);
  return encodePacket(PacketType::READER_HELLO, payload, sizeof(payload), sequence, output, capacity);
}

size_t encodePluginUpdateHelloPacket(const uint32_t pluginAbi, const uint16_t maxDataBytes, const uint32_t sequence,
                                     uint8_t* output, const size_t capacity) {
  if (pluginAbi == 0 || maxDataBytes == 0) return 0;
  uint8_t payload[6]{};
  writeLe32(payload, pluginAbi);
  writeLe16(payload + 4, maxDataBytes);
  return encodePacket(PacketType::PLUGIN_UPDATE_HELLO, payload, sizeof(payload), sequence, output, capacity);
}

size_t encodePluginUpdateStatusPacket(const uint8_t status, const uint32_t value, const uint32_t sequence,
                                      uint8_t* output, const size_t capacity) {
  if (status < 1 || status > 3) return 0;
  uint8_t payload[5]{};
  payload[0] = status;
  writeLe32(payload + 1, value);
  return encodePacket(PacketType::PLUGIN_UPDATE_STATUS, payload, sizeof(payload), sequence, output, capacity);
}

DocumentReceiver::DocumentReceiver(char* buffer, const size_t capacity) : buffer_(buffer), capacity_(capacity) {
  clear();
}

void DocumentReceiver::setBase(const char* text, const size_t length, const uint32_t generation,
                               const uint32_t revision) {
  baseText_ = text;
  baseLength_ = length;
  baseGeneration_ = generation;
  baseRevision_ = revision;
}

void DocumentReceiver::clear() {
  currentLength_ = 0;
  expectedLength_ = 0;
  expectedCrc_ = 0;
  generation_ = 0;
  revision_ = 0;
  committedGeneration_ = 0;
  committedRevision_ = 0;
  requestedBaseRevision_ = 0;
  windowStart_ = 0;
  documentLength_ = 0;
  flags_ = 0;
  expectedSequence_ = 0;
  receiving_ = false;
  baseMismatch_ = false;
  if (buffer_ && capacity_ != 0) buffer_[0] = '\0';
}

AcceptResult DocumentReceiver::accept(const uint8_t* packet, const size_t length) {
  if (packet && length >= 3 && packet[0] == MAGIC_0 && packet[1] == MAGIC_1 && packet[2] != PROTOCOL_VERSION) {
    return AcceptResult::UNSUPPORTED_VERSION;
  }
  PacketView view{};
  if (!decodePacket(packet, length, view)) return AcceptResult::INVALID_PACKET;
  switch (view.type) {
    case PacketType::DOCUMENT_BEGIN:
      return acceptBegin(view.sequence, view.payload, view.payloadLength);
    case PacketType::DOCUMENT_DATA:
      return acceptData(view.sequence, view.payload, view.payloadLength);
    case PacketType::DOCUMENT_COPY:
      return acceptCopy(view.sequence, view.payload, view.payloadLength);
    case PacketType::DOCUMENT_COMMIT:
      return acceptCommit(view.sequence, view.payload, view.payloadLength);
    default:
      return AcceptResult::UNEXPECTED_TYPE;
  }
}

AcceptResult DocumentReceiver::acceptBegin(const uint32_t sequence, const uint8_t* payload, const size_t payloadLength) {
  if (!payload || payloadLength != DOCUMENT_BEGIN_PAYLOAD_BYTES) return AcceptResult::INVALID_PACKET;
  const uint32_t generation = readLe32(payload);
  const uint32_t revision = readLe32(payload + 4);
  const uint32_t requestedBaseRevision = readLe32(payload + 8);
  const uint32_t windowStart = readLe32(payload + 12);
  const uint32_t documentLength = readLe32(payload + 16);
  const size_t expectedLength = readLe16(payload + 20);
  const uint32_t expectedCrc = readLe32(payload + 22);
  const uint8_t flags = payload[26];
  if (generation == 0 || revision == 0 || expectedLength > capacity_ || windowStart > documentLength ||
      expectedLength > documentLength - windowStart || (flags & ~DOCUMENT_FLAGS_MASK) != 0) {
    return expectedLength > capacity_ ? AcceptResult::TOO_LARGE : AcceptResult::INVALID_DOCUMENT;
  }
  generation_ = generation;
  revision_ = revision;
  requestedBaseRevision_ = requestedBaseRevision;
  windowStart_ = windowStart;
  documentLength_ = documentLength;
  flags_ = flags;
  currentLength_ = 0;
  expectedLength_ = expectedLength;
  expectedCrc_ = expectedCrc;
  expectedSequence_ = sequence + 1U;
  receiving_ = false;
  baseMismatch_ = false;
  if (requestedBaseRevision != 0 &&
      (baseGeneration_ != generation || baseRevision_ != requestedBaseRevision || !baseText_)) {
    baseMismatch_ = true;
    return AcceptResult::BASE_MISMATCH;
  }
  receiving_ = true;
  if (buffer_) buffer_[0] = '\0';
  return AcceptResult::DOCUMENT_STARTED;
}

bool DocumentReceiver::append(const uint8_t* data, const size_t length) {
  if ((!data && length != 0) || currentLength_ > expectedLength_ || length > expectedLength_ - currentLength_ ||
      !buffer_) {
    return false;
  }
  if (length != 0) std::memcpy(buffer_ + currentLength_, data, length);
  currentLength_ += length;
  buffer_[currentLength_] = '\0';
  return true;
}

AcceptResult DocumentReceiver::acceptData(const uint32_t sequence, const uint8_t* payload, const size_t payloadLength) {
  if (baseMismatch_) return AcceptResult::DUPLICATE_IGNORED;
  if (!receiving_) return AcceptResult::NEEDS_BEGIN;
  if (sequence == expectedSequence_ - 1U) return AcceptResult::DUPLICATE_IGNORED;
  if (sequence != expectedSequence_) return AcceptResult::OUT_OF_ORDER;
  if (!payload || payloadLength == 0 || !append(payload, payloadLength)) return AcceptResult::TOO_LARGE;
  expectedSequence_++;
  return AcceptResult::DOCUMENT_DATA_ACCEPTED;
}

AcceptResult DocumentReceiver::acceptCopy(const uint32_t sequence, const uint8_t* payload, const size_t payloadLength) {
  if (baseMismatch_) return AcceptResult::DUPLICATE_IGNORED;
  if (!receiving_) return AcceptResult::NEEDS_BEGIN;
  if (sequence == expectedSequence_ - 1U) return AcceptResult::DUPLICATE_IGNORED;
  if (sequence != expectedSequence_) return AcceptResult::OUT_OF_ORDER;
  if (requestedBaseRevision_ == 0 || !payload || payloadLength != 4) return AcceptResult::INVALID_DOCUMENT;
  const size_t offset = readLe16(payload);
  const size_t copyLength = readLe16(payload + 2);
  if (copyLength == 0 || offset > baseLength_ || copyLength > baseLength_ - offset ||
      !append(reinterpret_cast<const uint8_t*>(baseText_ + offset), copyLength)) {
    return AcceptResult::INVALID_DOCUMENT;
  }
  expectedSequence_++;
  return AcceptResult::DOCUMENT_COPY_ACCEPTED;
}

AcceptResult DocumentReceiver::acceptCommit(const uint32_t sequence, const uint8_t* payload,
                                            const size_t payloadLength) {
  if (!payload || payloadLength != DOCUMENT_COMMIT_PAYLOAD_BYTES) return AcceptResult::INVALID_PACKET;
  const uint32_t generation = readLe32(payload);
  const uint32_t revision = readLe32(payload + 4);
  if (baseMismatch_ && generation == generation_ && revision == revision_) {
    return AcceptResult::DUPLICATE_IGNORED;
  }
  if (!receiving_) {
    return generation == committedGeneration_ && revision == committedRevision_ ? AcceptResult::DUPLICATE_IGNORED
                                                                                : AcceptResult::NEEDS_BEGIN;
  }
  if (sequence == expectedSequence_ - 1U && generation == committedGeneration_ && revision == committedRevision_) {
    return AcceptResult::DUPLICATE_IGNORED;
  }
  if (sequence != expectedSequence_) return AcceptResult::OUT_OF_ORDER;
  receiving_ = false;
  if (generation != generation_ || revision != revision_ || currentLength_ != expectedLength_) {
    return AcceptResult::INVALID_DOCUMENT;
  }
  if (!isValidDisplayText(reinterpret_cast<const uint8_t*>(buffer_), currentLength_)) {
    return AcceptResult::INVALID_TEXT;
  }
  if (crc32(reinterpret_cast<const uint8_t*>(buffer_), currentLength_) != expectedCrc_) {
    return AcceptResult::CRC_MISMATCH;
  }
  committedGeneration_ = generation_;
  committedRevision_ = revision_;
  return AcceptResult::DOCUMENT_COMMITTED;
}

}  // namespace pagewire
