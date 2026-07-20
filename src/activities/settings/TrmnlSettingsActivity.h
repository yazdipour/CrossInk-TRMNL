#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TrmnlSettingsActivity final : public Activity {
 public:
  TrmnlSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TrmnlSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;

 private:
  ButtonNavigator buttonNavigator;
  uint8_t selectedIndex = 0;

  void handleSelection();
};
