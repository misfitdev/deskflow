/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXSwipe.h"

#include <mach/mach_time.h>
#include <sys/sysctl.h>

#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace deskflow::osx {

namespace {

// Undocumented WindowServer event fields and values. Derived from
// joshuarli/iss (0BSD). The macOS 27 layout is verified against real trackpad
// swipes; the legacy layout is iss's pre-27 path. See
// docs/dev/macos-gestures/README.md. Expect these to need re-deriving when a
// future macOS release changes the dock swipe event format.
const CGEventField kFieldCGSEventType = static_cast<CGEventField>(55);
const CGEventField kFieldGestureHIDType = static_cast<CGEventField>(110);
const CGEventField kFieldScrollY = static_cast<CGEventField>(119);
const CGEventField kFieldSwipeMotion = static_cast<CGEventField>(123);
const CGEventField kFieldSwipeProgress = static_cast<CGEventField>(124);
const CGEventField kFieldSwipePositionX = static_cast<CGEventField>(125);
const CGEventField kFieldSwipePositionY = static_cast<CGEventField>(126);
const CGEventField kFieldSwipeVelocityX = static_cast<CGEventField>(129);
const CGEventField kFieldSwipeVelocityY = static_cast<CGEventField>(130);
const CGEventField kFieldGesturePhase = static_cast<CGEventField>(132);
const CGEventField kFieldGesturePhaseAlias = static_cast<CGEventField>(134);
const CGEventField kFieldGestureFlagBits = static_cast<CGEventField>(135);
const CGEventField kFieldZoomDeltaY = static_cast<CGEventField>(138);
const CGEventField kFieldZoomDeltaX = static_cast<CGEventField>(139);
const CGEventField kFieldSourceTimestamp = static_cast<CGEventField>(169);
const CGEventField kFieldRawIOHIDPayload = static_cast<CGEventField>(4205);

const int64_t kCGSEventGesture = 29;
const int64_t kCGSEventDockControl = 30;
const int64_t kHIDEventTypeDockSwipe = 23;

const int64_t kMotionHorizontal = 1;
const int64_t kMotionVertical = 2;

const int64_t kPhaseBegan = 1;
const int64_t kPhaseChanged = 2;
const int64_t kPhaseEnded = 4;
const int64_t kPhaseCancelled = 8;

// Large enough that the Dock completes the swipe instead of snapping back.
const double kSyntheticEndVelocity = 9999.0;
const double kLegacySyntheticEndVelocity = 400.0;

// macOS 27 rejects synthetic dock swipes unless each carries a serialized raw
// IOHID queue element in field 4205. Layouts must match the OS byte for byte.
#pragma pack(push, 1)
struct IOHIDEventBase
{
  uint32_t size;
  uint32_t type;
  uint32_t options;
  uint8_t depth;
  uint8_t reserved[3];
};

struct IOHIDFluidTouchGestureData
{
  IOHIDEventBase base;
  int32_t positionX;
  int32_t positionY;
  int32_t positionZ;
  uint32_t swipeMask;
  uint16_t gestureMotion;
  uint16_t gestureFlavor;
  int32_t swipeProgress;
};

struct IOHIDVelocityEventData
{
  IOHIDEventBase base;
  int32_t velocityX;
  int32_t velocityY;
  int32_t velocityZ;
};

struct IOHIDSystemQueueElementHeader
{
  uint64_t timestamp;
  uint64_t senderId;
  uint32_t options;
  uint32_t attributeLength;
  uint32_t eventCount;
};
#pragma pack(pop)

static_assert(sizeof(IOHIDEventBase) == 16, "unexpected IOHID event base layout");
static_assert(sizeof(IOHIDFluidTouchGestureData) == 40, "unexpected IOHID fluid gesture layout");
static_assert(sizeof(IOHIDVelocityEventData) == 28, "unexpected IOHID velocity layout");
static_assert(sizeof(IOHIDSystemQueueElementHeader) == 28, "unexpected IOHID queue header layout");

const uint32_t kIOHIDEventTypeVelocity = 9;
const uint32_t kIOHIDEventTypeFluidTouchGesture = 23;
const uint16_t kIOHIDGestureFlavorDockPrimary = 3;

// Serialized CGEvents start with this version tag; anything else is a format
// this code does not know how to extend.
const uint8_t kSerializedEventHeader[] = {0, 0, 0, 2};

int32_t toFixed1616(double value)
{
  auto fixed = static_cast<int32_t>(value * 65536.0);
  if (fixed == 0 && value != 0.0) {
    return value > 0.0 ? 1 : -1;
  }
  return fixed;
}

int macOSMajorVersion()
{
  char version[32] = {};
  size_t size = sizeof(version);
  if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) != 0) {
    return 0;
  }
  return std::atoi(version);
}

