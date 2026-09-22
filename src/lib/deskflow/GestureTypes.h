/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstdint>
#include <optional>

//! Direction of a multi-finger trackpad swipe
/*!
Named for the navigation the swipe triggers on the receiving screen, not the
direction the fingers moved: \c Right moves to the next Space, \c Left to the
previous one, \c Up opens Mission Control and \c Down opens App Expose (or
closes Mission Control). Values are sent on the wire and must not change.
*/
enum class SwipeDirection : uint8_t
{
  Left = 1,
  Right = 2,
  Up = 3,
  Down = 4
};

//! Convert a wire value to a swipe direction, rejecting unknown values
inline std::optional<SwipeDirection> swipeDirectionFromWire(uint8_t value)
{
  switch (value) {
  case static_cast<uint8_t>(SwipeDirection::Left):
    return SwipeDirection::Left;
  case static_cast<uint8_t>(SwipeDirection::Right):
    return SwipeDirection::Right;
  case static_cast<uint8_t>(SwipeDirection::Up):
    return SwipeDirection::Up;
  case static_cast<uint8_t>(SwipeDirection::Down):
    return SwipeDirection::Down;
  default:
    return std::nullopt;
  }
}

//! Human readable name, for logging
inline const char *swipeDirectionName(SwipeDirection direction)
{
  switch (direction) {
  case SwipeDirection::Left:
    return "left";
  case SwipeDirection::Right:
    return "right";
  case SwipeDirection::Up:
    return "up";
  case SwipeDirection::Down:
    return "down";
  }
  return "unknown";
}
