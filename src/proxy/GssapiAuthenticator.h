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
#include "ProxyAuthenticator.h"
#include "libkev/src/utils/kmobject.h"

#include <string>

#include <GSS/gssapi.h>
#include <GSS/gssapi_krb5.h>
#include <GSS/gssapi_spnego.h>


KUMA_NS_BEGIN

class GssapiAuthenticator : public kev::KMObject, public ProxyAuthenticator
{
public:
    GssapiAuthenticator();
    ~GssapiAuthenticator();

    bool init(const AuthInfo &auth_info, const RequestInfo &req_info);
    bool nextAuthToken(const std::string& challenge) override;
    std::string getAuthHeader() const override;
    bool hasAuthHeader() const override;

protected:
    AuthScheme      auth_scheme_;
    std::string     auth_token_;
    
    gss_ctx_id_t    ctx_id_ = GSS_C_NO_CONTEXT;
    gss_cred_id_t   cred_id_ = GSS_C_NO_CREDENTIAL;
    OM_uint32       major_status_ = 0;
    OM_uint32       minor_status_ = 0;
    gss_OID         mech_oid_ = GSS_C_NO_OID;
    gss_name_t      gss_spn_name_ = GSS_C_NO_NAME;
    gss_name_t      gss_user_name_ = GSS_C_NO_NAME;
};

KUMA_NS_END
