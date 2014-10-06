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
#include "groveler.h"

using namespace std;

vector<int> grovel(const string& base_path, MusicDatabase& db)
{
    deque<string> directories;
    directories.push_back(base_path);

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

            if (e->d_type == DT_DIR)
            {
                directories.push_back(move(full_path));
                directory_count++;
            }
            else if (e->d_type == DT_REG || e->d_type == DT_LNK)
            {
                files.push_back(move(full_path));
            }
        }

        closedir(dir);
    }

    INFO("Found " << files.size() << " files "
        "in " << directory_count << " directories.");

    // Next, go through the DB and remove any tracks for which there are no
    // files or their file is unchanged since last grovel.

    INFO("Checking database freshness...");

    vector<tuple<int, time_t, string>> tracks = db.GetTracks();

    INFO("Got " << tracks.size() << " tracks from database.");

    size_t skipped_count = 0;
    size_t removed_count = 0;
    for (const auto& t : tracks)
    {
        int trackId = get<0>(t);
        time_t mtime = get<1>(t);
        const string& path = base_path + get<2>(t);

        auto pos = find(files.begin(), files.end(), path);

        if (pos == files.end())
        {
            DEBUG("File not found; removing from DB: " << path);
            db.RemoveTrack(trackId);
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
                db.RemoveTrack(trackId);
            }
        }
    }

    INFO("Removed " << removed_count << " stale tracks.");
    INFO("Skipping " << skipped_count << " fresh tracks.");

    // Next, get metadata for remaining files and add to database.

    vector<int> groveled_ids;

    size_t groveled_count = 0;
    while (!files.empty())
    {
        string path = files.front();
        files.pop_front();

        MusicInfo info(path.c_str());

        if (info.has_tag())
        {
            string partial_path(path.c_str() + base_path.size(), path.size() - base_path.size());

            struct stat s;
            if (0 != stat(path.c_str(), &s))
            {
                PERROR("stat(" << path << ")");
                continue;
            }

            int id;
            db.AddTrack(info, partial_path, s.st_mtime, &id);
            groveled_count++;
            groveled_ids.push_back(id);
        }
        else
        {
            DEBUG("no tag: " << path);
        }
    }

    INFO("Groveled " << groveled_count << " new/updated tracks.");

    INFO("Removing un-referenced artists, albums, genres, and folders.");
    
    db.CleanTables();
    db.CleanPaths();

    return groveled_ids;
}

void build_paths(
    MusicDatabase& db,
    const PathPattern& pathPattern,
    const vector<int>& track_ids
    )
{
    unordered_map<string, int> paths;
    size_t num_path_levels = pathPattern.GetNumPathLevels();

    for (int track_id : track_ids)
    {
        MusicAttributes attrs;
        db.GetAttributes(track_id, attrs);

        int parent_id = 0;
        string path;

        for (size_t level = 0; level < num_path_levels; level++)
        {
            pathPattern.AppendPathComponent(path, attrs, level);
            
            auto pos = paths.find(path);
            if (pos == paths.end())
            {
                DEBUG("adding path: " << path);
                parent_id = db.AddPath(path, parent_id, (level == num_path_levels - 1) ? track_id : 0);
                paths.emplace(path, parent_id);
            }
            else
            {
                parent_id = pos->second;
            }
        }
    }
}

