#ifndef __TestUtil_H__
#define __TestUtil_H__

#include "kmapi.h"

#include <string>

#ifdef KUMA_OS_WIN
# define PATH_SEPARATOR '\\'
# define strcasecmp     _stricmp
#else
# define PATH_SEPARATOR '/'
#endif

std::string getCurrentModulePath();
bool splitPath(const std::string &uri, std::string &path, std::string &name, std::string &ext);
bool fileExist(const std::string &file);
bool isDir(const std::string &dir);
std::string getMime(const std::string &ext);

#endif
