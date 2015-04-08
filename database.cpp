//
// MusicFS :: Database
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <unordered_map>
#include <utility>
#include <regex>
#include <algorithm>
#include <assert.h>

#include <sqlite3.h>

#define MUSICFS_LOG_SUBSYS "Database"
#include "logging.h"

#include "util.h"
#include "configuration.h"
#include "musicinfo.h"
#include "dbhelpers.h"
#include "database.h"

using namespace std;
using namespace DBHelpers;

#define CHECKERR(_) CHECKERR_MSG(_, "SQL Error")
#define CHECKERR_MSG(_, msg) \
    do { \
        if ((_) != SQLITE_OK) \
        { \
            ERROR(msg << " at " __FILE__ ":" << __LINE__  << ": " << sqlite3_errmsg(m_dbHandle)); \
            throw exception(); \
        } \
    } while(0)

const int DB_SCHEMA_VERSION = 1;

static vector<string> s_tableStatements =
{
    // ON DELETE RESTRICT: referenced table's rows can't be deleted if references to them exist.
    // ON DELETE CASCADE: if referenced table's rows are deleted, deletes propogate to rows that reference them.

    "PRAGMA foreign_keys = ON;",
    "CREATE TABLE IF NOT EXISTS artist ( id INTEGER PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS album  ( id INTEGER PRIMARY KEY, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS track ( "
        "id             INTEGER PRIMARY KEY, "
        "artist_id      INTEGER NOT NULL, "
        "albumartist_id INTEGER NOT NULL, "
        "album_id       INTEGER NOT NULL, "
        "year           INTEGER NOT NULL, "
        "name           TEXT    NOT NULL COLLATE NOCASE, "
        "track          INTEGER NOT NULL, "
        "disc           TEXT    NOT NULL, "
        "FOREIGN KEY(artist_id)      REFERENCES artist(id)  ON DELETE RESTRICT, "
        "FOREIGN KEY(albumartist_id) REFERENCES artist(id)  ON DELETE RESTRICT, "
        "FOREIGN KEY(album_id)       REFERENCES album(id)   ON DELETE RESTRICT "
        ");",
    "CREATE TABLE IF NOT EXISTS file ( "
        "id             INTEGER PRIMARY KEY, "
        "track_id       INTEGER NOT NULL, "
        "path           TEXT    NOT NULL, "
        "mtime          TEXT    NOT NULL, "
        "FOREIGN KEY(track_id)      REFERENCES track(id)    ON DELETE RESTRICT "
        ");",
    "CREATE TABLE IF NOT EXISTS path ( "
        "id             INTEGER PRIMARY KEY, "
        "path           TEXT    NOT NULL UNIQUE ON CONFLICT IGNORE, "
        "track_id       INTEGER, "
        "file_id        INTEGER, "
        "parent_id      INTEGER, "
        "FOREIGN KEY(track_id)      REFERENCES track(id)    ON DELETE CASCADE, "
        "FOREIGN KEY(file_id)       REFERENCES file(id)     ON DELETE CASCADE, "
        "FOREIGN KEY(parent_id)     REFERENCES path(id)     ON DELETE CASCADE "
        ");",
    "CREATE TABLE IF NOT EXISTS config ( "
        "id                 INTEGER PRIMARY KEY, "
        "schema_version     INTEGER NOT NULL, "
        "backing_fs_paths   BLOB NOT NULL, "
        "extension_priority TEXT, "
        "path_pattern       TEXT, "
        "aliases_conf_path  TEXT "
        ");",
};

#ifdef REGEXP_SUPPORT
void sql_regexp_function(sqlite3_context *dbHandle, int nArgs, sqlite3_value **args)
{
    if (nArgs != 2)
    {
        stringstream ss;
        ss << "regexp() expects 2 arguments; " << nArgs << " given.";
        DEBUG(ss.str());
        sqlite3_result_error(dbHandle, ss.str().c_str(),  -1);
        return;
    }

    string a = reinterpret_cast<const char*>(sqlite3_value_text(args[0]));
    string b = reinterpret_cast<const char*>(sqlite3_value_text(args[1]));

    sqlite3_result_int(dbHandle, regex_match(b, regex(a)) ? 1 : 0);
}

