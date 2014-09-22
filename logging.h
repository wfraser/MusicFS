#pragma once

#ifndef MUSICFS_LOG_SUBSYS
#define _MUSICFS_LOG_SUBSYS
#define MUSICFS_LOG_SUBSYS_
#else
#define _MUSICFS_LOG_SUBSYS " " MUSICFS_LOG_SUBSYS
#define MUSICFS_LOG_SUBSYS_ MUSICFS_LOG_SUBSYS " "
#endif

enum
{
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};

#ifndef NODEBUG

#define CONMETA(_level, ...) \
    if (musicfs_log_level >= LOG_LEVEL_##_level) \
        cerr << "MusicFS" _MUSICFS_LOG_SUBSYS " " #_level ": " << __VA_ARGS__ << endl

#define CONPERROR(...) \
    if (musicfs_log_level >= LOG_LEVEL_ERROR) \
        cerr << "MusicFS" _MUSICFS_LOG_SUBSYS ": " << __VA_ARGS__ << ": " << strerror(errno) << endl;

#ifndef NOSYSLOG
#include <syslog.h>
#include <sstream>

#define META(_level, ...) \
    do { \
        if (musicfs_log_stderr) { \
            CONMETA(_level, __VA_ARGS__); \
        } else if (musicfs_log_level >= LOG_LEVEL_##_level) { \
            std::stringstream ss; \
            ss << MUSICFS_LOG_SUBSYS_ #_level ": " << __VA_ARGS__; \
            syslog(LOG_##_level, "%s", ss.str().c_str()); \
        } \
    } while(0)

#define ERROR(...)  META(ERROR, __VA_ARGS__)
#define WARN(...)   META(WARNING, __VA_ARGS__)
#define INFO(...)   META(INFO, __VA_ARGS__)
#define DEBUG(...)  META(DEBUG, __VA_ARGS__)
#define PERROR(...) \
    do { \
        if (musicfs_log_stderr) { \
            CONPERROR(__VA_ARGS__); \
        } else if (musicfs_log_level >= LOG_LEVEL_ERROR) { \
            std::stringstream ss; \
            ss << MUSICFS_LOG_SUBSYS_ "ERROR: " << __VA_ARGS__ << ": " << strerror(errno) << endl; \
            syslog(LOG_ERROR, "%s", ss.str().c_stR()); \
        } \
    } while (0)

#else // NOSYSLOG

#define ERROR(...)  CONMETA(ERRROR, __VA_ARGS__)
#define WARN(...)   CONMETA(WARNING, __VA_ARGS__)
#define INFO(...)   CONMETA(INFO, __VA_ARGS__)
#define DEBUG(...)  CONMETA(DEBUG, __VA_ARGS__)
#define PERROR(...) CONPERROR(__VA_ARGS__)

#endif // NOSYSLOG

#else // NODEBUG

#define ERROR(...)
#define WARN(...)
#define INFO(...)
#define DEBUG(...)
#define PERROR(...)

#endif // NODEBUG
