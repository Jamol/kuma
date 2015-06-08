
#include "Uri.h"

KUMA_NS_BEGIN

//////////////////////////////////////////////////////////////////////////
Uri::Uri()
{
    
}

Uri::Uri(const std::string& url)
{
    parse(url);
}

Uri::~Uri()
{
    
}

bool Uri::parse(const std::string& url)
{
    auto pos = url.find("://");
    if(pos != std::string::npos) {
        scheme_.assign(url.begin(), url.begin()+pos);
        pos += 3;
        auto path_pos = url.find('/', pos);
        std::string hostport;
        if(path_pos == std::string::npos) {
            hostport.assign(url.begin()+pos, url.end());
        } else {
            hostport.assign(url.begin()+pos, url.begin()+path_pos);
        }
        pos = path_pos;
        parse_host_port(hostport, host_, port_);
    } else {
        pos = 0;
    }
    if (pos == std::string::npos) {
        path_ = "/";
        return true;
    } else if(url.at(pos) != '/') {
        return false;
    }
    // now url[pos] == '/'
    auto query_pos = url.find('?', pos+1);
    if (query_pos == std::string::npos) {
        auto fragment_pos = url.find('#', pos+1);
        if(fragment_pos == std::string::npos) {
            path_.assign(url.begin()+pos, url.end());
        } else {
            path_.assign(url.begin()+pos, url.begin()+fragment_pos);
            ++fragment_pos;
            fragment_.assign(url.begin()+fragment_pos, url.end());
        }
        return true;
    }
    path_.assign(url.begin()+pos, url.begin()+query_pos);
    ++query_pos;
    auto fragment_pos = url.find('#', query_pos);
    if(fragment_pos == std::string::npos) {
        query_.assign(url.begin()+query_pos, url.end());
    } else {
        query_.assign(url.begin()+query_pos, url.begin()+fragment_pos);
        ++fragment_pos;
        fragment_.assign(url.begin()+fragment_pos, url.end());
    }
    return true;
}

bool Uri::parse_host_port(const std::string& hostport, std::string& host, std::string& port)
{
    host.clear();
    port.clear();
    auto pos = hostport.find('[');
    if(pos != std::string::npos) { // ipv6
        ++pos;
        auto pos1 = hostport.find(']', pos);
        if(pos1 == std::string::npos) {
            return false;
        }
        host.assign(hostport.begin()+pos, hostport.begin()+pos1);
        pos = pos1 + 1;
        pos = hostport.find(':', pos);
        if(pos != std::string::npos) {
            port.assign(hostport.begin()+pos+1, hostport.end());
        }
    } else {
        pos = hostport.find(':');
        if(pos != std::string::npos) {
            host.assign(hostport.begin(), hostport.begin()+pos);
            port.assign(hostport.begin()+pos+1, hostport.end());
        } else {
            host = hostport;
        }
    }
    return true;
}

KUMA_NS_END
