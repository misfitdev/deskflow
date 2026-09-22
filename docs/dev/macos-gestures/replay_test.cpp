// Live end-to-end check of src/lib/platform/OSXSwipe.cpp.
//
// Swallows each real trackpad dock swipe, decodes it with DockSwipeDetector,
// and replays it with postDockSwipe -- the same code a Deskflow server and
// client run, minus the network. If a swipe still switches Spaces or opens
// Mission Control as usual, capture and synthesis agree with macOS.
//
// Build from the repository root:
//   clang++ -std=c++20 -O2 -Isrc/lib -o replay_test \
//     docs/dev/macos-gestures/replay_test.cpp src/lib/platform/OSXSwipe.cpp \
//     -framework ApplicationServices -framework CoreFoundation
//
// Usage: replay_test [seconds]   (needs Accessibility permission)

#include "platform/OSXSwipe.h"

#include <ApplicationServices/ApplicationServices.h>
#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>

namespace {

const auto kFieldCGSEventType = static_cast<CGEventField>(55);
const auto kFieldProgress = static_cast<CGEventField>(124);
const auto kFieldVelocityX = static_cast<CGEventField>(129);
const auto kFieldVelocityY = static_cast<CGEventField>(130);
const auto kFieldPhase = static_cast<CGEventField>(132);
const int64_t kDockControl = 30;
const int64_t kEnded = 4;

// Each synthetic swipe is three DockControl + companion pairs, which come back
// through this tap and must not be decoded as new swipes.
const int kSyntheticEventsPerSwipe = 6;

CFMachPortRef g_tap = nullptr;
deskflow::osx::DockSwipeDetector g_detector;
int g_passthrough = 0;
int g_replayed = 0;
int g_callbacks = 0;
int g_dockEvents[9] = {}; // indexed by phase
int g_companionEvents = 0;

uint64_t activeSpace()
{
  static auto *handle = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight", RTLD_LAZY);
  static auto *mainConnection = reinterpret_cast<int (*)()>(dlsym(handle, "CGSMainConnectionID"));
  static auto *getActiveSpace = reinterpret_cast<uint64_t (*)(int)>(dlsym(handle, "CGSGetActiveSpace"));
  return mainConnection && getActiveSpace ? getActiveSpace(mainConnection()) : 0;
}

long dockWindowCount()
{
  const CFArrayRef windows = CGWindowListCopyWindowInfo(kCGWindowListOptionOnScreenOnly, kCGNullWindowID);
  long count = 0;
  for (CFIndex i = 0; i < CFArrayGetCount(windows); ++i) {
    const auto window = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(windows, i));
    const auto owner = static_cast<CFStringRef>(CFDictionaryGetValue(window, kCGWindowOwnerName));
    if (owner != nullptr && CFStringCompare(owner, CFSTR("Dock"), 0) == kCFCompareEqualTo) {
      ++count;
    }
  }
  CFRelease(windows);
  return count;
}

void report(CFRunLoopTimerRef, void *info)
{
  const auto *before = static_cast<const uint64_t *>(info);
  std::printf(
      "         result: space %llu -> %llu, dock windows now %ld\n", static_cast<unsigned long long>(before[0]),
      static_cast<unsigned long long>(activeSpace()), dockWindowCount()
  );
  std::fflush(stdout);
  delete[] before;
}

CGEventRef onEvent(CGEventTapProxy, CGEventType type, CGEventRef event, void *)
{
  if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
    CGEventTapEnable(g_tap, true);
    return event;
  }
  ++g_callbacks;
  if (!deskflow::osx::isDockGestureEvent(event)) {
    return event;
  }
  if (CGEventGetIntegerValueField(event, kFieldCGSEventType) == kDockControl) {
    const auto phase = CGEventGetIntegerValueField(event, kFieldPhase);
    ++g_dockEvents[phase >= 0 && phase < 9 ? phase : 0];
  } else {
    ++g_companionEvents;
  }
  if (g_passthrough > 0) {
    --g_passthrough;
    return event;
  }

  const bool isEnded = CGEventGetIntegerValueField(event, kFieldCGSEventType) == kDockControl &&
                       CGEventGetIntegerValueField(event, kFieldPhase) == kEnded;

  if (const auto direction = g_detector.feed(event)) {
    auto *before = new uint64_t[1]{activeSpace()};
    std::printf("[%d] real swipe decoded as %s -> replaying\n", ++g_replayed, swipeDirectionName(*direction));
    std::fflush(stdout);
    g_passthrough += kSyntheticEventsPerSwipe;
    if (!deskflow::osx::postDockSwipe(*direction)) {
      std::printf("         FAILED to post synthetic swipe\n");
      g_passthrough -= kSyntheticEventsPerSwipe;
    }
    CFRunLoopTimerContext context = {0, before, nullptr, nullptr, nullptr};
    const auto timer = CFRunLoopTimerCreate(nullptr, CFAbsoluteTimeGetCurrent() + 1.5, 0, 0, 0, report, &context);
    CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopCommonModes);
    CFRelease(timer);
  }

  if (isEnded) {
    // let the Dock close its native gesture state without acting on it
    CGEventSetDoubleValueField(event, kFieldVelocityX, 0);
    CGEventSetDoubleValueField(event, kFieldVelocityY, 0);
    CGEventSetDoubleValueField(event, kFieldProgress, 0);
    return event;
  }
  return nullptr;
}

void stop(CFRunLoopTimerRef, void *)
{
  CFRunLoopStop(CFRunLoopGetMain());
}

} // namespace

int main(int argc, char **argv)
{
  const double seconds = argc > 1 ? std::atof(argv[1]) : 45.0;
  if (!deskflow::osx::isDockSwipeSupported()) {
    std::fprintf(stderr, "requires macOS 27 or later\n");
    return 2;
  }
  if (!AXIsProcessTrusted()) {
    std::fprintf(stderr, "grant Accessibility permission and re-run\n");
    return 2;
  }

  const CGEventMask mask = kCGEventMaskForAllEvents;
  g_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault, mask, onEvent, nullptr);
  if (g_tap == nullptr) {
    std::fprintf(stderr, "failed to create event tap\n");
    return 1;
  }

  const auto source = CFMachPortCreateRunLoopSource(nullptr, g_tap, 0);
  CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopCommonModes);
  CGEventTapEnable(g_tap, true);
  const auto timer = CFRunLoopTimerCreate(nullptr, CFAbsoluteTimeGetCurrent() + seconds, 0, 0, 0, stop, nullptr);
  CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopCommonModes);

  std::printf(
      "replaying real swipes for %.0fs (start: space %llu, dock windows %ld)\n", seconds,
      static_cast<unsigned long long>(activeSpace()), dockWindowCount()
  );
  std::fflush(stdout);
  CFRunLoopRun();

  std::printf("done: %d swipe(s) replayed\n", g_replayed);
  std::printf(
      "tap saw %d events: dock began=%d changed=%d ended=%d cancelled=%d, companion=%d\n", g_callbacks, g_dockEvents[1],
      g_dockEvents[2], g_dockEvents[4], g_dockEvents[8], g_companionEvents
  );
  CFRelease(timer);
  CFRelease(source);
  CFRelease(g_tap);
  return 0;
}
