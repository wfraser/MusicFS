#include <iostream>
#include <string>
#include <vector>
#include <memory>

#include <sqlite3.h>

#define MUSICFS_LOG_SUBSYS "Database"
extern int musicfs_log_level;
extern bool musicfs_log_stderr;
#include "logging.h"

#include "MusicInfo.h"
#include "database.h"

using namespace std;

static vector<string> s_tableStatements =
{
    "PRAGMA foreign_keys = ON;"
    "CREATE TABLE IF NOT EXISTS artist ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS album  ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS genre  ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS year   ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS track ( "
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "artist_id INTEGER NOT NULL, "
        "album_id INTEGER NOT NULL, "
        "genre_id INTEGER NOT NULL, "
        "year_id INTEGER NOT NULL, "
        "name TEXT NOT NULL, "
        "track INTEGER, "
        "filename TEXT NOT NULL, "
            "FOREIGN KEY(artist_id) REFERENCES artist(id), "
            "FOREIGN KEY(album_id) REFERENCES album(id), "
            "FOREIGN KEY(genre_id) REFERENCES genre(id), "
            "FOREIGN KEY(year_id) REFERENCES year(id) "
        ");"
};

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

    for (size_t i = 0, n = s_tableStatements.size(); i < n; i++)
    {
        string& statement = s_tableStatements[i];
        
        /*
        sqlite3_stmt *prepared;
        int result = sqlite3_prepare_v2(m_dbHandle, statement.c_str(), statement.size(), &prepared, nullptr);
        if (result != S_OK)
        {
            ERROR("Error in SQL table creation statement " << i << ": " << result);
            throw new exception("SQL error");
        }
        */

        char *errMsg;
        int result = sqlite3_exec(m_dbHandle, statement.c_str(), nullptr, nullptr, &errMsg);
        if (result != SQLITE_OK)
        {
            ERROR("Error in SQL table creation statement " << i << ": (" << result << ") " << errMsg);
            throw new exception();
        }

        /*
        if (sqlite3_changes(m_dbHandle) == 0)
        {
            // Nothing got changed by this statement. We must already have tables. Skip the rest.
            break;
        }
        */
    }
}

MusicDatabase::~MusicDatabase()
{
    sqlite3_close(m_dbHandle);
}

bool MusicDatabase::GetId(const char *table, std::string value, int *outId)
{
    int result = 0;

    string stmt = "SELECT id FROM ";
    stmt.append(table);
    stmt.append(" WHERE name = ?;");
    
    sqlite3_stmt *prepared;
    result = sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr);
    if (result != SQLITE_OK)
    {
        ERROR("Error preparing SQL select statement for " << table << ": " << sqlite3_errmsg(m_dbHandle));
        throw new exception();
    }

#define CHECKERR(_) CHECKERR_MSG(_, "SQL Error")
#define CHECKERR_MSG(_, msg) \
    do { \
        if ((_) != SQLITE_OK) \
        { \
            ERROR(msg << " at " __FILE__ ":" << __LINE__  << ": " << sqlite3_errmsg(m_dbHandle)); \
            throw new exception(); \
        } \
    } while(0)

    result = sqlite3_bind_text(prepared, 1, value.c_str(), value.size(), nullptr);
    CHECKERR_MSG(result, "Error in binding SQL select statement for " << table);

    bool found = true;
    result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        *outId = sqlite3_column_int(prepared, 0);
    }
    else if (result == SQLITE_DONE)
    {
        found = false;
    }
    else
    {
        CHECKERR_MSG(result, "Error in executing SQL select statement for " << table);
    }

    sqlite3_finalize(prepared);

    return found;
}

void MusicDatabase::AddRow(const char *table, std::string value, int *outId)
{
    int result = 0;

    string stmt = "INSERT INTO ";
    stmt.append(table);
    stmt.append(" ( name ) VALUES ( ? );");
    
    sqlite3_stmt *prepared;
    result = sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr);
    CHECKERR_MSG(result, "Error in preparing SQL insert statement for " << table);

    result = sqlite3_bind_text(prepared, 1, value.c_str(), value.size(), nullptr);
    CHECKERR_MSG(result, "Error in binding SQL insert statement for " << table);

    result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        ERROR("unexpected SQLITE_ROW on insert statement");
        throw new exception();
    }
    else if (result == SQLITE_DONE)
    {
        DEBUG("added okay");
    }
    else
    {
        CHECKERR_MSG(result, "Error in executing SQL insert statement for " << table);
    }

    *outId = static_cast<int>(sqlite3_last_insert_rowid(m_dbHandle));

    sqlite3_finalize(prepared);
}

void MusicDatabase::AddTrack(const MusicInfo& attributes, string filename)
{
    int artistId, albumId, genreId, yearId;

    if (!GetId("artist", attributes.artist(), &artistId))
    {
        DEBUG("adding artist " << attributes.artist());
        AddRow("artist", attributes.artist(), &artistId);
    }

    if (!GetId("album", attributes.album(), &albumId))
    {
        DEBUG("adding album " << attributes.album());
        AddRow("album", attributes.album(), &albumId);
    }

    if (!GetId("genre", attributes.genre(), &genreId))
    {
        DEBUG("adding genre " << attributes.genre());
        AddRow("genre", attributes.genre(), &genreId);
    }

    string year = (attributes.year() == 0) ? "" : to_string(attributes.year());
    if (!GetId("year", year, &yearId))
    {
        DEBUG("adding year " << attributes.year());
        AddRow("year", year, &yearId);
    }

    string trackStmt = "INSERT INTO track (artist_id, album_id, genre_id, year_id, name, track, filename) VALUES(?,?,?,?,?,?,?);";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, trackStmt.c_str(), trackStmt.size(), &prepared, nullptr));

    string title = attributes.title();

    CHECKERR(sqlite3_bind_int(prepared, 1, artistId));
    CHECKERR(sqlite3_bind_int(prepared, 2, albumId));
    CHECKERR(sqlite3_bind_int(prepared, 3, genreId));
    CHECKERR(sqlite3_bind_int(prepared, 4, yearId));
    CHECKERR(sqlite3_bind_text(prepared, 5, title.c_str(), title.size(), nullptr));
    CHECKERR(sqlite3_bind_int(prepared, 6, attributes.track()));
    CHECKERR(sqlite3_bind_text(prepared, 7, filename.c_str(), filename.size(), nullptr));

    int result = sqlite3_step(prepared);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);
}
