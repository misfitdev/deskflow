/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXSwipeTests.h"

#include "platform/OSXSwipe.h"

#include <QTest>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <optional>
#include <vector>

using deskflow::osx::DockSwipeDetector;
using deskflow::osx::DockSwipeFormat;
using deskflow::osx::ScopedCGEvent;

namespace {

// Field layout of DockControl events as observed from a real trackpad on
// macOS 27. Kept separate from OSXSwipe.cpp so the tests describe the format
// independently of the implementation under test.
const auto kFieldCGSEventType = static_cast<CGEventField>(55);
const auto kFieldHIDType = static_cast<CGEventField>(110);
const auto kFieldMotion = static_cast<CGEventField>(123);
const auto kFieldProgress = static_cast<CGEventField>(124);
const auto kFieldVelocityX = static_cast<CGEventField>(129);
const auto kFieldVelocityY = static_cast<CGEventField>(130);
const auto kFieldPhase = static_cast<CGEventField>(132);
const auto kFieldFlagBits = static_cast<CGEventField>(135);

const int64_t kDockControl = 30;
const int64_t kGesture = 29;
const int64_t kDockSwipe = 23;
const int64_t kHorizontal = 1;
const int64_t kVertical = 2;

const int64_t kBegan = 1;
const int64_t kChanged = 2;
const int64_t kEnded = 4;
const int64_t kCancelled = 8;

ScopedCGEvent dockEvent(int64_t motion, int64_t phase, double progress, double velocity = 0.0)
{
  ScopedCGEvent event(CGEventCreate(nullptr));
  CGEventSetIntegerValueField(event.get(), kFieldCGSEventType, kDockControl);
  CGEventSetIntegerValueField(event.get(), kFieldHIDType, kDockSwipe);
  CGEventSetIntegerValueField(event.get(), kFieldMotion, motion);
  CGEventSetIntegerValueField(event.get(), kFieldPhase, phase);
  CGEventSetDoubleValueField(event.get(), kFieldProgress, progress);
  // real ended events report the same velocity on both axes
  CGEventSetDoubleValueField(event.get(), kFieldVelocityX, velocity);
  CGEventSetDoubleValueField(event.get(), kFieldVelocityY, velocity);
  return event;
}

ScopedCGEvent companionEvent()
{
  ScopedCGEvent event(CGEventCreate(nullptr));
  CGEventSetIntegerValueField(event.get(), kFieldCGSEventType, kGesture);
  return event;
}

std::vector<SwipeDirection> feedAll(DockSwipeDetector &detector, const std::vector<ScopedCGEvent> &events)
{
  std::vector<SwipeDirection> fired;
  for (const auto &event : events) {
    if (const auto direction = detector.feed(event.get())) {
      fired.push_back(*direction);
    }
  }
  return fired;
}

// A swipe shaped like a real one: began, a run of changed events with growing
// progress, then ended with a velocity of the same sign.
std::vector<ScopedCGEvent> realisticSwipe(int64_t motion, double sign)
{
  std::vector<ScopedCGEvent> events;
  events.push_back(dockEvent(motion, kBegan, sign * 0.0116));
  for (const double progress : {0.0116, 0.1113, 0.3207, 0.6696, 0.9831}) {
    events.push_back(dockEvent(motion, kChanged, sign * progress));
    events.push_back(companionEvent());
  }
  events.push_back(dockEvent(motion, kEnded, sign * 1.1168, sign * 5.7));
  return events;
}

const SwipeDirection kAllDirections[] = {
    SwipeDirection::Left, SwipeDirection::Right, SwipeDirection::Up, SwipeDirection::Down
};

bool isHorizontal(SwipeDirection direction)
{
  return direction == SwipeDirection::Left || direction == SwipeDirection::Right;
}

// Velocity on the swipe's own axis, carried by the ended dock event.
double endVelocity(SwipeDirection direction, DockSwipeFormat format)
{
  const auto events = deskflow::osx::createDockSwipeEvents(direction, format);
  return CGEventGetDoubleValueField(events.at(4).get(), isHorizontal(direction) ? kFieldVelocityX : kFieldVelocityY);
}

// Whether the serialized event carries the raw IOHID payload field: a 2 byte
// length (68 for a began swipe, which has no velocity record) followed by the
// field tag 4205.
bool hasBeganPayload(CGEventRef event)
{
  const std::unique_ptr<const __CFData, deskflow::osx::CFReleaser> data(CGEventCreateData(nullptr, event));
  const auto *bytes = CFDataGetBytePtr(data.get());
  const auto *end = bytes + CFDataGetLength(data.get());
  const uint8_t marker[] = {0x00, 0x44, 0x10, 0x6D};
  return std::search(bytes, end, std::begin(marker), std::end(marker)) != end;
}

} // namespace

