/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "GestureSwipeProtocolTests.h"

#include "deskflow/GestureTypes.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <QTest>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

// Loops writes back to reads, so encoded messages can be decoded again.
class LoopbackStream : public deskflow::IStream
{
public:
  const std::string &bytes() const
  {
    return m_bytes;
  }

  void close() override
  {
    m_bytes.clear();
  }

  uint32_t read(void *buffer, uint32_t size) override
  {
    const auto count = std::min(static_cast<size_t>(size), m_bytes.size());
    if (buffer != nullptr) {
      std::memcpy(buffer, m_bytes.data(), count);
    }
    m_bytes.erase(0, count);
    return static_cast<uint32_t>(count);
  }

  void write(const void *buffer, uint32_t size) override
  {
    m_bytes.append(static_cast<const char *>(buffer), size);
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<LoopbackStream *>(this);
  }

  bool isReady() const override
  {
    return !m_bytes.empty();
  }

  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_bytes.size());
  }

private:
  std::string m_bytes;
};

const SwipeDirection kAllDirections[] = {
    SwipeDirection::Left, SwipeDirection::Right, SwipeDirection::Up, SwipeDirection::Down
};

} // namespace

void GestureSwipeProtocolTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Debug);
}

void GestureSwipeProtocolTests::directionValues_areStable()
{
  // these values are on the wire; changing them breaks older peers
  QCOMPARE(static_cast<uint8_t>(SwipeDirection::Left), static_cast<uint8_t>(1));
  QCOMPARE(static_cast<uint8_t>(SwipeDirection::Right), static_cast<uint8_t>(2));
  QCOMPARE(static_cast<uint8_t>(SwipeDirection::Up), static_cast<uint8_t>(3));
  QCOMPARE(static_cast<uint8_t>(SwipeDirection::Down), static_cast<uint8_t>(4));
}

void GestureSwipeProtocolTests::swipeDirectionFromWire_acceptsKnownValues()
{
  for (const auto direction : kAllDirections) {
    QCOMPARE(swipeDirectionFromWire(static_cast<uint8_t>(direction)), std::optional{direction});
  }
}

void GestureSwipeProtocolTests::swipeDirectionFromWire_rejectsUnknownValues()
{
  for (const uint8_t value : {0, 5, 127, 255}) {
    QVERIFY2(!swipeDirectionFromWire(value).has_value(), qPrintable(QString::number(value)));
  }
}

void GestureSwipeProtocolTests::writef_encodesOneByteDirection()
{
  LoopbackStream stream;
  ProtocolUtil::writef(&stream, kMsgDGestureSwipe, static_cast<uint32_t>(SwipeDirection::Right));
  QCOMPARE(stream.bytes(), std::string("DGSW\x02", 5));
}

void GestureSwipeProtocolTests::readf_decodesWhatWritefEncoded()
{
  for (const auto direction : kAllDirections) {
    LoopbackStream stream;
    ProtocolUtil::writef(&stream, kMsgDGestureSwipe, static_cast<uint32_t>(direction));

    char messageCode[4] = {};
    QCOMPARE(stream.read(messageCode, sizeof(messageCode)), static_cast<uint32_t>(4));
    QVERIFY(std::memcmp(messageCode, kMsgDGestureSwipe, 4) == 0);

    uint8_t value = 0;
    QVERIFY(ProtocolUtil::readf(&stream, kMsgDGestureSwipe + 4, &value));
    QCOMPARE(swipeDirectionFromWire(value), std::optional{direction});
    QVERIFY(!stream.isReady());
  }
}

QTEST_MAIN(GestureSwipeProtocolTests)
