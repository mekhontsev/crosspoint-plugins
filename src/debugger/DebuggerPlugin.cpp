#include <Arduino.h>
#include <crosspoint/PluginAbi.h>

#include <array>
#include <cstdio>
#include <cstring>

namespace {
std::array<uint8_t, 4096> logSnapshot{};
size_t logBytes = 0;
bool hasLogSnapshot = false;
uint32_t read32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
void write32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(value >> (8 * i));
}
bool copyName(const uint8_t* data, size_t length, char* out, size_t capacity) {
  if (!length || length >= capacity) return false;
  for (size_t i = 0; i < length; ++i) {
    if (data[i] < 32 || data[i] == 127) return false;
    out[i] = static_cast<char>(data[i]);
  }
  out[length] = '\0';
  return true;
}
}  // namespace

extern "C" __attribute__((visibility("default"))) uint32_t crosspoint_plugin_abi() {
  return crosspoint_plugin::ABI_VERSION;
}

extern "C" __attribute__((used, section(".crosspoint.plugin"), visibility("default")))
const crosspoint_plugin::PluginDescriptorV3 crosspoint_plugin_metadata_v3 = {
    "Debugger", "0.2.0", 100, crosspoint_plugin::PLUGIN_FLAG_SERVICE,
};

// One request -> one bounded response. No worker, timer, polling or SD writes.
// Response status: 0 success, 1 invalid, 2 unavailable, 3 operation failed.
extern "C" __attribute__((visibility("default"))) size_t crosspoint_plugin_request_v5(
    const uint8_t* request, size_t length, uint8_t* response, size_t capacity) {
  if (!request || !length || length > 239 || !response || capacity < 1) return 0;
  response[0] = 1;
  switch (request[0]) {
    case 5: {  // Stable log snapshot, first chunk captures; subsequent reads reuse it.
      if (length != 5 || capacity < 197) return 1;
      const uint32_t offset = read32(request + 1);
      if (offset == 0) {
        logBytes = crosspoint_plugin_logs_copy_v5(logSnapshot.data(), logSnapshot.size());
        hasLogSnapshot = true;
      }
      if (!hasLogSnapshot || offset > logBytes) return 1;
      const size_t remaining = logBytes - offset;
      const size_t bytes = remaining < 192 ? remaining : 192;
      response[0] = 0;
      write32(response + 1, static_cast<uint32_t>(logBytes));
      std::memcpy(response + 5, logSnapshot.data() + offset, bytes);
      return 5 + bytes;
    }
    case 1: {  // System status; no firmware/application-specific memory reads.
      if (length != 1 || capacity < 192) return 1;
      const int bytes = std::snprintf(reinterpret_cast<char*>(response + 1), capacity - 1,
          "{\"abi\":%u,\"uptime_ms\":%lu,\"free_heap\":%u,\"free_psram\":%u,\"ble_status\":%u,\"dropped_packets\":%lu}",
          static_cast<unsigned>(crosspoint_plugin::ABI_VERSION), millis(),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getFreePsram()),
          static_cast<unsigned>(crosspoint_plugin_ble_status_v4()),
          static_cast<unsigned long>(crosspoint_plugin_ble_dropped_packets_v4()));
      if (bytes < 0 || static_cast<size_t>(bytes) >= capacity - 1) return 1;
      response[0] = 0;
      return 1 + static_cast<size_t>(bytes);
    }
    case 2: {  // Plugin-owned state and explicitly implemented operations.
      if (length < 4 || request[1] > 15 || size_t(request[1]) + 2 >= length) return 1;
      std::array<char, 16> id{};
      if (!copyName(request + 2, request[1], id.data(), id.size())) return 1;
      const size_t offset = 2 + request[1];
      const size_t bytes = crosspoint_plugin_state_call_v5(id.data(), request + offset, length - offset, response, capacity);
      if (bytes) return bytes;
      response[0] = 2;
      return 1;
    }
    case 3: {  // Read SD by absolute path and byte offset (up to 192 bytes).
      if (length < 7 || capacity < 197) return 1;
      std::array<char, 128> path{};
      if (!copyName(request + 5, length - 5, path.data(), path.size())) return 1;
      size_t bytes = 0;
      uint32_t total = 0;
      if (!crosspoint_plugin_file_read_v5(path.data(), read32(request + 1), response + 5, 192, &bytes, &total)) {
        response[0] = 2;
        return 1;
      }
      response[0] = 0;
      write32(response + 1, total);
      return 5 + bytes;
    }
    case 4: {  // Read an opaque plugin setting. Writes go through its provider.
      std::array<char, 16> id{};
      if (capacity < 129 || !copyName(request + 1, length - 1, id.data(), id.size())) return 1;
      size_t bytes = 0;
      if (!crosspoint_plugin_settings_read_v5(id.data(), response + 1, 128, &bytes)) {
        response[0] = 2;
        return 1;
      }
      response[0] = 0;
      return 1 + bytes;
    }
    default:
      return 1;
  }
}
