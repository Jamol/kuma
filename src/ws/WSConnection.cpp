/* Copyright (c) 2014-2019, Fengping Bao <jamol@live.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


#include "WSConnection.h"

using namespace kuma;
using namespace kuma::ws;

WSConnection::WSConnection()
{
    
}

KMError WSConnection::setSubprotocol(const std::string& subprotocol)
{
    subprotocol_ = subprotocol;
    return KMError::NOERR;
}

KMError WSConnection::setExtensions(const std::string& extensions)
{
    extensions_ = extensions;
    return KMError::NOERR;
}

void WSConnection::onStateOpen()
{
    setState(State::OPEN);
    if (open_cb_) open_cb_(KMError::NOERR);
}

void WSConnection::onStateError(KMError err)
{
    setState(State::IN_ERROR);
    if(error_cb_) error_cb_(err);
}
