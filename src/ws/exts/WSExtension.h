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

#pragma once

#include "kmdefs.h"
#include "kmbuffer.h"
#include "ws/wsdefs.h"
#include "http/httpdefs.h"

#include <functional>
#include <string>

WS_NS_BEGIN

class WSExtension
{
public:
    using FrameCallback = std::function<KMError(FrameHeader, KMBuffer &)>;
    using Ptr = std::shared_ptr<WSExtension>;
    
    virtual ~WSExtension() {}
    
    virtual KMError handleIncomingFrame(FrameHeader hdr, KMBuffer &payload) = 0;
    virtual KMError handleOutgoingFrame(FrameHeader hdr, KMBuffer &payload) = 0;
    virtual KMError getOffer(std::string &offer) = 0;
    virtual KMError negotiateAnswer(const std::string &answer) = 0;
    virtual KMError negotiateOffer(const std::string &offer, std::string &answer) = 0;
    
    void setIncomingCallback(FrameCallback cb) { incoming_cb_ = std::move(cb); }
    void setOutgoingCallback(FrameCallback cb) { outgoing_cb_ = std::move(cb); }
    
    virtual std::string getExtensionName() const = 0;
    
    static KMError parseKeyValue(const std::string &str, std::string &key, std::string &value);
    static KMError parseParameterList(const std::string &parameters, KeyValueList &param_list);
    
protected:
    virtual KMError onIncomingFrame(FrameHeader hdr, KMBuffer &payload)
    {
        if (incoming_cb_) {
            return incoming_cb_(hdr, payload);
        }
        
        return KMError::INVALID_STATE;
    }
    virtual KMError onOutgoingFrame(FrameHeader hdr, KMBuffer &payload)
    {
        if (outgoing_cb_) {
            return outgoing_cb_(hdr, payload);
        }
        
        return KMError::INVALID_STATE;
    }
    
protected:
    FrameCallback incoming_cb_;
    FrameCallback outgoing_cb_;
};

WS_NS_END

