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

#undef TABLE
#define TABLE(_x) std::vector<std::string> _x(const MusicAttributes& constraints) const
    TABLE(Genres);
    TABLE(Artists);
    TABLE(Albums);
    TABLE(Titles);
    TABLE(Years);
    TABLE(Tracks);

    void AddTrack(const MusicInfo& attributes, std::string filename);

private:
    bool GetId(const char *table, std::string value, int *outId);
    void AddRow(const char *table, std::string value, int *outId);

    sqlite3 *m_dbHandle;
};
