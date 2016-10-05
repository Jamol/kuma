#include "testutil.h"

#include <string>
#include <sys/stat.h>

#ifdef KUMA_OS_WIN
# include <direct.h>
#else
# include <unistd.h>
# ifdef KUMA_OS_MAC
#  include <libproc.h>
# endif
#endif

#include <errno.h>
#include <string.h>

#ifdef KUMA_OS_WIN
typedef struct _stat _f_stat_t;
#define f_stat		 _stat
#define f_mkdir(f, m)	 _mkdir(f)
#define f_remove	 remove
#define f_rename	 rename
#define IS_DIR(s)	 ((s.st_mode & _S_IFDIR) == _S_IFDIR)
#else
typedef struct stat _f_stat_t;
#define f_stat		 stat
#define f_mkdir(f, m)	 mkdir(f, m)
#define IS_DIR(s)	 ((s.st_mode & S_IFDIR) == S_IFDIR)
#endif

#ifdef KUMA_OS_WIN
#else
#include <dlfcn.h>
#endif

std::string getCurrentModulePath()
{
    std::string str_path;
#ifdef KUMA_OS_WIN
    char c_path[MAX_PATH] = { 0 };
    HMODULE hModule = NULL;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCTSTR>(getCurrentModulePath), &hModule);
    GetModuleFileName(hModule, c_path, sizeof(c_path));
    str_path = c_path;
#else
    Dl_info dlInfo;
    dladdr((void*)getCurrentModulePath, &dlInfo);
    str_path = dlInfo.dli_fname;
#endif
    auto pos = str_path.rfind(PATH_SEPARATOR, str_path.size());
    str_path.resize(pos);
    return str_path;
}

bool splitPath(const std::string &uri, std::string &path, std::string &name, std::string &ext)
{
    if(uri.empty()) {
        return false;
    }
    auto pos = uri.rfind(PATH_SEPARATOR);
    if (pos == std::string::npos) {
        name = uri;
    } else {
        path = uri.substr(0, pos);
        name = uri.substr(pos + 1);
    }
    pos = name.rfind('.');
    if (pos != std::string::npos) {
        ext = name.substr(pos + 1);
        name = name.substr(0, pos);
    }
    return true;
}

bool getCurrentPath(std::string &path)
{
#ifdef KUMA_OS_WIN
    char pathbuf[1024] = {0};
    if(!GetModuleFileName(NULL, pathbuf, sizeof(pathbuf))) {
        return false;
    }
    std::string name, ext;
    return splitPath(pathbuf, path, name, ext);
#elif defined(KUMA_OS_MAC)
    char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
    if(proc_pidpath(getpid(), pathbuf, sizeof(pathbuf)) <= 0) {
        return false;
    }
    std::string name, ext;
    return splitPath(pathbuf, path, name, ext);
#elif defined(KUMA_OS_LINUX)
    char pathbuf[1024] = {0};
    if (readlink("/proc/self/exe", pathbuf, sizeof(pathbuf)) < 0) {
        return false;
    }
    std::string name, ext;
    return splitPath(pathbuf, path, name, ext);
#else
    return false;
#endif
#if 0//MACOS
    char path[1024];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0)
        printf("executable path is %s\n", path);
    else
        printf("buffer too small; need size %u\n", size);
#endif
}

bool fileExist(const std::string &file)
{
    _f_stat_t buf;
    int result = f_stat(file.c_str(), &buf );
    if(result < 0 && errno == ENOENT){
        return false;
    }
    return true;
}

bool isDir(const std::string &dir)
{
    _f_stat_t buf;
    int result = f_stat(dir.c_str(), &buf );
    if(result < 0 && errno == ENOENT){
        return f_mkdir(dir.c_str(), 444) == 0;
    }else if(result == 0 && IS_DIR(buf))
        return true;
    return false;
}

std::string getMime(const std::string &ext)
{
    static const std::string s_def_mime{"application/octet-stream"};
    static struct
    {
        std::string ext;
        std::string mime;
    } s_mime_map[] = {
        {"html","text/html"},
        {"htm","text/html"},
        {"js","text/javascript"},
    };
    if(ext.empty()) {
        return s_def_mime;
    }
    int count = sizeof(s_mime_map)/sizeof(s_mime_map[0]);
    for(int i = 0; i<count; ++i) {
        if(strcasecmp(ext.c_str(), s_mime_map[i].ext.c_str()) == 0) {
            return s_mime_map[i].mime;
        }
    }
    return s_def_mime;
}
