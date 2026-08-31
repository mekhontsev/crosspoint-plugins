#include <Memory.h>
#include <crosspoint/PluginAbi.h>

#include "PluginsActivity.h"

extern "C" __attribute__((visibility("default"))) uint32_t crosspoint_plugin_abi() {
  return crosspoint_plugin::ABI_VERSION;
}

extern "C" __attribute__((visibility("default"))) Activity* crosspoint_plugin_create(GfxRenderer* renderer,
                                                                                     MappedInputManager* mappedInput) {
  if (!renderer || !mappedInput) return nullptr;
  auto activity = makeUniqueNoThrow<PluginsActivity>(*renderer, *mappedInput);
  return activity.release();
}
