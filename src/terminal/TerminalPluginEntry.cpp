#include <EpdFont.h>
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <builtinFonts/terminalmono_10_regular.h>
#include <builtinFonts/terminalmono_12_regular.h>
#include <builtinFonts/terminalmono_14_regular.h>
#include <builtinFonts/terminalmono_16_regular.h>
#include <builtinFonts/terminalmono_18_regular.h>
#include <builtinFonts/terminalmono_20_regular.h>
#include <builtinFonts/terminalmono_22_regular.h>
#include <builtinFonts/terminalmono_24_regular.h>
#include <builtinFonts/terminalmono_8_regular.h>
#include <crosspoint/PluginAbi.h>
#include <crosspoint/PluginStrings.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

#include "BleTerminalActivity.h"
#include "TerminalFontIds.h"

namespace {

constexpr std::array<const EpdFontData*, 9> FONT_DATA = {
    &terminalmono_8_regular,  &terminalmono_10_regular, &terminalmono_12_regular,
    &terminalmono_14_regular, &terminalmono_16_regular, &terminalmono_18_regular,
    &terminalmono_20_regular, &terminalmono_22_regular, &terminalmono_24_regular,
};

constexpr std::array<int, 9> FONT_IDS = {
    TERMINAL_MONO_8_FONT_ID,  TERMINAL_MONO_10_FONT_ID, TERMINAL_MONO_12_FONT_ID,
    TERMINAL_MONO_14_FONT_ID, TERMINAL_MONO_16_FONT_ID, TERMINAL_MONO_18_FONT_ID,
    TERMINAL_MONO_20_FONT_ID, TERMINAL_MONO_22_FONT_ID, TERMINAL_MONO_24_FONT_ID,
};

struct FontState {
  alignas(EpdFont) std::byte fonts[FONT_DATA.size()][sizeof(EpdFont)];
  alignas(EpdFontFamily) std::byte families[FONT_DATA.size()][sizeof(EpdFontFamily)];
  bool initialized;
};

FontState fontState;

EpdFont* fontAt(const size_t index) { return reinterpret_cast<EpdFont*>(fontState.fonts[index]); }

EpdFontFamily* familyAt(const size_t index) { return reinterpret_cast<EpdFontFamily*>(fontState.families[index]); }

void registerFonts(GfxRenderer& renderer) {
  if (fontState.initialized) return;
  for (size_t index = 0; index < FONT_DATA.size(); ++index) {
    new (fontAt(index)) EpdFont(FONT_DATA[index]);
    new (familyAt(index)) EpdFontFamily(fontAt(index));
    renderer.insertFont(FONT_IDS[index], *familyAt(index));
  }
  fontState.initialized = true;
}

void unregisterFonts(GfxRenderer& renderer) {
  if (!fontState.initialized) return;
  for (size_t index = 0; index < FONT_DATA.size(); ++index) {
    renderer.removeFont(FONT_IDS[index]);
    familyAt(index)->~EpdFontFamily();
    fontAt(index)->~EpdFont();
  }
  fontState.initialized = false;
}

}  // namespace

void releaseTerminalPluginFonts(GfxRenderer& renderer) { unregisterFonts(renderer); }

extern "C" __attribute__((visibility("default"))) uint32_t crosspoint_plugin_abi() {
  return crosspoint_plugin::ABI_VERSION;
}

extern "C" __attribute__((used, section(".crosspoint.plugin"), visibility("default")))
const crosspoint_plugin::PluginDescriptorV3 crosspoint_plugin_metadata_v3 = {
    "Terminal",
    "0.2.0",
    10,
    0,
};

extern "C" __attribute__((visibility("default"))) Activity* crosspoint_plugin_create(GfxRenderer* renderer,
                                                                                     MappedInputManager* mappedInput) {
  if (!renderer || !mappedInput) return nullptr;
  registerFonts(*renderer);
  auto activity = makeUniqueNoThrow<BleTerminalActivity>(*renderer, *mappedInput);
  if (!activity) {
    unregisterFonts(*renderer);
    return nullptr;
  }
  return activity.release();
}