bool isHorizontal(SwipeDirection direction)
{
  return direction == SwipeDirection::Left || direction == SwipeDirection::Right;
}

// Sign of progress and velocity for a direction. The same convention decodes
// real swipes and encodes synthetic ones, so a captured swipe replays as the
// same navigation.
double directionSign(SwipeDirection direction)
{
  switch (direction) {
  case SwipeDirection::Right:
  case SwipeDirection::Down:
    return -1.0;
  case SwipeDirection::Left:
  case SwipeDirection::Up:
    return 1.0;
  }
  return 0.0;
}

std::optional<SwipeDirection> directionFromSign(int64_t motion, double value)
{
  if (value == 0.0) {
    return std::nullopt;
  }
  if (motion == kMotionHorizontal) {
    return value < 0.0 ? SwipeDirection::Right : SwipeDirection::Left;
  }
  if (motion == kMotionVertical) {
    return value > 0.0 ? SwipeDirection::Up : SwipeDirection::Down;
  }
  return std::nullopt;
}

std::vector<uint8_t> createIOHIDPayload(CGEventRef event)
{
  const int64_t phase = CGEventGetIntegerValueField(event, kFieldGesturePhase);
  const double velocityX = CGEventGetDoubleValueField(event, kFieldSwipeVelocityX);
  const double velocityY = CGEventGetDoubleValueField(event, kFieldSwipeVelocityY);
  const bool includeVelocity = velocityX != 0.0 || velocityY != 0.0 || phase == kPhaseEnded;

  size_t length = sizeof(IOHIDSystemQueueElementHeader) + sizeof(IOHIDFluidTouchGestureData);
  if (includeVelocity) {
    length += sizeof(IOHIDVelocityEventData);
  }
  std::vector<uint8_t> payload(length, 0);

  IOHIDSystemQueueElementHeader header = {};
  const uint64_t timestamp = CGEventGetTimestamp(event);
  header.timestamp = timestamp != 0 ? timestamp : mach_absolute_time();
  header.eventCount = includeVelocity ? 2 : 1;
  std::memcpy(payload.data(), &header, sizeof(header));

  IOHIDFluidTouchGestureData fluid = {};
  fluid.base.size = sizeof(IOHIDFluidTouchGestureData);
  fluid.base.type = kIOHIDEventTypeFluidTouchGesture;
  fluid.base.options = static_cast<uint32_t>((phase & 0xFF) << 24);
  fluid.positionX = toFixed1616(CGEventGetDoubleValueField(event, kFieldSwipePositionX));
  fluid.positionY = toFixed1616(CGEventGetDoubleValueField(event, kFieldSwipePositionY));
  fluid.gestureMotion = static_cast<uint16_t>(CGEventGetIntegerValueField(event, kFieldSwipeMotion));
  fluid.gestureFlavor = kIOHIDGestureFlavorDockPrimary;
  fluid.swipeProgress = toFixed1616(CGEventGetDoubleValueField(event, kFieldSwipeProgress));
  std::memcpy(payload.data() + sizeof(header), &fluid, sizeof(fluid));

  if (includeVelocity) {
    IOHIDVelocityEventData velocity = {};
    velocity.base.size = sizeof(IOHIDVelocityEventData);
    velocity.base.type = kIOHIDEventTypeVelocity;
    velocity.base.depth = 1;
    velocity.velocityX = toFixed1616(velocityX);
    velocity.velocityY = toFixed1616(velocityY);
    std::memcpy(payload.data() + sizeof(header) + sizeof(fluid), &velocity, sizeof(velocity));
  }

  return payload;
}