string regex_escape(const string& s)
{
    string result;
    for (const char c : s)
    {
        switch (c)
        {
        case '^':
        case '$':
        case '\\':
        case '.':
        case '*':
        case '+':
        case '?':
        case '(':
        case ')':
        case '[':
        case ']':
        case '{':
        case '}':
        case '|':
            result.push_back('\\');
            // fall-through
        default:
            result.push_back(c);
        }
    }
    return result;
}
#endif

MusicDatabase::MusicDatabase(const char *dbFile)
    : m_dbHandle(nullptr)
    , m_path(dbFile)
{
}

void MusicDatabase::Init(Config& config, bool loadConfigFromDatabase)
{
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;

    CHECKERR_MSG(sqlite3_open_v2(m_path.c_str(), &m_dbHandle, flags, nullptr),
        "Failed to open database file \"" << m_path << "\": " << sqlite3_errmsg(m_dbHandle));

    for (size_t i = 0, n = s_tableStatements.size(); i < n; i++)
    {
        string& statement = s_tableStatements[i];
        
        CHECKERR_MSG(sqlite3_exec(m_dbHandle, statement.c_str(), nullptr, nullptr, nullptr),
            "Error in SQL table creation statement " << i);
    }

#ifdef REGEXP_SUPPORT
    CHECKERR_MSG(sqlite3_create_function_v2(
        m_dbHandle,
        "regexp",
        2,
        SQLITE_UTF8 | SQLITE_DETERMINISTIC,
        nullptr,
        &sql_regexp_function,
        nullptr,
        nullptr,
        nullptr
        ),
        "Error defining REGEXP function.");
#endif

    string stmt = "SELECT schema_version FROM config WHERE id = 0";
    sqlite3_stmt *prepared;
    CHECKERR_MSG(
        sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr),
        "Error fetching DB schema version");

    int result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        int existing_schema = GetColumn<int>(prepared, 0);

        if (existing_schema != DB_SCHEMA_VERSION)
        {
            ERROR("schema version mismatch: have " << existing_schema
                << "; need " << DB_SCHEMA_VERSION);

            cout << "The database needs to be updated.\n"
                 << m_path << " has schema version " << existing_schema << "; "
                 << DB_SCHEMA_VERSION << " is needed.\n";

            // TODO: automated schema migrations.
            // For now, though:
            cout << "Delete the database and re-run.\n";
            throw exception();
        }

        sqlite3_finalize(prepared);

        stmt = "SELECT backing_fs_paths, extension_priority, path_pattern, aliases_conf_path "
                "FROM config WHERE id = 0;";
        CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

        int result = sqlite3_step(prepared);
        if (result != SQLITE_ROW)
        {
            CHECKERR(result);
        }

        vector<string> backing_fs_paths = GetColumn<vector<string>>(prepared, 0);
        vector<string> extension_priority = GetColumn<vector<string>>(prepared, 1);
        string path_pattern = GetColumn<string>(prepared, 2);
        string aliases_conf_path = GetColumn<string>(prepared, 3);

        if (loadConfigFromDatabase)
        {
            config.backing_fs_paths = move(backing_fs_paths);
            config.extension_priority = move(extension_priority);
            config.path_pattern = move(path_pattern);
            config.aliases_conf = move(aliases_conf_path);
        }
        else
        {
            // Check if they're equal.
            //TODO
        }
    }
    else if (loadConfigFromDatabase)
    {
        cout << "No saved configuration was found in the database.\n";
        throw exception();
    }
    else
    {
        // No config row was found. This must be a newly created database.

        sqlite3_finalize(prepared);

        stmt = "INSERT INTO config (id, schema_version, backing_fs_paths, "
                    "extension_priority, path_pattern, aliases_conf_path) "
                    "VALUES(0, ?, ?, ?, ?, ?);";
        CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

        CHECKERR(BindColumn(prepared, 1, DB_SCHEMA_VERSION));
        CHECKERR(BindColumn(prepared, 2, config.backing_fs_paths));
        CHECKERR(BindColumn(prepared, 3, config.extension_priority));
        CHECKERR(BindColumn(prepared, 4, config.path_pattern));
        CHECKERR(BindColumn(prepared, 5, config.aliases_conf));

        result = sqlite3_step(prepared);
        if (result != SQLITE_DONE)
        {
            CHECKERR(result);
        }
    }
    sqlite3_finalize(prepared);
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

    CHECKERR_MSG(BindColumn(prepared, 1, value),
        "Error in binding SQL select statement for " << table);

    bool found = true;
    int result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        *outId = GetColumn<int>(prepared, 0);
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

