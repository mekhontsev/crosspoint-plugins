#include "PluginsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <crosspoint/PluginAbi.h>
#include <crosspoint/PluginStrings.h>

#include <memory>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

PluginsActivity::PluginsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Plugins", renderer, mappedInput) {}

void PluginsActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void PluginsActivity::openTerminal() {
  Activity* child = crosspoint_plugin_create_child(crosspoint_plugin::TERMINAL_MODULE, &renderer, &mappedInput);
  if (!child) {
    loadFailed_ = true;
    requestUpdate();
    return;
  }
  loadFailed_ = false;
  startActivityForResult(std::unique_ptr<Activity>(child), {});
}

void PluginsActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome(HomeMenuItem::PLUGINS);
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    openTerminal();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (mappedInput.wasTapInRect(metrics.contentSidePadding, rowTop,
                               renderer.getScreenWidth() - 2 * metrics.contentSidePadding, metrics.listRowHeight)) {
    openTerminal();
  }
}

bool PluginsActivity::handleHomeGesture() {
  onGoHome(HomeMenuItem::PLUGINS);
  return true;
}

void PluginsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int rowTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int rowLeft = metrics.contentSidePadding;
  const int rowWidth = pageWidth - 2 * rowLeft;
  const int rowHeight = metrics.listRowHeight;

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_PLUGINS));
  renderer.drawRect(rowLeft, rowTop, rowWidth, rowHeight, true);
  const int textY = rowTop + (rowHeight - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawText(UI_12_FONT_ID, rowLeft + metrics.listSidePadding, textY, crosspoint_plugin_strings::TERMINAL_TITLE);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  if (loadFailed_) GUI.drawPopup(renderer, tr(STR_PLUGIN_MODULE_LOAD_FAILED));
  renderer.displayBuffer();
}