// Returns a copy of \c event with the raw IOHID payload appended to its
// serialized form, which is how macOS 27 expects synthetic dock swipes.
ScopedCGEvent attachIOHIDPayload(CGEventRef event)
{
  const std::unique_ptr<const __CFData, CFReleaser> data(CGEventCreateData(kCFAllocatorDefault, event));
  if (!data) {
    return nullptr;
  }

  const UInt8 *bytes = CFDataGetBytePtr(data.get());
  const auto length = static_cast<size_t>(CFDataGetLength(data.get()));
  if (length < sizeof(kSerializedEventHeader) ||
      std::memcmp(bytes, kSerializedEventHeader, sizeof(kSerializedEventHeader)) != 0) {
    return nullptr;
  }

  const auto payload = createIOHIDPayload(event);
  std::vector<uint8_t> augmented(bytes, bytes + length);
  augmented.push_back(static_cast<uint8_t>(payload.size() >> 8));
  augmented.push_back(static_cast<uint8_t>(payload.size()));
  augmented.push_back(static_cast<uint8_t>(kFieldRawIOHIDPayload >> 8));
  augmented.push_back(static_cast<uint8_t>(kFieldRawIOHIDPayload));
  augmented.insert(augmented.end(), payload.begin(), payload.end());

  const std::unique_ptr<const __CFData, CFReleaser> augmentedData(
      CFDataCreate(kCFAllocatorDefault, augmented.data(), static_cast<CFIndex>(augmented.size()))
  );
  if (!augmentedData) {
    return nullptr;
  }
  return ScopedCGEvent(CGEventCreateFromData(kCFAllocatorDefault, augmentedData.get()));
}

// Legacy swipes use the opposite sign to macOS 27 for the same navigation:
// iss replays "next Space" with a positive sign before 27 and a negative one
// on 27. Only the horizontal case is known; vertical assumes the same flip.
double legacyDirectionSign(SwipeDirection direction)
{
  return -directionSign(direction);
}

ScopedCGEvent createLegacyDockEvent(SwipeDirection direction, int64_t phase)
{
  ScopedCGEvent event(CGEventCreate(nullptr));
  if (!event) {
    return nullptr;
  }

  const bool horizontal = isHorizontal(direction);
  const double sign = legacyDirectionSign(direction);

  // the Dock reads direction from the bit pattern of the smallest float with
  // that sign; this also makes the switch instant
  const float flags = sign > 0.0 ? FLT_TRUE_MIN : -FLT_TRUE_MIN;
  int32_t flagBits = 0;
  std::memcpy(&flagBits, &flags, sizeof(flagBits));

  CGEventSetIntegerValueField(event.get(), kFieldCGSEventType, kCGSEventDockControl);
  CGEventSetIntegerValueField(event.get(), kFieldGestureHIDType, kHIDEventTypeDockSwipe);
  CGEventSetIntegerValueField(event.get(), kFieldGesturePhase, phase);
  CGEventSetIntegerValueField(event.get(), kFieldGestureFlagBits, flagBits);
  CGEventSetIntegerValueField(event.get(), kFieldSwipeMotion, horizontal ? kMotionHorizontal : kMotionVertical);
  CGEventSetDoubleValueField(event.get(), kFieldScrollY, 0);
  CGEventSetDoubleValueField(event.get(), kFieldZoomDeltaX, FLT_TRUE_MIN);
  if (phase == kPhaseEnded) {
    CGEventSetDoubleValueField(
        event.get(), horizontal ? kFieldSwipeVelocityX : kFieldSwipeVelocityY, sign * kLegacySyntheticEndVelocity
    );
  }

  return event;
}

ScopedCGEvent createPayloadDockEvent(SwipeDirection direction, int64_t phase)
{
  ScopedCGEvent event(CGEventCreate(nullptr));
  if (!event) {
    return nullptr;
  }

  const bool horizontal = isHorizontal(direction);
  const double sign = directionSign(direction);

  CGEventSetIntegerValueField(event.get(), kFieldCGSEventType, kCGSEventDockControl);
  CGEventSetIntegerValueField(event.get(), kFieldGestureHIDType, kHIDEventTypeDockSwipe);
  CGEventSetIntegerValueField(event.get(), kFieldGesturePhase, phase);
  CGEventSetIntegerValueField(event.get(), kFieldGesturePhaseAlias, phase);
  CGEventSetIntegerValueField(event.get(), kFieldSwipeMotion, horizontal ? kMotionHorizontal : kMotionVertical);
  CGEventSetDoubleValueField(event.get(), kFieldSwipeProgress, sign);
  CGEventSetDoubleValueField(event.get(), kFieldZoomDeltaY, 3.0);
  CGEventSetDoubleValueField(event.get(), kFieldSourceTimestamp, static_cast<double>(mach_absolute_time()));
  CGEventSetDoubleValueField(event.get(), horizontal ? kFieldSwipePositionX : kFieldSwipePositionY, 0.1);
  if (phase == kPhaseEnded) {
    CGEventSetDoubleValueField(
        event.get(), horizontal ? kFieldSwipeVelocityX : kFieldSwipeVelocityY, sign * kSyntheticEndVelocity
    );
  }

  return attachIOHIDPayload(event.get());
}

