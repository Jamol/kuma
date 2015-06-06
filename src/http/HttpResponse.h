#ifndef __HttpResponse_H__
#define __HttpResponse_H__

#include "kmdefs.h"
#include <string>
#include <map>
#include <vector>

KUMA_NS_BEGIN

class HttpResponse
{
public:
    HttpResponse();
    ~HttpResponse();


private:
    bool*               destroy_flag_ptr_;
};

KUMA_NS_END

#endif
