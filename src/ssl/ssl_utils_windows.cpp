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

#include <Wincrypt.h>

KUMA_NS_BEGIN

bool loadTrustedSystemCertificates(X509_STORE *x509_store)
{
    auto hStore = CertOpenSystemStore(NULL, L"ROOT");
    if (!hStore) {
        return false;
    }

    PCCERT_CONTEXT pContext = NULL;
    while (pContext = CertEnumCertificatesInStore(hStore, pContext)) {
        const auto *cert_bytes = (const uint8_t *)(pContext->pbCertEncoded);
        if (!cert_bytes) {
            continue;
        }
        auto *x509_cert = d2i_X509(NULL, &cert_bytes, pContext->cbCertEncoded);
        if (x509_cert) {
            X509_STORE_add_cert(x509_store, x509_cert);
            X509_free(x509_cert);
        }
    }

    if (pContext) {
        CertFreeCertificateContext(pContext);
    }
    CertCloseStore(hStore, 0);
    return true;
}

KUMA_NS_END

#pragma comment (lib, "crypt32.lib")

#endif // KUMA_HAS_OPENSSL
