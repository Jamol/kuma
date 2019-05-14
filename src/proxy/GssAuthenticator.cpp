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

#include "GssAuthenticator.h"
#include "util/util.h"
#include "util/kmtrace.h"
#include "util/base64.h"


using namespace kuma;

GssAuthenticator::GssAuthenticator()
{
    KM_SetObjKey("GssAuthenticator");
}

GssAuthenticator::~GssAuthenticator()
{
    if(cred_id_ != GSS_C_NO_CREDENTIAL)
    {
        gss_release_cred(&minor_status_, &cred_id_);
    }
    
    if(gss_domain_name_ != GSS_C_NO_NAME)
    {
        gss_release_name(&minor_status_, &gss_domain_name_);
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

bool GssAuthenticator::init(const std::string& proxy_name, const std::string& auth_scheme, const std::string &domain_user, const std::string &password)
{
    proxy_name_ = proxy_name;
    auth_scheme_ = auth_scheme;
    
    if(is_equal(auth_scheme, "Negotiate"))
    {
        mech_oid_ = GSS_SPNEGO_MECHANISM;
    }
    else if(is_equal(auth_scheme, "NTLM"))
    {
        mech_oid_ = GSS_NTLM_MECHANISM;
    }
    else
    {
        mech_oid_ = gss_OID();
    }

    std::string domain_name;
    std::string user_name;
    const auto back_slash_pos = domain_user.find_first_of("\\");
    if(back_slash_pos != std::string::npos)
    {
        domain_name = domain_user.substr(0, back_slash_pos);
        user_name = domain_user.substr(back_slash_pos + 1);
    }
    else
    {
        user_name = domain_user;
    }
    
    gss_buffer_desc domain_name_token;
    domain_name_token.value = (void*)domain_name.c_str();
    domain_name_token.length = domain_name.length();
    
    major_status_ = gss_import_name(&minor_status_,
                                 &domain_name_token,
                                 GSS_C_NT_HOSTBASED_SERVICE,
                                 &gss_domain_name_);
    
    if(GSS_ERROR(major_status_))
    {
        return false;
    }
    
    if(!domain_user.empty())
    {
        gss_buffer_desc user_name_token;
        user_name_token.value = (void*)user_name.c_str();
        user_name_token.length = user_name.length();
        
        major_status_ = gss_import_name(&minor_status_,
                                     &user_name_token,
                                     GSS_C_NT_USER_NAME,
                                     &gss_user_name_);
        
        if(GSS_ERROR(major_status_))
        {
            return false;
        }
        
        gss_buffer_desc password_token;
        password_token.value = (void*)password.c_str();
        password_token.length = password.length();
        
        // Binary representation of NTLM OID (https://msdn.microsoft.com/en-us/library/cc236636.aspx)
        static char gss_ntlm_oid_value[] = "\x2b\x06\x01\x04\x01\x82\x37\x02\x02\x0a";
        static gss_OID_desc gss_mech_ntlm_OID_desc = {
            .length = ARRAY_SIZE(gss_ntlm_oid_value) - 1,
            .elements = static_cast<void*>(gss_ntlm_oid_value)
        };
        
        gss_OID_desc gss_mech_OID_desc;
        
        gss_mech_OID_desc = gss_mech_ntlm_OID_desc;
        
        gss_OID_set_desc gss_mech_OID_set_desc = {
            .count = 1,
            .elements = &gss_mech_OID_desc
        };
        gss_OID_set desiredMech = &gss_mech_OID_set_desc;
        
        major_status_ = gss_acquire_cred_with_password(&minor_status_,
                                                    gss_user_name_,
                                                    &password_token,
                                                    GSS_C_INDEFINITE,
                                                    desiredMech,
                                                    GSS_C_INITIATE,
                                                    &cred_id_,
                                                    nullptr,
                                                    nullptr);
        
        if(GSS_ERROR(major_status_))
        {
            return false;
        }
    }

    return true;
}

bool GssAuthenticator::nextAuthToken(const std::string& challenge)
{
    auth_token_ = "";
    if (!hasAuthHeader())
    {
        return false;
    }
    
    gss_buffer_desc input_token_obj, output_token_obj;
    gss_buffer_t input_token = &input_token_obj, output_token = &output_token_obj;
    
    const auto decoded_challenge = x64_decode(challenge);
    
    if(!challenge.empty())
    {
        input_token->value = (void*)decoded_challenge.data();
        input_token->length = decoded_challenge.size();
    }
    else
    {
        input_token->length = 0;
    }
    
    major_status_ = gss_init_sec_context(&minor_status_,
                                      cred_id_,
                                      &ctx_id_,
                                      gss_domain_name_,
                                      mech_oid_,
                                      GSS_C_MUTUAL_FLAG | GSS_C_SEQUENCE_FLAG,
                                      0,
                                      nullptr,
                                      input_token,
                                      nullptr,
                                      output_token,
                                      nullptr,
                                      nullptr);
    
    
    if(GSS_ERROR(major_status_))
    {
        return false;
    }
    
    if(output_token->length != 0)
    {
        auth_token_ = x64_encode(output_token->value, output_token->length, false);
        
        gss_release_buffer(&minor_status_, output_token);
    }
    
    return true;
}

std::string GssAuthenticator::getAuthHeader() const
{
    if (hasAuthHeader()) {
        return auth_scheme_ + " " + auth_token_;
    }
    
    return "";
}

bool GssAuthenticator::hasAuthHeader() const
{
    const bool hasCreds = gss_user_name_ != GSS_C_NO_NAME;
    const bool errorHappened = GSS_ERROR(major_status_);
    const bool isInitialState = (major_status_ == 0);
    const bool needsContinue = (major_status_ & GSS_S_CONTINUE_NEEDED);
    
    return hasCreds && !errorHappened && (isInitialState || needsContinue);
}
