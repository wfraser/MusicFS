#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <unordered_set>
#include <utility>
#include <regex>

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
    // ON DELETE RESTRICT: referenced table's rows can't be deleted if references to them exist.
    // ON DELETE CASCADE: if referenced table's rows are deleted, deletes propogate to rows that reference them.

    "PRAGMA foreign_keys = ON;",
    "CREATE TABLE IF NOT EXISTS artist ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS album  ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS genre  ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS year   ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL COLLATE NOCASE );",
    "CREATE TABLE IF NOT EXISTS track ( "
        "id             INTEGER PRIMARY KEY AUTOINCREMENT, "
        "artist_id      INTEGER NOT NULL, "
        "albumartist_id INTEGER NOT NULL, "
        "album_id       INTEGER NOT NULL, "
        "genre_id       INTEGER NOT NULL, "
        "year_id        INTEGER NOT NULL, "
        "name           TEXT    NOT NULL, "
        "track          INTEGER, "
        "path           TEXT    NOT NULL, "
        "mtime          INTEGER NOT NULL, "
        "FOREIGN KEY(artist_id)      REFERENCES artist(id)  ON DELETE RESTRICT, "
        "FOREIGN KEY(albumartist_id) REFERENCES artist(id)  ON DELETE RESTRICT, "
        "FOREIGN KEY(album_id)       REFERENCES album(id)   ON DELETE RESTRICT, "
        "FOREIGN KEY(genre_id)       REFERENCES genre(id)   ON DELETE RESTRICT, "
        "FOREIGN KEY(year_id)        REFERENCES year(id)    ON DELETE RESTRICT "
        ");",
    "CREATE TABLE IF NOT EXISTS path ( "
        "id             INTEGER PRIMARY KEY AUTOINCREMENT, "
        "path           TEXT    NOT NULL UNIQUE ON CONFLICT IGNORE, "
        "artist_id      INTEGER, "
        "albumartist_id INTEGER, "
        "album_id       INTEGER, "
        "genre_id       INTEGER, "
        "year_id        INTEGER, "
        "track_id       INTEGER, "
        "FOREIGN KEY(artist_id)      REFERENCES artist(id)  ON DELETE CASCADE, "
        "FOREIGN KEY(albumartist_id) REFERENCES artist(id)  ON DELETE CASCADE, "
        "FOREIGN KEY(album_id)       REFERENCES album(id)   ON DELETE CASCADE, "
        "FOREIGN KEY(genre_id)       REFERENCES genre(id)   ON DELETE CASCADE, "
        "FOREIGN KEY(year_id)        REFERENCES year(id)    ON DELETE CASCADE, "
        "FOREIGN KEY(track_id)       REFERENCES track(id)   ON DELETE CASCADE "
        ");"
};

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

void MusicDatabase::ClearPaths()
{
    CHECKERR_MSG(sqlite3_exec(m_dbHandle, "DELETE FROM path;", nullptr, nullptr, nullptr),
        "Error clearing out path table");
}

