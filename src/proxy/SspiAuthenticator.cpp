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

#include "SspiAuthenticator.h"
#include "util/util.h"
#include "util/kmtrace.h"
#include "util/base64.h"


#pragma comment(lib, "Secur32.lib")

#define SEC_SUCCESS(status) ((status) >= 0)


using namespace kuma;

SspiAuthenticator::SspiAuthenticator()
{
    KM_SetObjKey("SspiAuthenticator");
}

SspiAuthenticator::~SspiAuthenticator()
{
    if (initialized_) {
        ::FreeCredentialsHandle(&cred_handle_);
    }
}

bool SspiAuthenticator::init(const std::string& proxy_name, const std::string& auth_scheme, const std::string &domain_user, const std::string &password)
{
    proxy_name_ = proxy_name;
    auth_scheme_ = auth_scheme;

    TimeStamp expiry;
    SEC_WINNT_AUTH_IDENTITY auth_data;
    PVOID p_auth_data = nullptr;

    if (!domain_user.empty() && is_equal(auth_scheme, "NTLM")) {
        memset(&auth_data, 0, sizeof(auth_data));
        const auto back_slash_pos = domain_user.find_first_of("\\");
        if (back_slash_pos != std::string::npos) {
            auth_data.Domain = (unsigned char*)domain_user.c_str();
            auth_data.DomainLength = (unsigned long)back_slash_pos;
            auth_data.User = auth_data.Domain + auth_data.DomainLength + 1;
            auth_data.UserLength = (unsigned long)domain_user.size() - auth_data.DomainLength - 1;
        }
        else {
            auth_data.User = (unsigned char*)domain_user.c_str();
            auth_data.UserLength = (unsigned long)domain_user.size();
        }
        if (!password.empty()) {
            auth_data.Password = (unsigned char*)password.c_str();
            auth_data.PasswordLength = (unsigned long)password.size();
        }
        auth_data.Flags = SEC_WINNT_AUTH_IDENTITY_ANSI;

        p_auth_data = &auth_data;
    }

    auto status = ::AcquireCredentialsHandleA(
        NULL,
        (SEC_CHAR *)auth_scheme.c_str(),
        SECPKG_CRED_OUTBOUND,
        NULL,
        p_auth_data,
        NULL,
        NULL,
        &cred_handle_,
        &expiry);

    if (!(SEC_SUCCESS(status))) {
        return false;
    }

    initialized_ = true;
    return true;
}

bool SspiAuthenticator::nextAuthToken(const std::string& challenge)
{
    TimeStamp           expiry;
    SECURITY_STATUS     status;
    SecBufferDesc       out_sec_buf_desc;
    SecBuffer           out_sec_buf;
    ULONG               ctxt_attrs;

    out_sec_buf_desc.ulVersion = SECBUFFER_VERSION;
    out_sec_buf_desc.cBuffers = 1;
    out_sec_buf_desc.pBuffers = &out_sec_buf;

    out_sec_buf.cbBuffer = 0;
    out_sec_buf.BufferType = SECBUFFER_TOKEN;
    out_sec_buf.pvBuffer = 0;

    std::string target{ "InetSvcs" };

    if (is_equal(auth_scheme_, "Negotiate"))
    {
        target = "http/" + proxy_name_; // Service Principle Name
    }

    if (challenge.empty())
    {
        status = ::InitializeSecurityContextA(
            &cred_handle_,
            NULL,
            (SEC_CHAR *)target.c_str(),
            ISC_REQ_ALLOCATE_MEMORY, //ISC_REQ_CONFIDENTIALITY ,
            0,
            SECURITY_NETWORK_DREP, //SECURITY_NATIVE_DREP,
            NULL,
            0,
            &ctxt_handle_,
            &out_sec_buf_desc,
            &ctxt_attrs,
            &expiry);
    }
    else
    {
        auto decoded_challenge = x64_decode(challenge);

        SecBufferDesc     in_sec_buf_desc;
        SecBuffer         in_sec_buf;

        in_sec_buf_desc.ulVersion = 0;
        in_sec_buf_desc.cBuffers = 1;
        in_sec_buf_desc.pBuffers = &in_sec_buf;

        in_sec_buf.cbBuffer = (unsigned long)decoded_challenge.size();
        in_sec_buf.BufferType = SECBUFFER_TOKEN;
        in_sec_buf.pvBuffer = (BYTE *)decoded_challenge.data();

        status = ::InitializeSecurityContextA(
            &cred_handle_,
            &ctxt_handle_,
            (SEC_CHAR *)target.c_str(),
            ISC_REQ_ALLOCATE_MEMORY, //ISC_REQ_CONFIDENTIALITY,
            0,
            SECURITY_NETWORK_DREP, // SECURITY_NATIVE_DREP,
            &in_sec_buf_desc,
            0,
            &ctxt_handle_,
            &out_sec_buf_desc,
            &ctxt_attrs,
            &expiry);
    }

    if ((SEC_I_COMPLETE_NEEDED == status) || (SEC_I_COMPLETE_AND_CONTINUE == status))
    {
        status = ::CompleteAuthToken(&ctxt_handle_, &out_sec_buf_desc);
        if (!SEC_SUCCESS(status))
        {
            return false;
        }
    }

    if (!out_sec_buf.pvBuffer)
    {
        return false;
    }

    auth_token_ = x64_encode(out_sec_buf.pvBuffer, out_sec_buf.cbBuffer, false);

    ::FreeContextBuffer(out_sec_buf.pvBuffer);

    bool continueAuthFlow = ((SEC_I_CONTINUE_NEEDED == status) || (SEC_I_COMPLETE_AND_CONTINUE == status));

    return continueAuthFlow;
}

std::string SspiAuthenticator::getAuthHeader() const
{
    if (hasAuthHeader()) {
        return auth_scheme_ + " " + auth_token_;
    }

    return "";
}

bool SspiAuthenticator::hasAuthHeader() const
{
    return true;
}
