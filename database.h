#pragma once

struct MusicAttributes
{
    std::unique_ptr<std::string> genre,
        artist,
        album,
        title,
        year,
        track;
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

    std::vector<std::vector<std::string>> GetTables(
        const std::vector<std::string>& tables,
        const MusicAttributes& constraints
        ) const;

    void AddTrack(const MusicInfo& attributes, std::string filename, time_t mtime);
    void RemoveTrack(int id);
    std::vector<std::tuple<int, time_t, std::string>> GetTracks() const;

    void BeginHeavyWriting();
    void EndHeavyWriting();

private:

    bool GetId(const char *table, std::string value, int *outId);
    void AddRow(const char *table, std::string value, int *outId);
    void CleanTable(const char *table);

    sqlite3 *m_dbHandle;
};
