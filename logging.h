//
// MusicFS :: Logging Macros
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#pragma once

extern int musicfs_log_level;
extern bool musicfs_log_stderr;

#include <string.h> // for strerror
#include <errno.h> // for errno

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

#define CONLOG(_level, ...) \
    if (musicfs_log_level >= LOG_LEVEL_##_level) \
        std::cerr << "MusicFS" _MUSICFS_LOG_SUBSYS " " #_level ": " << __VA_ARGS__ << std::endl

#define CONPERROR(...) \
    if (musicfs_log_level >= LOG_LEVEL_ERROR) \
        std::cerr << "MusicFS" _MUSICFS_LOG_SUBSYS ": " << __VA_ARGS__ << ": " << strerror(errno) << std::endl;

#ifndef NOSYSLOG
#include <syslog.h>
#include <sstream>

#define LOG_ERROR LOG_ERR

#define LOGMETA(_level, ...) \
    do { \
        if (musicfs_log_stderr) { \
            CONLOG(_level, __VA_ARGS__); \
        } else if (musicfs_log_level >= LOG_LEVEL_##_level) { \
            std::stringstream ss; \
            ss << MUSICFS_LOG_SUBSYS_ #_level ": " << __VA_ARGS__; \
            syslog(LOG_##_level, "%s", ss.str().c_str()); \
        } \
    } while(0)

#define ERROR(...)  LOGMETA(ERROR, __VA_ARGS__)
#define WARN(...)   LOGMETA(WARNING, __VA_ARGS__)
#define INFO(...)   LOGMETA(INFO, __VA_ARGS__)
#define DEBUG(...)  LOGMETA(DEBUG, __VA_ARGS__)
#define PERROR(...) \
    do { \
        if (musicfs_log_stderr) { \
            CONPERROR(__VA_ARGS__); \
        } else if (musicfs_log_level >= LOG_LEVEL_ERROR) { \
            std::stringstream ss; \
            ss << MUSICFS_LOG_SUBSYS_ "ERROR: " << __VA_ARGS__ << ": " << strerror(errno) << endl; \
            syslog(LOG_ERROR, "%s", ss.str().c_str()); \
        } \
    } while (0)

#else // NOSYSLOG

#define ERROR(...)  CONLOG(ERRROR, __VA_ARGS__)
#define WARN(...)   CONLOG(WARNING, __VA_ARGS__)
#define INFO(...)   CONLOG(INFO, __VA_ARGS__)
#define DEBUG(...)  CONLOG(DEBUG, __VA_ARGS__)
#define PERROR(...) CONPERROR(__VA_ARGS__)

#endif // NOSYSLOG

#else // NODEBUG

#define ERROR(...)
#define WARN(...)
#define INFO(...)
#define DEBUG(...)
#define PERROR(...)

#endif // NODEBUG
