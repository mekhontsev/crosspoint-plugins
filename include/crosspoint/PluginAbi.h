#pragma once

#include <cstddef>
#include <cstdint>

class Activity;
class GfxRenderer;
class MappedInputManager;

namespace crosspoint_plugin {

constexpr uint32_t ABI_VERSION = 1;
constexpr char TERMINAL_MODULE[] = "terminal";
constexpr char CREATE_SYMBOL[] = "crosspoint_plugin_create";
constexpr char ABI_SYMBOL[] = "crosspoint_plugin_abi";
constexpr char TRAILER_MAGIC[] = "X4PLUG01";
constexpr uint32_t TRAILER_FORMAT = 1;
constexpr size_t SHA256_BYTES = 32;

}  // namespace crosspoint_plugin

extern "C" Activity* crosspoint_plugin_create_child(const char* moduleName, GfxRenderer* renderer,
                                                    MappedInputManager* mappedInput);
