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

#include "ExtensionHandler.h"
#include "PMCE_Deflate.h"
#include "libkev/src/util/util.h"

using namespace kuma;
using namespace kuma::ws;


KMError ExtensionHandler::handleIncomingFrame(FrameHeader hdr, KMBuffer &payload)
{
    if (!ws_extensions_.empty()) {
        return ws_extensions_[0]->handleIncomingFrame(hdr, payload);
    } else {
        return onIncomingFrame(hdr, payload);
    }
}

KMError ExtensionHandler::handleOutgoingFrame(FrameHeader hdr, KMBuffer &payload)
{
    if (!ws_extensions_.empty()) {
        return ws_extensions_[0]->handleOutgoingFrame(hdr, payload);
    } else {
        return onOutgoingFrame(hdr, payload);
    }
}

KMError ExtensionHandler::negotiateExtensions(const std::string &extensions, bool is_answer)
{
    bool pmce_done = false;
    kev::for_each_token(extensions, ',', [this, &pmce_done, is_answer] (std::string &str) {
        std::string extension_name;
        kev::for_each_token(str, ';', [&extension_name] (std::string &str) {
            extension_name = str;
            return false;
        });
        if (!pmce_done && extension_name == kPerMessageDeflate) {
            auto pmce = std::make_shared<PMCE_Deflate>();
            KMError err;
            std::string ext_answer;
            if (is_answer) {
                err = pmce->negotiateAnswer(str);
            } else {
                err = pmce->negotiateOffer(str, ext_answer);
            }
            if (err == KMError::NOERR && pmce->init() == KMError::NOERR) {
                if (!ws_extensions_.empty()) {
                    auto prev = ws_extensions_[ws_extensions_.size() - 1];
                    prev->setIncomingCallback([pmce=pmce.get()] (FrameHeader hdr, KMBuffer &buf) {
                        return pmce->handleIncomingFrame(hdr, buf);
                    });
                    prev->setOutgoingCallback([pmce=pmce.get()] (FrameHeader hdr, KMBuffer &buf) {
                        return pmce->handleOutgoingFrame(hdr, buf);
                    });
                }
                ws_extensions_.emplace_back(std::move(pmce));
                if (!ext_answer.empty()) {
                    if (extension_answer_.empty()) {
                        extension_answer_ = std::move(ext_answer);
                    } else {
                        extension_answer_ += ", " + ext_answer;
                    }
                }
                pmce_done = true;
            }
        }
        
        return true;
    });
    
    if (!ws_extensions_.empty()) {
        auto last = ws_extensions_[ws_extensions_.size() - 1];
        last->setIncomingCallback([this] (FrameHeader hdr, KMBuffer &buf) {
            return onIncomingFrame(hdr, buf);
        });
        last->setOutgoingCallback([this] (FrameHeader hdr, KMBuffer &buf) {
            return onOutgoingFrame(hdr, buf);
        });
    }
    
    return KMError::NOERR;
}

std::string ExtensionHandler::getExtensionOffer()
{
    return "permessage-deflate; client_max_window_bits";
}
