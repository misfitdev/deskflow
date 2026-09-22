/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QObject>

class GestureSwipeProtocolTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  void directionValues_areStable();
  void swipeDirectionFromWire_acceptsKnownValues();
  void swipeDirectionFromWire_rejectsUnknownValues();
  void writef_encodesOneByteDirection();
  void readf_decodesWhatWritefEncoded();

private:
  Log m_log;
};