void OSXSwipeTests::detector_horizontalProgress_firesOnce()
{
  DockSwipeDetector detector;
  QVERIFY(feedAll(detector, realisticSwipe(kHorizontal, -1.0)) == std::vector{SwipeDirection::Right});
  QVERIFY(feedAll(detector, realisticSwipe(kHorizontal, 1.0)) == std::vector{SwipeDirection::Left});
}

void OSXSwipeTests::detector_verticalProgress_firesOnce()
{
  DockSwipeDetector detector;
  QVERIFY(feedAll(detector, realisticSwipe(kVertical, 1.0)) == std::vector{SwipeDirection::Up});
  QVERIFY(feedAll(detector, realisticSwipe(kVertical, -1.0)) == std::vector{SwipeDirection::Down});
}

void OSXSwipeTests::detector_flickWithoutProgress_usesEndVelocity()
{
  DockSwipeDetector detector;
  std::vector<ScopedCGEvent> events;
  events.push_back(dockEvent(kHorizontal, kBegan, 0.0));
  events.push_back(dockEvent(kHorizontal, kEnded, 0.0, -4.3));
  QVERIFY(feedAll(detector, events) == std::vector{SwipeDirection::Right});
}

void OSXSwipeTests::detector_zeroProgress_waitsForMovement()
{
  DockSwipeDetector detector;
  QVERIFY(!detector.feed(dockEvent(kVertical, kBegan, 0.0).get()));
  QVERIFY(!detector.feed(dockEvent(kVertical, kChanged, 0.0).get()));
  QCOMPARE(detector.feed(dockEvent(kVertical, kChanged, 0.05).get()), std::optional{SwipeDirection::Up});
  QVERIFY(!detector.feed(dockEvent(kVertical, kEnded, 0.8, 5.2).get()));
}

void OSXSwipeTests::detector_cancelled_doesNotFire()
{
  DockSwipeDetector detector;
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kBegan, 0.0).get()));
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kCancelled, 0.0).get()));
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kEnded, 0.0, -5.0).get()));
}

void OSXSwipeTests::detector_changedWithoutBegan_isIgnored()
{
  // a swipe that began while the cursor was on the primary screen
  DockSwipeDetector detector;
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kChanged, -0.5).get()));
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kEnded, -1.0, -5.0).get()));
}

void OSXSwipeTests::detector_nonDockEvents_areIgnored()
{
  DockSwipeDetector detector;
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kBegan, 0.0).get()));
  QVERIFY(!detector.feed(companionEvent().get()));

  const ScopedCGEvent mouse(CGEventCreateMouseEvent(nullptr, kCGEventMouseMoved, CGPointZero, kCGMouseButtonLeft));
  QVERIFY(!detector.feed(mouse.get()));
  QVERIFY(!deskflow::osx::isDockGestureEvent(mouse.get()));
  QVERIFY(deskflow::osx::isDockGestureEvent(companionEvent().get()));

  // a dock event with an unknown motion never fires
  QVERIFY(!detector.feed(dockEvent(7, kChanged, -0.5).get()));
}

void OSXSwipeTests::detector_reset_forgetsSwipeInProgress()
{
  DockSwipeDetector detector;
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kBegan, 0.0).get()));
  detector.reset();
  QVERIFY(!detector.feed(dockEvent(kHorizontal, kChanged, -0.5).get()));
}