void MusicDatabase::AddPath(const std::string& path, const MusicAttributesById& constraints)
{
    string stmt = "INSERT INTO path (path, artist_id, albumartist_id, album_id, genre_id, year_id, track_id) "
                        "VALUES (?,?,?,?,?,?,?);";

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    CHECKERR(sqlite3_bind_text(prepared, 1, path.c_str(), path.size(), nullptr));

#define bind_int(_pos, _value) \
    if (_value == -1) \
        CHECKERR(sqlite3_bind_null(prepared, _pos)); \
    else \
        CHECKERR(sqlite3_bind_int(prepared, _pos, _value))

    bind_int(2, constraints.artist_id);
    bind_int(3, constraints.albumartist_id);
    bind_int(4, constraints.album_id);
    bind_int(5, constraints.genre_id);
    bind_int(6, constraints.year_id);
    bind_int(7, constraints.track_id);

#undef bind_int

    int result = sqlite3_step(prepared);
    if (result != SQLITE_DONE)
    {
        CHECKERR_MSG(result, "Error adding path row");
    }

    sqlite3_finalize(prepared);
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

vector<pair<string, bool>> MusicDatabase::GetChildrenOfPath(const string& path, const MusicAttributesById& constraints) const
{
    string stmt = "SELECT path, track_id FROM path WHERE ";

    // I know it's better to use bound values, but these are just ints, and it would make the code
    // a lot more complex to do it that way.
#define where(_) \
    if (constraints._ == 0) \
        stmt += "path." #_ " IS NULL AND "; \
    else if (constraints._ != -1) \
        stmt += "path." #_ " = " + to_string(constraints._) + " AND "

    where(artist_id);
    where(albumartist_id);
    where(album_id);
    where(genre_id);
    where(year_id);
    where(track_id);
    
    stmt += " path REGEXP ?;";

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    stringstream regex;
    regex << '^';
    if (path != "/")
    {
        // The root directory is a special case; don't add the extra slash.
        regex << regex_escape(path);
    }
    regex << "/[^/]+";

    string regex_str = regex.str();
    CHECKERR(sqlite3_bind_text(prepared, 1, regex_str.c_str(), regex_str.size(), nullptr));
    
    DEBUG(__FUNCTION__ << ": " << stmt << " " << regex.str());

    vector<pair<string, bool>> results;
    int result;
    while ((result = sqlite3_step(prepared)) == SQLITE_ROW)
    {
        string childPath = reinterpret_cast<const char*>(sqlite3_column_text(prepared, 0));
        bool isFile = sqlite3_column_type(prepared, 1) != SQLITE_NULL;

        results.emplace_back(childPath, isFile);
    }
    if (result != SQLITE_DONE)
    {
        CHECKERR(result);
    }

    sqlite3_finalize(prepared);

    return results;
}

string MusicDatabase::GetRealPath(const string& path) const
{
    string stmt = "SELECT track.path FROM track JOIN path ON path.track_id = track.id WHERE path.path = ?;";

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    CHECKERR(sqlite3_bind_text(prepared, 1, path.c_str(), path.size(), nullptr));

    int result = sqlite3_step(prepared);
    if (result == SQLITE_DONE)
    {
        return "";
    }
    else if (result == SQLITE_ROW)
    {
        string value = reinterpret_cast<const char*>(sqlite3_column_text(prepared, 0));
        sqlite3_finalize(prepared);
        return value;
    }
    else
    {
        CHECKERR(result);
        sqlite3_finalize(prepared);
        return "";
    }
}

bool MusicDatabase::GetPathAttributes(const string& path, MusicAttributesById& constraints) const
{
    string stmt = "SELECT path.artist_id, path.albumartist_id, path.album_id, path.genre_id, path.year_id, path.track_id "
        " FROM path WHERE path.path = ?;";

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.c_str(), stmt.size(), &prepared, nullptr));

    CHECKERR(sqlite3_bind_text(prepared, 1, path.c_str(), path.size(), nullptr));
    
    int result = sqlite3_step(prepared);
    if (result == SQLITE_DONE)
    {
        sqlite3_finalize(prepared);
        return false;
    }
    else if (result == SQLITE_ROW)
    {
#define intnull(_index, _name) \
        if (sqlite3_column_type(prepared, _index) == SQLITE_NULL) \
            constraints._name = -1; \
        else \
            constraints._name = sqlite3_column_int(prepared, _index)

        intnull(0, artist_id);
        intnull(1, albumartist_id);
        intnull(2, album_id);
        intnull(3, genre_id);
        intnull(4, year_id);
        intnull(5, track_id);

#undef intnull

        sqlite3_finalize(prepared);
        return true;
    }
    else
    {
        CHECKERR(result);
        sqlite3_finalize(prepared);
        return false;
    }
}

vector<vector<pair<int,string>>> MusicDatabase::GetValues(const vector<string>& columns, const MusicAttributesById& constraints) const
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
        else if (columns[i] == "albumartist")
        {
            stmt << "artist.id, artist.name ";
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
        if (column == "albumartist")
        {
            joined_tables.insert("artist");
        }
        else if (column != "title"
            && column != "track"
            && column != "path")
        {
            joined_tables.insert(column);
        }
    }

    vector<string> where_clauses;

#define add_where_clause(_) \
    if (constraints._##_id != -1) \
    { \
        where_clauses.push_back("track." #_ "_id = $" #_); \
    }

    add_where_clause(artist)
    add_where_clause(albumartist);
    add_where_clause(album)
    add_where_clause(genre)
    add_where_clause(year)

#undef add_where_clause

    for (const auto& table : joined_tables)
    {
        stmt << "JOIN " << table << " ON " << table << ".id = track." << table << "_id ";
    }

    if (where_clauses.size() > 0)
        stmt << " WHERE " << join(where_clauses, " AND ");
    stmt << ";";

    DEBUG("GetValues(id): " << stmt.str());

    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, stmt.str().c_str(), stmt.str().size(), &prepared, nullptr));

#define bind_text(_) \
    if (constraints._##_id != -1) \
    { \
        int index = sqlite3_bind_parameter_index(prepared, "$" #_); \
        CHECKERR(sqlite3_bind_int(prepared, index, constraints._##_id)); \
    }

    bind_text(artist)
    bind_text(albumartist)
    bind_text(album)
    bind_text(genre)
    bind_text(year)

#undef bind_text

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

    int artistId, albumartistId, albumId, genreId, yearId;

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

    string trackStmt = "INSERT INTO track (artist_id, albumartist_id, album_id, genre_id, year_id, name, track, path, mtime) "
        "VALUES(?,?,?,?,?,?,?,?,?);";
    sqlite3_stmt *prepared;
    CHECKERR(sqlite3_prepare_v2(m_dbHandle, trackStmt.c_str(), trackStmt.size(), &prepared, nullptr));

    string title = attributes.title();

    CHECKERR(sqlite3_bind_int(prepared, 1, artistId));
    CHECKERR(sqlite3_bind_int(prepared, 2, albumartistId));
    CHECKERR(sqlite3_bind_int(prepared, 3, albumId));
    CHECKERR(sqlite3_bind_int(prepared, 4, genreId));
    CHECKERR(sqlite3_bind_int(prepared, 5, yearId));
    CHECKERR(sqlite3_bind_text(prepared, 6, title.c_str(), title.size(), nullptr));
    CHECKERR(sqlite3_bind_int(prepared, 7, attributes.track()));
    CHECKERR(sqlite3_bind_text(prepared, 8, path.c_str(), path.size(), nullptr));
    CHECKERR(sqlite3_bind_int(prepared, 9, mtime));

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
