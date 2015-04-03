//
// MusicFS :: File System Scanner ("Groveler")
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#include <string>
#include <memory>
#include <vector>
#include <deque>
#include <unordered_map>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#define MUSICFS_LOG_SUBSYS "Groveler"
#include "logging.h"

#include "musicinfo.h"
#include "database.h"
#include "path_pattern.h"
#include "aliases.h"
#include "groveler.h"

using namespace std;

static bool equals_nocase(char a, char b)
{
    return ::tolower(a) == ::tolower(b);
}

static bool file_extension_filter(const string& path)
{
    // TODO: limit grovel results to filetypes we care about
    // this will potentially save MusicInfo from doing a bunch of work

    static const vector<string> s_extensions {
        "mp3", "flac", "wma", "m4a", "mp4", "ogg"
    };

    for (const string& ext : s_extensions)
    {
        auto end = path.end();
        auto begin = end - ext.length();

        if ((begin >= path.begin())
                && (*(begin - 1) == '.')
                && equal(begin, end, ext.begin(), equals_nocase))
        {
            return true;
        }
    }

    return false;
}

vector<pair<int,int>> grovel(const vector<string>& base_paths, MusicDatabase& db)
{
    deque<string> directories;
    directories.assign(base_paths.begin(), base_paths.end());

    list<string> files;

    // First, take inventory of all the files in here.

    INFO("Enumerating files & directories.");

    size_t directory_count = 0;
    while (!directories.empty())
    {
        string path = directories.front();
        directories.pop_front();

        DEBUG("directory: " << path);
            
        DIR* dir = opendir(path.c_str());
        if (dir == nullptr)
        {
            PERROR("error opening directory \"" << path << "\"");
            continue;
        }

        dirent *e;
        while ((e = readdir(dir)) != nullptr)
        {
            if ((strcmp(e->d_name, ".") == 0)
                || (strcmp(e->d_name, "..") == 0))
            {
                continue;
            }

            string full_path = path;
            full_path.push_back('/');
            full_path.append(e->d_name);

            if (e->d_type == DT_UNKNOWN)
            {
                // Do a stat() to fill in the d_type field.
                struct stat statbuf;
                if (-1 == stat(full_path.c_str(), &statbuf))
                {
                    PERROR("stat on \"" << full_path << "\"");
                }
                else
                {
                    if (S_ISREG(statbuf.st_mode))
                        e->d_type = DT_REG;
                    else if (S_ISDIR(statbuf.st_mode))
                        e->d_type = DT_DIR;
                }
            }

            if (e->d_type == DT_DIR)
            {
                directories.push_back(move(full_path));
                directory_count++;
            }
            else if (e->d_type == DT_REG || e->d_type == DT_LNK)
            {
                if (file_extension_filter(full_path))
                {
                    files.push_back(move(full_path));
                }
            }
        }
        if (errno != 0)
        {
            PERROR("readdir in \"" << path << "\"");
        }

        closedir(dir);
    }

    INFO("Found " << files.size() << " files "
        "in " << directory_count << " directories.");

    // Next, go through the DB and remove any tracks for which there are no
    // files or their file is unchanged since last grovel.

    INFO("Checking database freshness...");

    vector<tuple<int, int, time_t, string>> db_files = db.GetFiles();

    INFO("Got " << db_files.size() << " files from database.");

    size_t skipped_count = 0;
    size_t removed_count = 0;
    for (const auto& f : db_files)
    {
        int fileId = get<0>(f);
        //int trackId = get<1>(f);
        time_t mtime = get<2>(f);
        const string& path = get<3>(f);

        auto pos = find(files.begin(), files.end(), path);

        if (pos == files.end())
        {
            DEBUG("File not found; removing from DB: " << path);
            db.RemoveFile(fileId);
            removed_count++;
        }
        else
        {
            struct stat s;
            int result = stat(path.c_str(), &s);
            if (result != 0)
            {
                PERROR("stat(" << path << ")");
                continue;
            }
            
            if (s.st_mtime == mtime)
            {
                // MTime is identical; we can skip groveling this one.
                DEBUG("File skipped due to MTime: " << path);
                files.erase(pos);
                skipped_count++;
            }
            else
            {
                // TODO: don't do this, but do an update instead.
                DEBUG("File has changed; removing from DB: " << path);
                db.RemoveFile(fileId);
            }
        }
    }

    INFO("Removed " << removed_count << " stale tracks.");
    INFO("Skipping " << skipped_count << " fresh tracks.");

    // Next, get metadata for remaining files and add to database.
    
    INFO("Extracting metadata from " << files.size() << " files...");

    vector<pair<int,int>> groveled_ids;

    size_t groveled_count = 0;
    while (!files.empty())
    {
        string path = files.front();
        files.pop_front();

        MusicInfo info(path.c_str());

        if (info.has_tag())
        {
            struct stat s;
            if (0 != stat(path.c_str(), &s))
            {
                PERROR("stat(" << path << ")");
                continue;
            }

            int track_id, file_id;
            db.AddTrack(info, path, s.st_mtime, &track_id, &file_id);
            groveled_count++;
            groveled_ids.emplace_back(track_id, file_id);
        }
        else
        {
            DEBUG("no tag: " << path);
        }
    }

    INFO("Groveled " << groveled_count << " new/updated files.");

    INFO("Removing un-referenced tracks, artists, albums, and folders.");
    
    db.CleanTracks();
    db.CleanTables();
    db.CleanPaths();

    return groveled_ids;
}

void build_paths(
    MusicDatabase& db,
    const PathPattern& pathPattern,
    const vector<pair<int,int>>& track_file_ids,
    const ArtistAliases& aliases
    )
{
    unordered_map<string, int> paths;
    size_t num_path_levels = pathPattern.GetNumPathLevels();

    for (pair<int, int> ids : track_file_ids)
    {
        int track_id = get<0>(ids);
        int file_id  = get<1>(ids);

        MusicAttributes attrs;
        db.GetAttributes(file_id, attrs);

        const string *artist = aliases.Lookup(attrs.Artist);
        if (artist != nullptr)
            attrs.Artist = *artist;
        const string *albumartist = aliases.Lookup(attrs.AlbumArtist);
        if (albumartist != nullptr)
            attrs.AlbumArtist = *albumartist;

        int parent_id = 0;
        string path;

        for (size_t level = 0; level < num_path_levels; level++)
        {
            pathPattern.AppendPathComponent(path, attrs, level);
            
            auto pos = paths.find(path);
            if (pos == paths.end())
            {
                DEBUG("adding path: " << path);
                parent_id = db.AddPath(
                    path,
                    parent_id,
                    (level == num_path_levels - 1) ? track_id : 0,
                    (level == num_path_levels - 1) ? file_id  : 0
                    );
                paths.emplace(path, parent_id);
            }
            else
            {
                parent_id = pos->second;
            }
        }
    }
}