ScopedCGEvent createCompanionEvent()
{
  ScopedCGEvent event(CGEventCreate(nullptr));
  if (event) {
    CGEventSetIntegerValueField(event.get(), kFieldCGSEventType, kCGSEventGesture);
  }
  return event;
}

} // namespace

DockSwipeFormat currentDockSwipeFormat()
{
  static const auto format = macOSMajorVersion() >= 27 ? DockSwipeFormat::IOHIDPayload : DockSwipeFormat::Legacy;
  return format;
}

bool canCaptureDockSwipes()
{
  return currentDockSwipeFormat() == DockSwipeFormat::IOHIDPayload;
}

bool isDockGestureEvent(CGEventRef event)
{
  const int64_t type = CGEventGetIntegerValueField(event, kFieldCGSEventType);
  return type == kCGSEventDockControl || type == kCGSEventGesture;
}

std::optional<SwipeDirection> DockSwipeDetector::feed(CGEventRef event)
{
  if (CGEventGetIntegerValueField(event, kFieldCGSEventType) != kCGSEventDockControl ||
      CGEventGetIntegerValueField(event, kFieldGestureHIDType) != kHIDEventTypeDockSwipe) {
    return std::nullopt;
  }

  const int64_t motion = CGEventGetIntegerValueField(event, kFieldSwipeMotion);
  const int64_t phase = CGEventGetIntegerValueField(event, kFieldGesturePhase);

  if (phase == kPhaseBegan) {
    m_tracking = true;
    m_fired = false;
    return std::nullopt;
  }

  if (!m_tracking) {
    return std::nullopt;
  }

  if (phase == kPhaseChanged) {
    if (m_fired) {
      return std::nullopt;
    }
    const auto direction = directionFromSign(motion, CGEventGetDoubleValueField(event, kFieldSwipeProgress));
    m_fired = direction.has_value();
    return direction;
  }

  if (phase == kPhaseEnded) {
    std::optional<SwipeDirection> direction;
    if (!m_fired) {
      // a quick flick can end before any progress is reported
      const auto velocityField = motion == kMotionHorizontal ? kFieldSwipeVelocityX : kFieldSwipeVelocityY;
      direction = directionFromSign(motion, CGEventGetDoubleValueField(event, velocityField));
    }
    reset();
    return direction;
  }

  if (phase == kPhaseCancelled) {
    reset();
  }
  return std::nullopt;
}

void DockSwipeDetector::reset()
{
  m_tracking = false;
  m_fired = false;
}

std::vector<ScopedCGEvent> createDockSwipeEvents(SwipeDirection direction, DockSwipeFormat format)
{
  std::vector<ScopedCGEvent> events;
  for (const int64_t phase : {kPhaseBegan, kPhaseChanged, kPhaseEnded}) {
    auto dock = format == DockSwipeFormat::IOHIDPayload ? createPayloadDockEvent(direction, phase)
                                                        : createLegacyDockEvent(direction, phase);
    auto companion = createCompanionEvent();
    if (!dock || !companion) {
      return {};
    }
    events.push_back(std::move(dock));
    events.push_back(std::move(companion));
  }
  return events;
}

bool postDockSwipe(SwipeDirection direction)
{
  const auto events = createDockSwipeEvents(direction, currentDockSwipeFormat());
  if (events.empty()) {
    return false;
  }
  for (const auto &event : events) {
    CGEventPost(kCGSessionEventTap, event.get());
  }
  return true;
}

} // namespace deskflow::osx
