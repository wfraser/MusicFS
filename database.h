//
// MusicFS :: Database
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#pragma once

struct MusicAttributes
{
    std::string Artist, AlbumArtist, Album, Year, Track, Disc, Title, Path;
};

struct sqlite3;

class MusicInfo;
class Config;

class MusicDatabaseTransaction
{
public:
    MusicDatabaseTransaction(sqlite3* databaseHandle);
    ~MusicDatabaseTransaction();
    MusicDatabaseTransaction& operator=(MusicDatabaseTransaction other);

    void Commit();
    void Discard();

private:
    sqlite3* m_dbHandle;
    bool m_dismissed;
};

class MusicDatabase
{
public:
    MusicDatabase(const char *dbFile);
    ~MusicDatabase();

    void Init(
            Config& config,
            bool load_config_from_db,
            bool* out_paths_need_rebuilding
            );

    MusicDatabase(const MusicDatabase&) = delete;
    MusicDatabase& operator=(const MusicDatabase&) = delete;
    MusicDatabase(MusicDatabase&&) = delete;

    void AddTrack(
            const MusicInfo& attributes,
            std::string filename,
            time_t mtime,
            int *out_track_id,
            int *out_file_id
            );
    void RemoveFile(int id);
    std::vector<std::tuple<int, int, time_t, std::string>> GetFiles() const;
    void GetAttributes(int file_id, MusicAttributes& attributes) const;

    void ClearPaths();
    bool GetRealPath(const std::string& path, std::string& pathOut) const;
    int GetPathId(const std::string& path) const;
    int AddPath(const std::string& path, int parent_id, int track_id, int file_id);
    std::vector<std::string> GetChildrenOfPath(
            int parent_id,
            const std::function<bool(const std::string&, const std::string&)>& file_preference
            ) const;

    std::vector<std::pair<int, int>> GetAllTrackFileIds() const;
    
    MusicDatabaseTransaction BeginTransaction();

    void CleanTables();
    void CleanPaths();
    void CleanTracks();

private:

    bool GetId(const char *table, std::string value, int *outId);
    void AddSimpleRow(const char *table, std::string value, int *outId);
    void CleanTable(const char *table);

    sqlite3 *m_dbHandle;
    std::string m_path;
};