void OSXSwipeTests::createDockSwipeEvents_pairsDockAndCompanionEvents()
{
  const auto events = deskflow::osx::createDockSwipeEvents(SwipeDirection::Right, DockSwipeFormat::IOHIDPayload);
  QCOMPARE(events.size(), static_cast<size_t>(6));

  const int64_t phases[] = {kBegan, kChanged, kEnded};
  for (size_t i = 0; i < 3; ++i) {
    CGEventRef dock = events[i * 2].get();
    CGEventRef companion = events[i * 2 + 1].get();
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldCGSEventType), kDockControl);
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldHIDType), kDockSwipe);
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldMotion), kHorizontal);
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldPhase), phases[i]);
    QCOMPARE(CGEventGetIntegerValueField(companion, kFieldCGSEventType), kGesture);
  }

  // only the ended event carries the velocity that completes the swipe
  QCOMPARE(CGEventGetDoubleValueField(events[0].get(), kFieldVelocityX), 0.0);
  QVERIFY(CGEventGetDoubleValueField(events[4].get(), kFieldVelocityX) < 0.0);
}

void OSXSwipeTests::createDockSwipeEvents_roundTripsThroughDetector()
{
  for (const auto direction : {SwipeDirection::Left, SwipeDirection::Right, SwipeDirection::Up, SwipeDirection::Down}) {
    DockSwipeDetector detector;
    const auto events = deskflow::osx::createDockSwipeEvents(direction, DockSwipeFormat::IOHIDPayload);
    QVERIFY(!events.empty());
    QVERIFY2(feedAll(detector, events) == std::vector{direction}, swipeDirectionName(direction));
  }
}

void OSXSwipeTests::createDockSwipeEvents_legacyMatchesIssHorizontal()
{
  // iss replays "next Space" before macOS 27 as a positive flag and velocity
  const auto events = deskflow::osx::createDockSwipeEvents(SwipeDirection::Right, DockSwipeFormat::Legacy);
  QCOMPARE(events.size(), static_cast<size_t>(6));

  const float positive = FLT_TRUE_MIN;
  int32_t positiveBits = 0;
  std::memcpy(&positiveBits, &positive, sizeof(positiveBits));

  const int64_t phases[] = {kBegan, kChanged, kEnded};
  for (size_t i = 0; i < 3; ++i) {
    CGEventRef dock = events[i * 2].get();
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldCGSEventType), kDockControl);
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldHIDType), kDockSwipe);
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldMotion), kHorizontal);
    QCOMPARE(CGEventGetIntegerValueField(dock, kFieldPhase), phases[i]);
    QCOMPARE(static_cast<int32_t>(CGEventGetIntegerValueField(dock, kFieldFlagBits)), positiveBits);
    QCOMPARE(CGEventGetIntegerValueField(events[i * 2 + 1].get(), kFieldCGSEventType), kGesture);
  }
  QCOMPARE(CGEventGetDoubleValueField(events[4].get(), kFieldVelocityX), 400.0);
}

void OSXSwipeTests::createDockSwipeEvents_legacyFlipsSignOfMacOS27()
{
  for (const auto direction : kAllDirections) {
    const double legacy = endVelocity(direction, DockSwipeFormat::Legacy);
    const double current = endVelocity(direction, DockSwipeFormat::IOHIDPayload);
    QVERIFY2(legacy != 0.0 && current != 0.0, swipeDirectionName(direction));
    QVERIFY2((legacy > 0.0) != (current > 0.0), swipeDirectionName(direction));
  }
}

void OSXSwipeTests::createDockSwipeEvents_onlyMacOS27CarriesPayload()
{
  const auto legacy = deskflow::osx::createDockSwipeEvents(SwipeDirection::Up, DockSwipeFormat::Legacy);
  const auto current = deskflow::osx::createDockSwipeEvents(SwipeDirection::Up, DockSwipeFormat::IOHIDPayload);
  QVERIFY(hasBeganPayload(current[0].get()));
  QVERIFY(!hasBeganPayload(legacy[0].get()));
}

QTEST_MAIN(OSXSwipeTests)
