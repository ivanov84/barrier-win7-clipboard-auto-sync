/*
 * barrier -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2006 Chris Schoeneman
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "server/BaseClientProxy.h"

//
// BaseClientProxy
//

BaseClientProxy::BaseClientProxy(const std::string& name) :
    m_name(name),
    m_x(0),
    m_y(0)
{
    for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
        m_lastClipboardSeqNum[id] = 0;
    }
}

BaseClientProxy::~BaseClientProxy()
{
    // do nothing
}

bool
BaseClientProxy::checkClipboardSeqNum(ClipboardID id, UInt32 seqNum) const
{
    // true when the incoming sequence number is older than the last one
    // seen from this same screen.  sequence numbers are per-screen
    // (keyboard-focus) generations, so they may only be compared against
    // values previously reported by this screen, never against another
    // screen's value (which is what the shared per-clipboard counter used
    // to be compared against, and which wrongly rejected legitimate client
    // copies -- see the "missequenced" clipboard drops).
    return seqNum < m_lastClipboardSeqNum[id];
}

void
BaseClientProxy::setLastClipboardSeqNum(ClipboardID id, UInt32 seqNum)
{
    m_lastClipboardSeqNum[id] = seqNum;
}

void
BaseClientProxy::setJumpCursorPos(SInt32 x, SInt32 y)
{
    m_x = x;
    m_y = y;
}

void
BaseClientProxy::getJumpCursorPos(SInt32& x, SInt32& y) const
{
    x = m_x;
    y = m_y;
}

std::string BaseClientProxy::getName() const
{
    return m_name;
}
