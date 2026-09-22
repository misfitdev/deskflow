/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>

class OSXSwipeTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void detector_horizontalProgress_firesOnce();
  void detector_verticalProgress_firesOnce();
  void detector_flickWithoutProgress_usesEndVelocity();
  void detector_zeroProgress_waitsForMovement();
  void detector_cancelled_doesNotFire();
  void detector_changedWithoutBegan_isIgnored();
  void detector_nonDockEvents_areIgnored();
  void detector_reset_forgetsSwipeInProgress();
  void createDockSwipeEvents_pairsDockAndCompanionEvents();
  void createDockSwipeEvents_roundTripsThroughDetector();
};
