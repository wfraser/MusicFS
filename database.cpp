#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <utility>

#include <sqlite3.h>

#define MUSICFS_LOG_SUBSYS "Database"
extern int musicfs_log_level;
extern bool musicfs_log_stderr;
#include "logging.h"

#include "util.h"
#include "MusicInfo.h"
#include "database.h"

using namespace std;

#define CHECKERR(_) CHECKERR_MSG(_, "SQL Error")
#define CHECKERR_MSG(_, msg) \
    do { \
        if ((_) != SQLITE_OK) \
        { \
            ERROR(msg << " at " __FILE__ ":" << __LINE__  << ": " << sqlite3_errmsg(m_dbHandle)); \
            throw new exception(); \
        } \
    } while(0)

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
        "path TEXT NOT NULL, "
        "mtime INTEGER NOT NULL, "
        "FOREIGN KEY(artist_id) REFERENCES artist(id), "
        "FOREIGN KEY(album_id) REFERENCES album(id), "
        "FOREIGN KEY(genre_id) REFERENCES genre(id), "
        "FOREIGN KEY(year_id) REFERENCES year(id) "
        ");"
};

MusicDatabase::MusicDatabase(const char *dbFile)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    CHECKERR_MSG(sqlite3_open_v2(dbFile, &m_dbHandle, flags, nullptr),
        "Failed to open database file \"" << dbFile << "\": " << sqlite3_errmsg(m_dbHandle));

    for (size_t i = 0, n = s_tableStatements.size(); i < n; i++)
    {
        string& statement = s_tableStatements[i];
        
        CHECKERR_MSG(sqlite3_exec(m_dbHandle, statement.c_str(), nullptr, nullptr, nullptr),
            "Error in SQL table creation statement " << i);

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
    string stmt = "SELECT id FROM ";
    stmt.append(table);
    stmt.append(" WHERE name = ?;");
    
    sqlite3_stmt *prepared;
    CHECKERR_MSG(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr),
        "Error preparing SQL select statement for " << table << ": " << sqlite3_errmsg(m_dbHandle));

    CHECKERR_MSG(sqlite3_bind_text(prepared, 1, value.c_str(), value.size(), nullptr),
        "Error in binding SQL select statement for " << table);

    bool found = true;
    int result = sqlite3_step(prepared);
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

vector<vector<pair<int,string>>> MusicDatabase::GetValues(const vector<string>& columns, const MusicAttributes& constraints) const
{
    stringstream stmt;

    stmt << "SELECT DISTINCT ";
    for (size_t i = 0, n = columns.size(); i < n; i++)
    {
        if (columns[i] == "title")
        {
            stmt << "track.id, track.name ";
        }
        else if (columns[i] == "track")
        {
            stmt << "track.track ";
        }
        else if (columns[i] == "path")
        {
            stmt << "track.path ";
        }
        else
        {
            stmt << columns[i] << ".id, " << columns[i] << ".name ";
        }
        if (i != n - 1)
            stmt << ", ";
    }
    stmt << "FROM track ";

    unordered_set<string> joined_tables;
    for (const string& column : columns)
    {
        if (column != "title"
            && column != "track"
            && column != "filename")
        {
            joined_tables.insert(column);
        }
    }

    vector<string> where_clauses;

#define join_table(_) \
    if (constraints._ != nullptr) \
    { \
        joined_tables.insert(#_); \
        where_clauses.push_back(#_ ".name = $" #_); \
    }

    join_table(artist)
    join_table(album)
    join_table(genre)
    join_table(year)

    if (constraints.track != nullptr)
    {
        // No join necessary; it's a column on the track table.
        where_clauses.push_back("track.track = ?track");
    }

    for (const auto& table : joined_tables)
    {
        stmt << "JOIN " << table << " ON " << table << ".id = track." << table << "_id ";
    }

    stmt << " WHERE " << join(where_clauses, " AND ") << ";";

    DEBUG("GetTable: " << stmt.str());

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.str().c_str(), stmt.str().size(), &prepared, nullptr));

#define bind_text(_) \
    if (constraints._ != nullptr) \
    { \
        int index = sqlite3_bind_parameter_index(prepared, "$" #_); \
        if (constraints._->empty()) \
        { \
            CHECKERR(sqlite3_bind_null(prepared, index)); \
        } \
        else \
        { \
            CHECKERR(sqlite3_bind_text(prepared, index, constraints._->c_str(), constraints._->size(), nullptr)); \
        } \
    }

    bind_text(artist)
    bind_text(album)
    bind_text(genre)
    bind_text(year)

    if (constraints.track != nullptr)
    {
        // Track is the only one that's an int.
        int pos = sqlite3_bind_parameter_index(prepared, "track");
        if (constraints.track->empty())
        {
            CHECKERR(sqlite3_bind_null(prepared, pos));
        }
        else
        {
            int t = atoi(constraints.track->c_str());
            CHECKERR(sqlite3_bind_int(prepared, pos, t));
        }
    }

    vector<vector<pair<int, string>>> results;
    int result;
    while ((result = sqlite3_step(prepared)) == SQLITE_ROW)
    {
        results.emplace_back();
        for (size_t i = 0, j = 0, n = columns.size(); i < n; i++)
        {
            int id;
            if (columns[i] == "track" || columns[i] == "path")
                id = -1;
            else
            {
                id = sqlite3_column_int(prepared, j++);
            }

            string value = reinterpret_cast<const char*>(sqlite3_column_text(prepared, j++));
            results.back().emplace_back(id, value);
        }
    }
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);

    return results;
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
    else if (result != SQLITE_DONE)
    {
        CHECKERR_MSG(result, "Error in executing SQL insert statement for " << table);
    }

    *outId = static_cast<int>(sqlite3_last_insert_rowid(m_dbHandle));

    sqlite3_finalize(prepared);
}

