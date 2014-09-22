#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include <sqlite3.h>

#define MUSICFS_LOG_SUBSYS "Database"
extern int musicfs_log_level;
extern bool musicfs_log_stderr;
#include "logging.h"

#include "database.h"

using namespace std;

MusicDatabase::MusicDatabase(const char *dbFile)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    int result = sqlite3_open_v2(dbFile, &m_dbHandle, flags, nullptr);
    if (result != 0)
    {
        ERROR("Failed to open database file \"" << dbFile << "\": " << sqlite3_errmsg(m_dbHandle));
        sqlite3_close(m_dbHandle);
        throw new exception();
    }
}

MusicDatabase::~MusicDatabase()
{
    sqlite3_close(m_dbHandle);
}
