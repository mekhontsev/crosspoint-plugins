#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "pagewire/PageWireProtocol.h"

namespace {
void le(std::vector<uint8_t>& bytes, uint32_t value, size_t width = 4) {
  for (size_t i = 0; i < width; ++i) bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}
uint32_t crc(const std::string& text) {
  uint32_t value = ~0U;
  for (uint8_t byte : text) {
    value ^= byte;
    for (int bit = 0; bit < 8; ++bit) value = (value >> 1) ^ (0xEDB88320U & (0U - (value & 1U)));
  }
  return ~value;
}
pagewire::AcceptResult send(pagewire::DocumentReceiver& receiver, uint8_t type, uint32_t sequence,
                            const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> packet{'P', 'W', pagewire::PROTOCOL_VERSION, type};
  le(packet, sequence);
  le(packet, payload.size(), 2);
  packet.insert(packet.end(), payload.begin(), payload.end());
  return receiver.accept(packet.data(), packet.size());
}
pagewire::AcceptResult begin(pagewire::DocumentReceiver& receiver, uint32_t sequence, uint32_t revision,
                             uint32_t base, const std::string& text, uint32_t checksum) {
  std::vector<uint8_t> payload;
  le(payload, 1);
  le(payload, revision);
  le(payload, base);
  le(payload, 0);
  le(payload, text.size());
  le(payload, text.size(), 2);
  le(payload, checksum);
  payload.push_back(pagewire::DOCUMENT_FLAG_LATEST);
  return send(receiver, 1, sequence, payload);
}
pagewire::AcceptResult commit(pagewire::DocumentReceiver& receiver, uint32_t sequence, uint32_t revision) {
  std::vector<uint8_t> payload;
  le(payload, 1);
  le(payload, revision);
  return send(receiver, 5, sequence, payload);
}
}  // namespace

int main() {
  using pagewire::AcceptResult;
  std::array<char, 257> staging{}, baseline{}, displayed{};
  pagewire::DocumentReceiver receiver(staging.data(), 256);
  const std::string original = "previous page\nlast page";
  assert(begin(receiver, 1, 1, 0, original, crc(original)) == AcceptResult::DOCUMENT_STARTED);
  assert(send(receiver, 2, 2, {original.begin(), original.end()}) == AcceptResult::DOCUMENT_DATA_ACCEPTED);
  assert(commit(receiver, 3, 1) == AcceptResult::DOCUMENT_COMMITTED);
  std::strcpy(displayed.data(), receiver.text());
  std::strcpy(baseline.data(), receiver.text());
  receiver.setBase(baseline.data(), original.size(), 1, 1);

  // A new live revision advances the delta base, not the history being read.
  const std::string updated = original + "\nnew output";
  assert(begin(receiver, 4, 2, 1, updated, crc(updated)) == AcceptResult::DOCUMENT_STARTED);
  std::vector<uint8_t> copy;
  le(copy, 0, 2);
  le(copy, original.size(), 2);
  assert(send(receiver, 14, 5, copy) == AcceptResult::DOCUMENT_COPY_ACCEPTED);
  const std::string appended = "\nnew output";
  assert(send(receiver, 2, 6, {appended.begin(), appended.end()}) == AcceptResult::DOCUMENT_DATA_ACCEPTED);
  assert(commit(receiver, 7, 2) == AcceptResult::DOCUMENT_COMMITTED);
  std::strcpy(baseline.data(), receiver.text());
  receiver.setBase(baseline.data(), updated.size(), 1, 2);
  assert(std::string(displayed.data()) == original);

  assert(begin(receiver, 8, 3, 2, updated, crc(updated)) == AcceptResult::DOCUMENT_STARTED);
  copy.clear();
  le(copy, 0, 2);
  le(copy, updated.size(), 2);
  assert(send(receiver, 14, 9, copy) == AcceptResult::DOCUMENT_COPY_ACCEPTED);
  assert(commit(receiver, 10, 3) == AcceptResult::DOCUMENT_COMMITTED);
  assert(std::string(receiver.text()) == updated);
  assert(std::string(displayed.data()) == original);
  assert(commit(receiver, 10, 3) == AcceptResult::DUPLICATE_IGNORED);

  receiver.clear();
  assert(commit(receiver, 10, 3) == AcceptResult::NEEDS_BEGIN);
  assert(begin(receiver, 11, 4, 99, updated, crc(updated)) == AcceptResult::BASE_MISMATCH);
  assert(std::string(baseline.data()) == updated);
  assert(begin(receiver, 12, 5, 0, original, crc(original) ^ 1) == AcceptResult::DOCUMENT_STARTED);
  assert(send(receiver, 2, 13, {original.begin(), original.end()}) == AcceptResult::DOCUMENT_DATA_ACCEPTED);
  assert(commit(receiver, 14, 5) == AcceptResult::CRC_MISMATCH);
  assert(std::string(baseline.data()) == updated);
  assert(std::string(displayed.data()) == original);
  std::cout << "Document receiver: frozen history, delta base, CRC, reset and duplicate checks passed\n";
}
