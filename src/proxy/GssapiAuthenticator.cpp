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

#include "GssapiAuthenticator.h"
#include "libkev/src/util/util.h"
#include "libkev/src/util/kmtrace.h"
#include "util/base64.h"


using namespace kuma;

static std::string getSecurityStatusString(OM_uint32 major_status);

GssapiAuthenticator::GssapiAuthenticator()
{
    KM_SetObjKey("GssapiAuthenticator");
}

GssapiAuthenticator::~GssapiAuthenticator()
{
    if(cred_id_ != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&minor_status_, &cred_id_);
    }
    
    if(gss_spn_name_ != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status_, &gss_spn_name_);
    }
    
    if(gss_user_name_ != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status_, &gss_user_name_);
    }
    
    if(ctx_id_ != GSS_C_NO_CONTEXT)
    {
        gss_delete_sec_context(&minor_status_, &ctx_id_, GSS_C_NO_BUFFER);
    }
}

bool GssapiAuthenticator::init(const AuthInfo &auth_info, const RequestInfo &req_info)
{
    auth_scheme_ = auth_info.scheme;
    
#ifdef GSS_NTLM_MECHANISM
    static gss_OID gss_mech_ntlm_OID = GSS_NTLM_MECHANISM;
#else
    static gss_OID_desc gss_mech_ntlm_OID_desc = {
        10,
        const_cast<char*>("\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a")
    };
    static gss_OID gss_mech_ntlm_OID = &gss_mech_ntlm_OID_desc;
#endif
    
#ifdef GSS_SPNEGO_MECHANISM
    static gss_OID gss_mech_spnego_OID = GSS_SPNEGO_MECHANISM;
#else
    static gss_OID_desc gss_mech_spnego_OID_desc = {
        6,
        const_cast<char*>("\x2b\x06\x01\x05\x05\x02")
    };
    static gss_OID gss_mech_spnego_OID = &gss_mech_spnego_OID_desc;
#endif
    
#ifdef GSS_KRB5_MECHANISM
    static gss_OID gss_mech_krb5_OID = GSS_KRB5_MECHANISM;
#else
    static gss_OID_desc gss_mech_krb5_OID_desc = {
        9,
        const_cast<char*>("\x2a\x86\x48\x86\xf7\x12\x01\x02\x02")
    };
    static gss_OID gss_mech_krb5_OID = &gss_mech_krb5_OID_desc;
#endif
    
    if(auth_scheme_ == AuthScheme::NTLM)
    {
        mech_oid_ = gss_mech_ntlm_OID;
    }
    else
    {
        mech_oid_ = gss_mech_spnego_OID;
    }
    
    std::string spn;
    if (!req_info.service.empty()) {
        spn = req_info.service + "/" + req_info.host;
    } else {
        spn = "HTTP/" + req_info.host;
    }
    
    gss_buffer_desc spn_buffer = GSS_C_EMPTY_BUFFER;
    spn_buffer.value = const_cast<char*>(spn.c_str());
    spn_buffer.length = spn.size() + 1;
    major_status_ = gss_import_name(&minor_status_,
                                    &spn_buffer,
                                    GSS_C_NT_HOSTBASED_SERVICE,
                                    &gss_spn_name_);
    if(GSS_ERROR(major_status_))
    {
        KM_ERRXTRACE("init, SPN gss_import_name failed, err=" << getSecurityStatusString(major_status_));
        return false;
    }
    
    if(!auth_info.user.empty())
    {
        std::string domain_name;
        std::string user_name;
        const auto back_slash_pos = auth_info.user.find_first_of("\\");
        if(back_slash_pos != std::string::npos)
        {
            domain_name = auth_info.user.substr(0, back_slash_pos);
            user_name = auth_info.user.substr(back_slash_pos + 1);
        }
        else
        {
            user_name = auth_info.user;
        }
        
        gss_buffer_desc user_buffer;
        user_buffer.value = (void*)user_name.c_str();
        user_buffer.length = user_name.length();
        
        major_status_ = gss_import_name(&minor_status_,
                                     &user_buffer,
                                     GSS_C_NT_USER_NAME,
                                     &gss_user_name_);
        
        if(GSS_ERROR(major_status_))
        {
            KM_ERRXTRACE("init, username gss_import_name failed, err=" << getSecurityStatusString(major_status_));
            return false;
        }
        
        gss_buffer_desc passwd_buffer;
        passwd_buffer.value = (void*)auth_info.passwd.c_str();
        passwd_buffer.length = auth_info.passwd.length();
        
        gss_OID_set desired_mech = GSS_C_NO_OID_SET;//&gss_mech_OID_set_desc;
#ifndef GSS_SPNEGO_MECHANISM
        gss_OID_set_desc gss_mech_OID_set_desc = {
            .count = 1,
            .elements = mech_oid_
        };
        desired_mech = &gss_mech_OID_set_desc;
#endif
        
        major_status_ = gss_acquire_cred_with_password(&minor_status_,
                                                    gss_user_name_,
                                                    &passwd_buffer,
                                                    GSS_C_INDEFINITE,
                                                    desired_mech,
                                                    GSS_C_INITIATE,
                                                    &cred_id_,
                                                    nullptr,
                                                    nullptr);
        
        if(GSS_ERROR(major_status_))
        {
            KM_ERRXTRACE("init, gss_acquire_cred_with_password failed, err=" << getSecurityStatusString(major_status_));
            return false;
        }
    }

    return true;
}