void MusicDatabase::AddTrack(const MusicInfo& attributes, string path, time_t mtime)
{
    DEBUG("Adding track: " << path);

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

    string trackStmt = "INSERT INTO track (artist_id, album_id, genre_id, year_id, name, track, path, mtime) "
        "VALUES(?,?,?,?,?,?,?,?);";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, trackStmt.c_str(), trackStmt.size(), &prepared, nullptr));

    string title = attributes.title();

    CHECKERR(sqlite3_bind_int(prepared, 1, artistId));
    CHECKERR(sqlite3_bind_int(prepared, 2, albumId));
    CHECKERR(sqlite3_bind_int(prepared, 3, genreId));
    CHECKERR(sqlite3_bind_int(prepared, 4, yearId));
    CHECKERR(sqlite3_bind_text(prepared, 5, title.c_str(), title.size(), nullptr));
    CHECKERR(sqlite3_bind_int(prepared, 6, attributes.track()));
    CHECKERR(sqlite3_bind_text(prepared, 7, path.c_str(), path.size(), nullptr));
    CHECKERR(sqlite3_bind_int(prepared, 8, mtime));

    int result = sqlite3_step(prepared);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);
}

void MusicDatabase::CleanTable(const char *table)
{
    sqlite3_stmt *prepared;
    string stmt = string(
        "DELETE FROM ") + table + " "
        "WHERE NOT EXISTS ("
            "SELECT NULL "
            "FROM track "
            "WHERE track." + table + "_id = " + table + ".id);";
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    int result = sqlite3_step(prepared);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    int count = sqlite3_changes(m_dbHandle);
    if (count > 0)
    {
        DEBUG("Cleaned " << count << " entries from " << table << " table.");
    }
}

void MusicDatabase::RemoveTrack(int trackId)
{
    sqlite3_stmt *prepared;
    string stmt = "DELETE FROM track WHERE id = ?;";
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
    CHECKERR(sqlite3_bind_int(prepared, 1, trackId));
    
    int result = sqlite3_step(prepared);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }
    
    sqlite3_finalize(prepared);
}

void MusicDatabase::CleanTables()
{
    CleanTable("artist");
    CleanTable("album");
    CleanTable("genre");
    CleanTable("year");
}

vector<tuple<int, time_t, string>> MusicDatabase::GetTracks() const
{
    vector<tuple<int, time_t, string>> results;

    sqlite3_stmt *prepared;
    string stmt = "SELECT id, mtime, path FROM track;";
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    int result;
    while ((result = sqlite3_step(prepared)) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(prepared, 0);
        time_t mtime = sqlite3_column_int64(prepared, 1);
        string path = reinterpret_cast<const char*>(sqlite3_column_text(prepared, 2));

        results.emplace_back(id, mtime, path);
    }
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);

    return results;
}

void MusicDatabase::BeginHeavyWriting()
{
    int result = sqlite3_exec(m_dbHandle, "BEGIN;", nullptr, nullptr, nullptr);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }
}

void MusicDatabase::EndHeavyWriting()
{
    int result = sqlite3_exec(m_dbHandle, "END;", nullptr, nullptr, nullptr);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }
}
