#include <crosspoint/PluginAbi.h>
#include <array>
#include <cassert>
#include <cstring>

extern "C" size_t crosspoint_plugin_request_v5(const uint8_t*, size_t, uint8_t*, size_t);
static int calls;
extern "C" size_t crosspoint_plugin_logs_copy_v5(uint8_t* out, size_t capacity) {
  assert(capacity == 4096);
  std::memcpy(out, "[ERR] test\n", 11);
  return 11;
}
extern "C" crosspoint_plugin::PluginBleStatusV4 crosspoint_plugin_ble_status_v4() {
  return crosspoint_plugin::PluginBleStatusV4::CONNECTED;
}
extern "C" uint32_t crosspoint_plugin_ble_dropped_packets_v4() { return 0; }
extern "C" size_t crosspoint_plugin_state_call_v5(const char* id, const uint8_t* data, size_t length,
                                                  uint8_t* out, size_t) {
  ++calls;
  assert(std::strcmp(id, "terminal") == 0 && length == 1 && data[0] == 2);
  out[0] = 0;
  return 1;
}
extern "C" uint8_t crosspoint_plugin_file_read_v5(const char* path, uint32_t offset, uint8_t* data,
                                                 size_t capacity, size_t* length, uint32_t* total) {
  ++calls;
  assert(std::strcmp(path, "/crash_report.txt") == 0 && offset == 256 && capacity == 192);
  std::memcpy(data, "test", 4);
  *length = 4;
  *total = 260;
  return 1;
}
extern "C" uint8_t crosspoint_plugin_settings_read_v5(const char* id, uint8_t* data, size_t capacity, size_t* length) {
  ++calls;
  assert(std::strcmp(id, "terminal") == 0 && capacity == 128);
  data[0] = 3;
  *length = 1;
  return 1;
}
int main() {
  std::array<uint8_t, 239> response{};
  uint8_t status[] = {1};
  assert(crosspoint_plugin_request_v5(status, 1, response.data(), response.size()) > 1);
  assert(response[0] == 0);
  assert(std::strstr(reinterpret_cast<char*>(response.data() + 1), "\"abi\":5"));
  uint8_t refresh[] = {2, 8, 't', 'e', 'r', 'm', 'i', 'n', 'a', 'l', 2};
  assert(crosspoint_plugin_request_v5(refresh, sizeof(refresh), response.data(), response.size()) == 1);
  assert(response[0] == 0 && calls == 1);
  uint8_t file[] = {3, 0, 1, 0, 0, '/', 'c', 'r', 'a', 's', 'h', '_', 'r', 'e', 'p', 'o', 'r', 't', '.', 't', 'x', 't'};
  assert(crosspoint_plugin_request_v5(file, sizeof(file), response.data(), response.size()) == 9);
  assert(response[0] == 0 && response[1] == 4 && response[2] == 1 && calls == 2);
  uint8_t setting[] = {4, 't', 'e', 'r', 'm', 'i', 'n', 'a', 'l'};
  assert(crosspoint_plugin_request_v5(setting, sizeof(setting), response.data(), response.size()) == 2);
  assert(response[0] == 0 && response[1] == 3 && calls == 3);
  // Malformed inputs cannot reach host operations. Exercise all tiny lengths
  // and every opcode with ASan/UBSan as well as the normal native test build.
  for (unsigned op = 0; op < 256; ++op) {
    std::array<uint8_t, 240> invalid{};
    invalid[0] = static_cast<uint8_t>(op);
    for (size_t length = 0; length <= 4; ++length) {
      crosspoint_plugin_request_v5(invalid.data(), length, response.data(), response.size());
    }
    assert(crosspoint_plugin_request_v5(invalid.data(), invalid.size(), response.data(), response.size()) == 0);
  }
  assert(calls == 3);
  uint8_t logs[] = {5, 0, 0, 0, 0};
  assert(crosspoint_plugin_request_v5(logs, sizeof(logs), response.data(), response.size()) == 16);
  assert(response[0] == 0 && response[1] == 11);
  logs[1] = 11;
  assert(crosspoint_plugin_request_v5(logs, sizeof(logs), response.data(), response.size()) == 5);
  logs[1] = 12;
  assert(crosspoint_plugin_request_v5(logs, sizeof(logs), response.data(), response.size()) == 1);
  assert(crosspoint_plugin_request_v5(nullptr, 1, response.data(), response.size()) == 0);
  assert(crosspoint_plugin_request_v5(status, 1, nullptr, 239) == 0);
}