void MusicDatabase::ClearPaths()
{
    CHECKERR_MSG(sqlite3_exec(m_dbHandle, "DELETE FROM path;", nullptr, nullptr, nullptr),
        "Error clearing out path table");
}

bool MusicDatabase::GetRealPath(const string& path, string& pathOut) const
{
    string stmt = "SELECT file.path FROM path LEFT JOIN file ON file.id = path.file_id WHERE path.path = ?;";

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
    CHECKERR(BindColumn(prepared, 1, path));

    bool found = false;
    int result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        found = true;
        pathOut = GetColumnOrDefault<string>(prepared, 0);
    }
    else if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);
    return found;
}

int MusicDatabase::GetPathId(const string& path) const
{
    string stmt = "SELECT id FROM path WHERE path = ?;";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
    CHECKERR(BindColumn(prepared, 1, path));

    int id = 0;
    int result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        id = GetColumn<int>(prepared, 0);
    }
    else if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);
    return id;
}

int MusicDatabase::AddPath(const std::string& path, int parent_id, int track_id, int file_id)
{
    // Both track_id and file_id must be zero, or both must be non-zero.
    assert((track_id == 0) == (file_id == 0));

    string stmt = "INSERT OR ABORT INTO path (path, parent_id, track_id, file_id) "
                        "VALUES (?,?,?,?);";

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    CHECKERR(BindColumn(prepared, 1, path));

    if (parent_id == 0)
        CHECKERR(BindColumn(prepared, 2, nullptr));
    else
        CHECKERR(BindColumn(prepared, 2, parent_id));

    if (track_id == 0)
        CHECKERR(BindColumn(prepared, 3, nullptr));
    else
        CHECKERR(BindColumn(prepared, 3, track_id));

    if (file_id == 0)
        CHECKERR(BindColumn(prepared, 4, nullptr));
    else
        CHECKERR(BindColumn(prepared, 4, file_id));

    int path_id = 0;
    int result = sqlite3_step(prepared);
    if (result == SQLITE_DONE)
    {
        path_id = sqlite3_last_insert_rowid(m_dbHandle);
    }
    else if (result == SQLITE_CONSTRAINT)
    {
        sqlite3_finalize(prepared);

        stmt = "SELECT id FROM path WHERE path = ?;";
        CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
        CHECKERR(BindColumn(prepared, 1, path));
        result = sqlite3_step(prepared);

        if (result == SQLITE_ROW)
        {
            path_id = GetColumn<int>(prepared, 0);
        }
        else
        {
            CHECKERR_MSG(result, "Error getting existing path row");
        }
    }
    else
    {
        CHECKERR_MSG(result, "Error adding path row");
    }
    
    sqlite3_finalize(prepared);

    return path_id;
}

