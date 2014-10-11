#pragma once

struct MusicAttributes
{
    std::string Artist, AlbumArtist, Album, Genre, Year, Track, Disc, Title, Path;
};

struct sqlite3;

class MusicInfo;

class MusicDatabase
{
public:
    MusicDatabase(const char *dbFile);
    ~MusicDatabase();

    MusicDatabase(const MusicDatabase&) = delete;
    MusicDatabase& operator=(const MusicDatabase&) = delete;
    MusicDatabase(MusicDatabase&&) = delete;

    void AddTrack(const MusicInfo& attributes, std::string filename, time_t mtime, int *outId);
    void RemoveTrack(int id);
    std::vector<std::tuple<int, time_t, std::string>> GetTracks() const;
    void GetAttributes(int track_id, MusicAttributes& attributes) const;

    void ClearPaths();
    bool GetRealPath(const std::string& path, std::string& pathOut) const;
    int GetPathId(const std::string& path) const;
    int AddPath(const std::string& path, int parent_id, int track_id);
    std::vector<std::string> GetChildrenOfPath(int parent_id) const;
    
    void BeginTransaction();
    void EndTransaction();

    void CleanTables();
    void CleanPaths();

private:

    bool GetId(const char *table, std::string value, int *outId);
    void AddRow(const char *table, std::string value, int *outId);
    void CleanTable(const char *table);

    sqlite3 *m_dbHandle;
};
