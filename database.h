#pragma once

struct MusicAttributesById
{
    MusicAttributesById()
        : artist_id(-1), albumartist_id(-1), album_id(-1), genre_id(-1), year_id(-1), track_id(-1)
    {};

    int artist_id, albumartist_id, album_id, genre_id, year_id, track_id;
};

struct sqlite3;

class MusicDatabase
{
public:
    MusicDatabase(const char *dbFile);
    ~MusicDatabase();

    MusicDatabase(const MusicDatabase&) = delete;
    MusicDatabase& operator=(const MusicDatabase&) = delete;
    MusicDatabase(MusicDatabase&&) = delete;

    std::vector<std::vector<std::pair<int, std::string>>> GetValues(
        const std::vector<std::string>& columns,
        const MusicAttributesById& constraints
        ) const;

    void AddTrack(const MusicInfo& attributes, std::string filename, time_t mtime);
    void RemoveTrack(int id);
    std::vector<std::tuple<int, time_t, std::string>> GetTracks() const;

    void ClearPaths();
    bool PathExists(const std::string& path) const;
    void AddPath(const std::string& path, const MusicAttributesById& constraints);
    std::vector<std::pair<std::string, bool>> GetChildrenOfPath(const std::string& path, const MusicAttributesById& constraints) const;
    std::string GetRealPath(const std::string& path) const;
    bool GetPathAttributes(const std::string& path, MusicAttributesById& constraints) const;
    void BeginHeavyWriting();
    void EndHeavyWriting();

    void CleanTables();

private:

    bool GetId(const char *table, std::string value, int *outId);
    void AddRow(const char *table, std::string value, int *outId);
    void CleanTable(const char *table);

    sqlite3 *m_dbHandle;
};
