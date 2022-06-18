/* Copyright (c) 2014-2022, Fengping Bao <jamol@live.com>
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

#ifdef KUMA_HAS_OPENSSL

#include "ssl_utils.h"

#include <Security/Security.h>

KUMA_NS_BEGIN

#ifdef KUMA_OS_IOS

bool loadTrustedSystemCertificates(X509_STORE *x509_store)
{
    return false;
}

#else // KUMA_OS_IOS

bool loadTrustedSystemCertificates(X509_STORE *x509_store)
{
    CFArrayRef certArray = nullptr;
    auto domain = kSecTrustSettingsDomainSystem;

    auto status = SecTrustSettingsCopyCertificates(domain, &certArray);
    if(status != errSecSuccess) {
        // errSecNoTrustSettings = -25263
        return false;
    }
    auto certCount = CFArrayGetCount(certArray);

    for(int i = 0; i < certCount; ++i) {
        auto certRef = (SecCertificateRef)CFArrayGetValueAtIndex(certArray, i);
        if(CFGetTypeID(certRef) != SecCertificateGetTypeID()) {
            break;
        }

        CFArrayRef trustSettings = nullptr;
        status = SecTrustSettingsCopyTrustSettings(certRef, domain, &trustSettings);
        if(status != errSecSuccess) {
            continue;
        }
        CFRelease(trustSettings);
        
        CFDataRef certData = (CFDataRef)SecCertificateCopyData(certRef);
        if (certData) {
            const UInt8* certBytes = CFDataGetBytePtr(certData);
            auto *x509_cert = d2i_X509(NULL, &certBytes, (long)CFDataGetLength(certData));
            if (x509_cert) {
                X509_STORE_add_cert(x509_store, x509_cert);
                X509_free(x509_cert);
            }
            CFRelease(certData);
        }
    }
    CFRelease(certArray);
    return true;
}

#endif // KUMA_OS_IOS

KUMA_NS_END

#endif // KUMA_HAS_OPENSSL