bool GssapiAuthenticator::nextAuthToken(const std::string& challenge)
{
    auth_token_ = "";
    if (!hasAuthHeader())
    {
        return false;
    }
    
    gss_buffer_desc input_token_obj = GSS_C_EMPTY_BUFFER;
    gss_buffer_desc output_token_obj = GSS_C_EMPTY_BUFFER;
    gss_buffer_t input_token = &input_token_obj;
    gss_buffer_t output_token = &output_token_obj;
    
    std::string decoded_challenge;
    if(!challenge.empty())
    {
        decoded_challenge = x64_decode(challenge);
        input_token->value = (void*)decoded_challenge.data();
        input_token->length = decoded_challenge.size();
    }
    
    major_status_ = gss_init_sec_context(&minor_status_,
                                      cred_id_,
                                      &ctx_id_,
                                      gss_spn_name_,
                                      mech_oid_,
                                      GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG,
                                      GSS_C_INDEFINITE,
                                      GSS_C_NO_CHANNEL_BINDINGS,
                                      input_token,
                                      nullptr,
                                      output_token,
                                      nullptr,
                                      nullptr);
    
    if(GSS_ERROR(major_status_))
    {
        KM_ERRXTRACE("init, gss_init_sec_context failed, err=" << getSecurityStatusString(major_status_));
        return false;
    }
    
    if(output_token->length != 0)
    {
        auth_token_ = x64_encode(output_token->value, output_token->length, false);
        
        gss_release_buffer(&minor_status_, output_token);
    }
    
    return true;
}

std::string GssapiAuthenticator::getAuthHeader() const
{
    if (hasAuthHeader()) {
        return getAuthScheme(auth_scheme_) + " " + auth_token_;
    }
    
    return "";
}

bool GssapiAuthenticator::hasAuthHeader() const
{
    const bool hasCreds = gss_user_name_ != GSS_C_NO_NAME;
    const bool errorHappened = GSS_ERROR(major_status_);
    const bool isInitialState = (major_status_ == 0);
    const bool needsContinue = (major_status_ & GSS_S_CONTINUE_NEEDED);
    
    return hasCreds && !errorHappened && (isInitialState || needsContinue);
}

static std::string getSecurityStatusString(OM_uint32 major_status)
{
    OM_uint32 routine_status = GSS_ROUTINE_ERROR(major_status);
    OM_uint32 calling_status = GSS_CALLING_ERROR(major_status);
    OM_uint32 supplemental_status = GSS_SUPPLEMENTARY_INFO(major_status);
    switch (routine_status) {
        case GSS_S_FAILURE:
            return "GSS_S_FAILURE";
        case GSS_S_DEFECTIVE_TOKEN:
            return "GSS_S_DEFECTIVE_TOKEN";
        case GSS_S_DEFECTIVE_CREDENTIAL:
            return "GSS_S_DEFECTIVE_CREDENTIAL";
        case GSS_S_BAD_SIG:
            return "GSS_S_BAD_SIG";
        case GSS_S_NO_CRED:
            return "GSS_S_NO_CRED";
        case GSS_S_CREDENTIALS_EXPIRED:
            return "GSS_S_CREDENTIALS_EXPIRED";
        case GSS_S_BAD_BINDINGS:
            return "GSS_S_BAD_BINDINGS";
        case GSS_S_NO_CONTEXT:
            return "GSS_S_NO_CONTEXT";
        case GSS_S_BAD_NAMETYPE:
            return "GSS_S_BAD_NAMETYPE";
        case GSS_S_BAD_NAME:
            return "GSS_S_BAD_NAME";
        case GSS_S_BAD_MECH:
            return "GSS_S_BAD_MECH";
        default:
            if (routine_status != 0) {
                return std::to_string(routine_status) + " " +
                       std::to_string(calling_status) + " " +
                       std::to_string(supplemental_status);
            }
    }
    
    return "NOERR";
}
