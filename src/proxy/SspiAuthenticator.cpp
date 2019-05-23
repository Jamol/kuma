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

static std::string getSecurityStatusString(SECURITY_STATUS status);

SspiAuthenticator::SspiAuthenticator()
{
    SecInvalidateHandle(&cred_handle_);
    SecInvalidateHandle(&ctxt_handle_);

    KM_SetObjKey("SspiAuthenticator");
}

SspiAuthenticator::~SspiAuthenticator()
{
    cleanup();
}

void SspiAuthenticator::cleanup()
{
    if (SecIsValidHandle(&ctxt_handle_)) {
        ::DeleteSecurityContext(&ctxt_handle_);
        SecInvalidateHandle(&ctxt_handle_);
    }
    if (SecIsValidHandle(&cred_handle_)) {
        ::FreeCredentialsHandle(&cred_handle_);
        SecInvalidateHandle(&cred_handle_);
    }
}

bool SspiAuthenticator::init(const AuthInfo &auth_info, const RequestInfo &req_info)
{
    cleanup();

    auth_scheme_ = auth_info.scheme;
    req_info_ = req_info;

    TimeStamp expiry;
    SEC_WINNT_AUTH_IDENTITY auth_data;
    PVOID p_auth_data = nullptr;
    std::string package_name = getAuthScheme(auth_scheme_);
    if (auth_scheme_ == AuthScheme::DIGEST) {
        package_name = "WDigest";
    }

    if (!auth_info.user.empty()) {
        memset(&auth_data, 0, sizeof(auth_data));
        const auto back_slash_pos = auth_info.user.find_first_of("\\");
        if (back_slash_pos != std::string::npos) {
            auth_data.Domain = (unsigned char*)auth_info.user.c_str();
            auth_data.DomainLength = (unsigned long)back_slash_pos;
            auth_data.User = auth_data.Domain + auth_data.DomainLength + 1;
            auth_data.UserLength = (unsigned long)auth_info.user.size() - auth_data.DomainLength - 1;
        }
        else {
            auth_data.User = (unsigned char*)auth_info.user.c_str();
            auth_data.UserLength = (unsigned long)auth_info.user.size();
        }
        if (!auth_info.passwd.empty()) {
            auth_data.Password = (unsigned char*)auth_info.passwd.c_str();
            auth_data.PasswordLength = (unsigned long)auth_info.passwd.size();
        }
        auth_data.Flags = SEC_WINNT_AUTH_IDENTITY_ANSI;

        p_auth_data = &auth_data;
    }

    auto status = ::AcquireCredentialsHandleA(
        NULL,
        (SEC_CHAR *)package_name.c_str(),
        SECPKG_CRED_OUTBOUND,
        NULL,
        p_auth_data,
        NULL,
        NULL,
        &cred_handle_,
        &expiry);

    if (!(SEC_SUCCESS(status))) {
        KUMA_ERRXTRACE("init, AcquireCredentialsHandle failed, status=" << status);
        return false;
    }

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

    std::string target;
    if (auth_scheme_ == AuthScheme::DIGEST)
    {
        target = req_info_.path; // Service Principle Name
    }
    else {
        target = req_info_.service + "/" + req_info_.host; // Service Principle Name
    }

    if (challenge.empty())
    {
        status = ::InitializeSecurityContextA(
            &cred_handle_,
            NULL,
            (SEC_CHAR *)(target.empty() ? nullptr : target.c_str()),
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
        std::string decoded_challenge;
        if (auth_scheme_ == AuthScheme::DIGEST) {
            std::string realm, nonce, qop;
            parseDigestChallenge(challenge, realm, nonce, qop);
            decoded_challenge = challenge;
        }
        else {
            decoded_challenge = x64_decode(challenge);
        }

        SecBufferDesc     in_sec_buf_desc;
        SecBuffer         in_sec_buf[3];

        in_sec_buf_desc.ulVersion = 0;
        in_sec_buf_desc.cBuffers = 1;
        in_sec_buf_desc.pBuffers = in_sec_buf;

        in_sec_buf[0].cbBuffer = (unsigned long)decoded_challenge.size();
        in_sec_buf[0].BufferType = SECBUFFER_TOKEN;
        in_sec_buf[0].pvBuffer = (BYTE *)decoded_challenge.data();
        if (auth_scheme_ == AuthScheme::DIGEST) {
            in_sec_buf_desc.cBuffers = 3;
            in_sec_buf[1].BufferType = SECBUFFER_PKG_PARAMS;
            in_sec_buf[1].pvBuffer = const_cast<char*>(req_info_.method.c_str());
            in_sec_buf[1].cbBuffer = static_cast<unsigned long>(req_info_.method.size());
            in_sec_buf[2].BufferType = SECBUFFER_PKG_PARAMS;
            in_sec_buf[2].pvBuffer = NULL;
            in_sec_buf[2].cbBuffer = 0;
        }

        status = ::InitializeSecurityContextA(
            &cred_handle_,
            SecIsValidHandle(&ctxt_handle_) ? &ctxt_handle_ : nullptr,
            (SEC_CHAR *)target.c_str(),
            ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_USE_HTTP_STYLE,
            0,
            SECURITY_NETWORK_DREP,
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
            KUMA_ERRXTRACE("nextAuthToken, InitializeSecurityContext failed, status=" << status);
            return false;
        }
    }

    if (!out_sec_buf.pvBuffer)
    {
        KUMA_ERRXTRACE("nextAuthToken, the output security buffer is null");
        return false;
    }

    if (auth_scheme_ == AuthScheme::DIGEST) {
        auth_token_.assign((char*)out_sec_buf.pvBuffer, out_sec_buf.cbBuffer);
    }
    else {
        auth_token_ = x64_encode(out_sec_buf.pvBuffer, out_sec_buf.cbBuffer, false);
    }

    ::FreeContextBuffer(out_sec_buf.pvBuffer);

    bool continueAuthFlow = ((SEC_I_CONTINUE_NEEDED == status) || (SEC_I_COMPLETE_AND_CONTINUE == status));

    return continueAuthFlow;
}

std::string SspiAuthenticator::getAuthHeader() const
{
    if (hasAuthHeader()) {
        return getAuthScheme(auth_scheme_) + " " + auth_token_;
    }

    return "";
}

bool SspiAuthenticator::hasAuthHeader() const
{
    return !auth_token_.empty();
}

bool SspiAuthenticator::parseDigestChallenge(const std::string &challenge, std::string &realm, std::string &nonce, std::string qop)
{
    for_each_token(challenge, ',', [&nonce, &realm, &qop](std::string &t) {
        std::string k, v;
        auto pos = t.find('=');
        if (pos == std::string::npos) {
            return true;
        }
        k = t.substr(0, pos);
        if (t[++pos] == '\"') {
            ++pos;
        }
        auto sz = t.size();
        if (t[sz - 1] == '\"' && (sz - 1) > pos) {
            v = t.substr(pos, sz - 1 - pos);
        }
        else {
            v = t.substr(pos);
        }
        if (is_equal(k, "nonce")) {
            nonce = v;
        }
        else if (is_equal(k, "realm")) {
            realm = v;
        }
        else if (is_equal(k, "qop")) {
            qop = v;
        }
        return true;
    });

    return true;
}

static std::string getSecurityStatusString(SECURITY_STATUS status)
{
    switch (status)
    {
    case SEC_E_OK:
        return "SEC_E_OK";

    case SEC_E_INSUFFICIENT_MEMORY:
        return "SEC_E_INSUFFICIENT_MEMORY";

    case SEC_E_INVALID_HANDLE:
        return "SEC_E_INVALID_HANDLE";

    case SEC_E_UNSUPPORTED_FUNCTION:
        return "SEC_E_UNSUPPORTED_FUNCTION";

    case SEC_E_TARGET_UNKNOWN:
        return "SEC_E_TARGET_UNKNOWN";

    case SEC_E_INTERNAL_ERROR:
        return "SEC_E_INTERNAL_ERROR";

    case SEC_E_SECPKG_NOT_FOUND:
        return "SEC_E_SECPKG_NOT_FOUND";

    case SEC_E_NOT_OWNER:
        return "SEC_E_NOT_OWNER";

    case SEC_E_CANNOT_INSTALL:
        return "SEC_E_CANNOT_INSTALL";

    case SEC_E_INVALID_TOKEN:
        return "SEC_E_INVALID_TOKEN";

    case SEC_E_CANNOT_PACK:
        return "SEC_E_CANNOT_PACK";

    case SEC_E_QOP_NOT_SUPPORTED:
        return "SEC_E_QOP_NOT_SUPPORTED";

    case SEC_E_NO_IMPERSONATION:
        return "SEC_E_NO_IMPERSONATION";

    case SEC_E_LOGON_DENIED:
        return "SEC_E_LOGON_DENIED";

    case SEC_E_UNKNOWN_CREDENTIALS:
        return "SEC_E_UNKNOWN_CREDENTIALS";

    case SEC_E_NO_CREDENTIALS:
        return "SEC_E_NO_CREDENTIALS";

    case SEC_E_MESSAGE_ALTERED:
        return "SEC_E_MESSAGE_ALTERED";

    case SEC_E_OUT_OF_SEQUENCE:
        return "SEC_E_OUT_OF_SEQUENCE";

    case SEC_E_NO_AUTHENTICATING_AUTHORITY:
        return "SEC_E_NO_AUTHENTICATING_AUTHORITY";

    case SEC_E_BAD_PKGID:
        return "SEC_E_BAD_PKGID";

    case SEC_E_CONTEXT_EXPIRED:
        return "SEC_E_CONTEXT_EXPIRED";

    case SEC_E_INCOMPLETE_MESSAGE:
        return "SEC_E_INCOMPLETE_MESSAGE";

    case SEC_E_INCOMPLETE_CREDENTIALS:
        return "SEC_E_INCOMPLETE_CREDENTIALS";

    case SEC_E_BUFFER_TOO_SMALL:
        return "SEC_E_BUFFER_TOO_SMALL";

    case SEC_E_WRONG_PRINCIPAL:
        return "SEC_E_WRONG_PRINCIPAL";

    case SEC_E_TIME_SKEW:
        return "SEC_E_TIME_SKEW";

    case SEC_E_UNTRUSTED_ROOT:
        return "SEC_E_UNTRUSTED_ROOT";

    case SEC_E_ILLEGAL_MESSAGE:
        return "SEC_E_ILLEGAL_MESSAGE";

    case SEC_E_CERT_UNKNOWN:
        return "SEC_E_CERT_UNKNOWN";

    case SEC_E_CERT_EXPIRED:
        return "SEC_E_CERT_EXPIRED";

    case SEC_E_ENCRYPT_FAILURE:
        return "SEC_E_ENCRYPT_FAILURE";

    case SEC_E_DECRYPT_FAILURE:
        return "SEC_E_DECRYPT_FAILURE";

    case SEC_E_ALGORITHM_MISMATCH:
        return "SEC_E_ALGORITHM_MISMATCH";

    case SEC_E_SECURITY_QOS_FAILED:
        return "SEC_E_SECURITY_QOS_FAILED";

    case SEC_E_UNFINISHED_CONTEXT_DELETED:
        return "SEC_E_UNFINISHED_CONTEXT_DELETED";

    case SEC_E_NO_TGT_REPLY:
        return "SEC_E_NO_TGT_REPLY";

    case SEC_E_NO_IP_ADDRESSES:
        return "SEC_E_NO_IP_ADDRESSES";

    case SEC_E_WRONG_CREDENTIAL_HANDLE:
        return "SEC_E_WRONG_CREDENTIAL_HANDLE";

    case SEC_E_CRYPTO_SYSTEM_INVALID:
        return "SEC_E_CRYPTO_SYSTEM_INVALID";

    case SEC_E_MAX_REFERRALS_EXCEEDED:
        return "SEC_E_MAX_REFERRALS_EXCEEDED";

    case SEC_E_MUST_BE_KDC:
        return "SEC_E_MUST_BE_KDC";

    case SEC_E_STRONG_CRYPTO_NOT_SUPPORTED:
        return "SEC_E_STRONG_CRYPTO_NOT_SUPPORTED";

    case SEC_E_TOO_MANY_PRINCIPALS:
        return "SEC_E_TOO_MANY_PRINCIPALS";

    case SEC_E_NO_PA_DATA:
        return "SEC_E_NO_PA_DATA";

    case SEC_E_PKINIT_NAME_MISMATCH:
        return "SEC_E_PKINIT_NAME_MISMATCH";

    case SEC_E_SMARTCARD_LOGON_REQUIRED:
        return "SEC_E_SMARTCARD_LOGON_REQUIRED";

    case SEC_E_SHUTDOWN_IN_PROGRESS:
        return "SEC_E_SHUTDOWN_IN_PROGRESS";

    case SEC_E_KDC_INVALID_REQUEST:
        return "SEC_E_KDC_INVALID_REQUEST";

    case SEC_E_KDC_UNABLE_TO_REFER:
        return "SEC_E_KDC_UNABLE_TO_REFER";

    case SEC_E_KDC_UNKNOWN_ETYPE:
        return "SEC_E_KDC_UNKNOWN_ETYPE";

    case SEC_E_UNSUPPORTED_PREAUTH:
        return "SEC_E_UNSUPPORTED_PREAUTH";

    case SEC_E_DELEGATION_REQUIRED:
        return "SEC_E_DELEGATION_REQUIRED";

    case SEC_E_BAD_BINDINGS:
        return "SEC_E_BAD_BINDINGS";

    case SEC_E_MULTIPLE_ACCOUNTS:
        return "SEC_E_MULTIPLE_ACCOUNTS";

    case SEC_E_NO_KERB_KEY:
        return "SEC_E_NO_KERB_KEY";

    case SEC_E_CERT_WRONG_USAGE:
        return "SEC_E_CERT_WRONG_USAGE";

    case SEC_E_DOWNGRADE_DETECTED:
        return "SEC_E_DOWNGRADE_DETECTED";

    case SEC_E_SMARTCARD_CERT_REVOKED:
        return "SEC_E_SMARTCARD_CERT_REVOKED";

    case SEC_E_ISSUING_CA_UNTRUSTED:
        return "SEC_E_ISSUING_CA_UNTRUSTED";

    case SEC_E_REVOCATION_OFFLINE_C:
        return "SEC_E_REVOCATION_OFFLINE_C";

    case SEC_E_PKINIT_CLIENT_FAILURE:
        return "SEC_E_PKINIT_CLIENT_FAILURE";

    case SEC_E_SMARTCARD_CERT_EXPIRED:
        return "SEC_E_SMARTCARD_CERT_EXPIRED";

    case SEC_E_NO_S4U_PROT_SUPPORT:
        return "SEC_E_NO_S4U_PROT_SUPPORT";

    case SEC_E_CROSSREALM_DELEGATION_FAILURE:
        return "SEC_E_CROSSREALM_DELEGATION_FAILURE";

    case SEC_E_REVOCATION_OFFLINE_KDC:
        return "SEC_E_REVOCATION_OFFLINE_KDC";

    case SEC_E_ISSUING_CA_UNTRUSTED_KDC:
        return "SEC_E_ISSUING_CA_UNTRUSTED_KDC";

    case SEC_E_KDC_CERT_EXPIRED:
        return "SEC_E_KDC_CERT_EXPIRED";

    case SEC_E_KDC_CERT_REVOKED:
        return "SEC_E_KDC_CERT_REVOKED";

    case SEC_E_INVALID_PARAMETER:
        return "SEC_E_INVALID_PARAMETER";

    case SEC_E_DELEGATION_POLICY:
        return "SEC_E_DELEGATION_POLICY";

    case SEC_E_POLICY_NLTM_ONLY:
        return "SEC_E_POLICY_NLTM_ONLY";

    case SEC_E_NO_CONTEXT:
        return "SEC_E_NO_CONTEXT";

    case SEC_E_PKU2U_CERT_FAILURE:
        return "SEC_E_PKU2U_CERT_FAILURE";

    case SEC_E_MUTUAL_AUTH_FAILED:
        return "SEC_E_MUTUAL_AUTH_FAILED";

    case SEC_I_CONTINUE_NEEDED:
        return "SEC_I_CONTINUE_NEEDED";

    case SEC_I_COMPLETE_NEEDED:
        return "SEC_I_COMPLETE_NEEDED";

    case SEC_I_COMPLETE_AND_CONTINUE:
        return "SEC_I_COMPLETE_AND_CONTINUE";

    case SEC_I_LOCAL_LOGON:
        return "SEC_I_LOCAL_LOGON";

    case SEC_I_CONTEXT_EXPIRED:
        return "SEC_I_CONTEXT_EXPIRED";

    case SEC_I_INCOMPLETE_CREDENTIALS:
        return "SEC_I_INCOMPLETE_CREDENTIALS";

    case SEC_I_RENEGOTIATE:
        return "SEC_I_RENEGOTIATE";

    case SEC_I_NO_LSA_CONTEXT:
        return "SEC_I_NO_LSA_CONTEXT";

    case SEC_I_SIGNATURE_NEEDED:
        return "SEC_I_SIGNATURE_NEEDED";

    case SEC_I_NO_RENEGOTIATION:
        return "SEC_I_NO_RENEGOTIATION";
    }

    return "SEC_E_UNKNOWN_" + std::to_string(status);
}

