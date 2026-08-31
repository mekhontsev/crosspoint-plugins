#pragma once

#include "activities/Activity.h"

class PluginsActivity final : public Activity {
 public:
  PluginsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;

 private:
  void openTerminal();
  bool loadFailed_ = false;
};