vector<string> MusicDatabase::GetChildrenOfPath(
    int parent_id,
    const function<bool(const string&, const string&)>& file_preference
    ) const
{
    vector<string> results;
    unordered_map<int, vector<pair<string, string>>> files_by_track;

    string stmt = "SELECT path.path, track.id, file.path "
                    "FROM path "
                    "LEFT JOIN track ON track.id = path.track_id "
                    "LEFT JOIN file ON file.id = path.file_id "
                    "WHERE path.parent_id ";

    if (parent_id == 0)
        stmt += "IS NULL;";
    else
        stmt += "= ?;";
    
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
    
    if (parent_id != 0)
        CHECKERR(BindColumn(prepared, 1, parent_id));

    int result;
    while ((result = sqlite3_step(prepared)) == SQLITE_ROW)
    {
        const char* childPath = GetColumn<const char*>(prepared, 0);
        int track_id = GetColumn<int>(prepared, 1);
        if (track_id == 0)
        {
            // Row represents a directory, not a file. Insert into result set directly.
            results.emplace_back(childPath);
        }
        else
        {
            // Row represents a track and file. Add to the map.
            const char* filePath = GetColumn<const char*>(prepared, 2);

            if (files_by_track.find(track_id) == files_by_track.end())
                files_by_track.emplace(track_id, vector<pair<string, string>>({}));

            files_by_track[track_id].emplace_back(filePath, childPath);
        }
    }
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);

    for (auto& track_pair : files_by_track)
    {
        vector<pair<string, string>>& files = track_pair.second;
        auto pair_unpacker = [file_preference](const pair<string, string>& p1, const pair<string, string>& p2)
        {
            return file_preference(p1.first, p2.first);
        };
        stable_sort(files.begin(), files.end(), pair_unpacker);

        //DEBUG
        INFO("track id: " << track_pair.first);
        for (const auto& pair : files)
        {
            INFO(pair.first << " # " << pair.second);
        }

        // If the preference function prefers empty string to the best file path, that means nothing is selected.
        if (file_preference(files.front().first, ""))
        {
            results.emplace_back(move(files.front().second));
        }
        else
        {
            DEBUG("removed by file preference");
        }
    }

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

    result = BindColumn(prepared, 1, value);
    CHECKERR_MSG(result, "Error in binding SQL insert statement for " << table);

    result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        ERROR("unexpected SQLITE_ROW on insert statement");
        throw exception();
    }
    else if (result != SQLITE_DONE)
    {
        CHECKERR_MSG(result, "Error in executing SQL insert statement for " << table);
    }

    *outId = static_cast<int>(sqlite3_last_insert_rowid(m_dbHandle));

    sqlite3_finalize(prepared);
}

void MusicDatabase::AddTrack(const MusicInfo& attributes, string path, time_t mtime, int *out_track_id, int *out_file_id)
{
    DEBUG("Adding track: " << path);

    int artistId, albumartistId, albumId;

    if (!GetId("artist", attributes.artist(), &artistId))
    {
        DEBUG("adding artist " << attributes.artist());
        AddRow("artist", attributes.artist(), &artistId);
    }

    if (!GetId("artist", attributes.albumartist(), &albumartistId))
    {
        DEBUG("adding artist " << attributes.albumartist());
        AddRow("artist", attributes.albumartist(), &albumartistId);
    }

    if (!GetId("album", attributes.album(), &albumId))
    {
        DEBUG("adding album " << attributes.album());
        AddRow("album", attributes.album(), &albumId);
    }

    // Bind these to local strings so we control their lifetimes.
    string title = attributes.title();
    string disc = attributes.disc();

    string stmt = "SELECT id FROM track "
                    "WHERE artist_id = ? "
                        "AND albumartist_id = ? "
                        "AND album_id = ? "
                        "AND year = ? "
                        "AND name = ? "
                        "AND track = ? "
                        "AND disc = ?;";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    auto bindValues = [&]()
    {    
        CHECKERR(BindColumn(prepared, 1, artistId));
        CHECKERR(BindColumn(prepared, 2, albumartistId));
        CHECKERR(BindColumn(prepared, 3, albumId));
        CHECKERR(BindColumn(prepared, 4, attributes.year()));
        CHECKERR(BindColumn(prepared, 5, title));
        CHECKERR(BindColumn(prepared, 6, attributes.track()));
        CHECKERR(BindColumn(prepared, 7, disc));
    };

    bindValues();

    int track_id = 0;
    int result = sqlite3_step(prepared);
    if (result == SQLITE_DONE)
    {
        // No track was found with that metadata. Make one.

        sqlite3_finalize(prepared);
        stmt = "INSERT INTO track (artist_id, albumartist_id, album_id, year, name, track, disc) "
            "VALUES(?,?,?,?,?,?,?);";
        CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
        bindValues();

        result = sqlite3_step(prepared);
        if (result == SQLITE_DONE)
        {
            track_id = sqlite3_last_insert_rowid(m_dbHandle);
        }
        else
        {
            CHECKERR(result);
        }
    }
    else if (result == SQLITE_ROW)
    {
        track_id = GetColumn<int>(prepared, 0);
    }
    else
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);

    stmt = "INSERT INTO file (track_id, path, mtime) VALUES(?,?,?);";
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    CHECKERR(BindColumn(prepared, 1, track_id));
    CHECKERR(BindColumn(prepared, 2, path));
    CHECKERR(BindColumn(prepared, 3, mtime));

    result = sqlite3_step(prepared);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);

    if (out_file_id != nullptr)
        *out_file_id = sqlite3_last_insert_rowid(m_dbHandle);
    if (out_track_id != nullptr)
        *out_track_id = track_id;
}

