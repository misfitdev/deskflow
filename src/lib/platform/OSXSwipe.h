/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/GestureTypes.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

//! Trackpad dock swipe capture and synthesis
/*!
Multi-finger swipes that switch Spaces and open Mission Control are delivered
as private WindowServer "DockControl" events, not public gesture events. This
namespace recognizes them on the primary screen and synthesizes them on a
secondary screen. Everything here relies on undocumented event fields; see
docs/dev/macos-gestures/README.md for how they were derived and verified.
*/
namespace deskflow::osx {

struct CFReleaser
{
  void operator()(CFTypeRef ref) const
  {
    if (ref != nullptr) {
      CFRelease(ref);
    }
  }
};

using ScopedCGEvent = std::unique_ptr<std::remove_pointer_t<CGEventRef>, CFReleaser>;

//! Whether this macOS release uses the swipe event format implemented here
/*!
Only macOS 27 and later is supported: earlier releases encode swipe direction
differently and need a different synthetic event layout.
*/
bool isDockSwipeSupported();

//! Whether \c event is a DockControl or companion gesture event
bool isDockGestureEvent(CGEventRef event);

//! Recognizes completed dock swipes in a stream of tapped events
class DockSwipeDetector
{
public:
  //! Feed one tapped event
  /*!
  Returns the swipe direction once per swipe, as soon as it can be
  determined, and \c std::nullopt for every other event.
  */
  std::optional<SwipeDirection> feed(CGEventRef event);

  //! Forget any swipe in progress
  void reset();

private:
  bool m_tracking = false;
  bool m_fired = false;
};

//! Build the synthetic event sequence for a swipe
/*!
Returns DockControl and companion events in posting order, or an empty
vector if any event could not be created.
*/
std::vector<ScopedCGEvent> createDockSwipeEvents(SwipeDirection direction);

//! Post a synthetic swipe to the current session
/*!
Returns false if the events could not be created.
*/
bool postDockSwipe(SwipeDirection direction);

} // namespace deskflow::osx
