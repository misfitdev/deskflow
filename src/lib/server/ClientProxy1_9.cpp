/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_8(name, adoptedStream, server, events)
{
  // do nothing
}

void ClientProxy1_9::gestureSwipe(SwipeDirection direction)
{
  LOG_VERBOSE("send gesture swipe to \"%s\" %s", getName().c_str(), swipeDirectionName(direction));
  ProtocolUtil::writef(getStream(), kMsgDGestureSwipe, static_cast<uint32_t>(direction));
}
