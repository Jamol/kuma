
#include "HttpResponse.h"
#include "util/kmtrace.h"

KUMA_NS_BEGIN

//////////////////////////////////////////////////////////////////////////
HttpResponse::HttpResponse()
: destroy_flag_ptr_(nullptr)
{

}

HttpResponse::~HttpResponse()
{
    if(destroy_flag_ptr_) {
        *destroy_flag_ptr_ = true;
    }
}

KUMA_NS_END