void MusicDatabase::GetAttributes(int file_id, MusicAttributes& attrs) const
{
    string stmt = "SELECT a1.name, a2.name, album.name, t.year, t.track, t.disc, t.name, f.path "
                    "FROM file f "
                    "JOIN track t ON t.id = f.track_id "
                    "JOIN artist a1 ON a1.id = t.artist_id "
                    "JOIN artist a2 ON a2.id = t.albumartist_id "
                    "JOIN album ON album.id = t.album_id "
                    "WHERE f.id = ?;";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
    CHECKERR(BindColumn(prepared, 1, file_id));

    int result = sqlite3_step(prepared);
    if (result == SQLITE_ROW)
    {
        attrs.Artist      = GetColumn<string>(prepared, 0);
        attrs.AlbumArtist = GetColumn<string>(prepared, 1);
        attrs.Album       = GetColumn<string>(prepared, 2);

        int year = GetColumn<int>(prepared, 3);
        if (year == 0)
            attrs.Year.clear();
        else
            attrs.Year = to_string(year);

        int track = GetColumn<int>(prepared, 4);
        if (track == 0)
            attrs.Track.clear();
        else
            attrs.Track = to_string(track);

        attrs.Disc  = GetColumnOrDefault<string>(prepared, 5);
        attrs.Title = GetColumnOrDefault<string>(prepared, 6);
        attrs.Path  = GetColumnOrDefault<string>(prepared, 7);
    }
    else
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
            "WHERE track." + table + "_id = " + table + ".id";

    if (strcmp(table, "artist") == 0)
    {
        stmt += " OR track.albumartist_id = artist.id";
    }
    stmt += ");";

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

void MusicDatabase::CleanTracks()
{
    string stmt = "DELETE FROM track WHERE NOT EXISTS ("
                        "SELECT NULL "
                        "FROM file "
                        "WHERE file.track_id = track.id"
                    ")";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
    int result = sqlite3_step(prepared);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    int count = sqlite3_changes(m_dbHandle);
    if (count > 0)
    {
        DEBUG("Cleaned " << count << " tracks with no files.");
    }
}

void MusicDatabase::CleanPaths()
{
    string stmt = "DELETE "
                    "FROM path "
                    "WHERE path.track_id IS NULL "
                        "AND NOT EXISTS ("
                            "SELECT NULL "
                            "FROM path p2 "
                            "WHERE p2.parent_id = path.id);";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    int deleted_count = 0;
    int round = 1;
    do
    {
        int result = sqlite3_step(prepared);
        if (result != SQLITE_DONE)
            CHECKERR(result);

        sqlite3_reset(prepared);

        deleted_count = sqlite3_changes(m_dbHandle);
        DEBUG("CleanPaths round " << round << " removed " << deleted_count << " rows.");
        round++;

    } while (deleted_count > 0);

    sqlite3_finalize(prepared);
}

void MusicDatabase::RemoveFile(int file_id)
{
    sqlite3_stmt *prepared;
    string stmt = "DELETE FROM file WHERE id = ?;";
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));
    CHECKERR(BindColumn(prepared, 1, file_id));
    
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
}

vector<tuple<int, int, time_t, string>> MusicDatabase::GetFiles() const
{
    vector<tuple<int, int, time_t, string>> results;

    sqlite3_stmt *prepared;
    string stmt = "SELECT file.id, file.track_id, file.mtime, file.path FROM file;";
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    int result;
    while ((result = sqlite3_step(prepared)) == SQLITE_ROW)
    {
        int id = GetColumn<int>(prepared, 0);
        int track_id = GetColumn<int>(prepared, 1);
        time_t mtime = GetColumn<int64_t>(prepared, 2);
        string path = GetColumn<string>(prepared, 3);

        results.emplace_back(id, track_id, mtime, path);
    }
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);

    return results;
}

void MusicDatabase::BeginTransaction()
{
    int result = sqlite3_exec(m_dbHandle, "BEGIN;", nullptr, nullptr, nullptr);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }
}

void MusicDatabase::EndTransaction()
{
    int result = sqlite3_exec(m_dbHandle, "END;", nullptr, nullptr, nullptr);
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }
}
