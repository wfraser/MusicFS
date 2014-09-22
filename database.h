#pragma once

struct Constraints
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
#define TABLE(_x) std::vector<std::string> _x(const Constraints&) const
    TABLE(Genres);
    TABLE(Artists);
    TABLE(Albums);
    TABLE(Titles);
    TABLE(Years);
    TABLE(Tracks);

private:
    sqlite3 *m_dbHandle;
};
