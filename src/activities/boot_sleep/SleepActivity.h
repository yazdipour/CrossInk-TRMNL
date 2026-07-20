#pragma once
#include <string>
#include <utility>

#include "activities/Activity.h"

class Bitmap;

class SleepActivity final : public Activity {
 public:
  explicit SleepActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool canSnapshotOverlayBackground,
                         std::string currentBookPath = {}, bool fromTimeout = false)
      : Activity("Sleep", renderer, mappedInput),
        canSnapshotOverlayBackground(canSnapshotOverlayBackground),
        currentBookPath(std::move(currentBookPath)),
        fromTimeout(fromTimeout) {}
  void onEnter() override;

 private:
  void renderDefaultSleepScreen() const;
  void renderCustomSleepScreen() const;
  void renderCoverSleepScreen() const;
  void renderReadingStatsSleepScreen() const;
  void renderMinimalSleepScreen() const;
  void renderMinimalStatsSleepScreen() const;
  void renderDashboardSleepScreen() const;
  void renderBitmapSleepScreen(const Bitmap& bitmap) const;
  bool renderPngSleepScreen(const std::string& path) const;
  bool renderTrmnlCachedImage() const;
  void renderTrmnlSleepScreen() const;
  void renderLastScreenSleepScreen() const;
  void renderBlankSleepScreen() const;
  void renderOverlaySleepScreen() const;
  bool canSnapshotOverlayBackground = false;
  bool overlayBackgroundBufferStored = false;
  std::string currentBookPath;
  bool fromTimeout = false;
};
