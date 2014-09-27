#pragma once

struct MusicAttributes
{
    std::unique_ptr<std::string>
        artist,
        album,
        genre,
        title,
        year,
        track;
};

struct MusicAttributesById
{
    int artist_id, album_id, genre_id, year_id, track_id;
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
        const MusicAttributes& constraints
        ) const;

    std::vector<std::vector<std::pair<int, std::string>>> GetValues(
        const std::vector<std::string>& columns,
        const MusicAttributesById& constraints
        ) const;

    void AddTrack(const MusicInfo& attributes, std::string filename, time_t mtime);
    void RemoveTrack(int id);
    std::vector<std::tuple<int, time_t, std::string>> GetTracks() const;

    void ClearPaths();
    void AddPath(const std::string& path, const MusicAttributesById& constraints);

    void BeginHeavyWriting();
    void EndHeavyWriting();

    void CleanTables();

private:

    bool GetId(const char *table, std::string value, int *outId);
    void AddRow(const char *table, std::string value, int *outId);
    void CleanTable(const char *table);

    sqlite3 *m_dbHandle;
};
