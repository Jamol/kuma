#ifndef __Uri_H__
#define __Uri_H__

#include "kmdefs.h"
#include <string>
#include <map>
#include <vector>

KUMA_NS_BEGIN

class Uri
{
public:
	Uri();
    Uri(const std::string& url);
	~Uri();

    bool parse(const std::string& url);
    
    const std::string& getScheme() { return scheme_; }
    const std::string& getHost() { return host_; }
    const std::string& getPort() { return port_; }
    const std::string& getPath() { return path_; }
    const std::string& getQuery() { return query_; }
    const std::string& getFragment() { return fragment_; }
    
private:
    bool parse_host_port(const std::string& hostport, std::string& host, std::string& port);

private:
    std::string         scheme_;
    std::string         host_;
    std::string         port_;
	std::string         path_;
    std::string         query_;
    std::string         fragment_;
};

KUMA_NS_END

#endif
