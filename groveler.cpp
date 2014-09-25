#include <string>
#include <memory>
#include <vector>
#include <deque>

#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#define MUSICFS_LOG_SUBSYS "Groveler"
extern int musicfs_log_level;
extern bool musicfs_log_stderr;
#include "logging.h"

#include "MusicInfo.h"
#include "database.h"
#include "groveler.h"

using namespace std;

//static vector<string> s_groveledExtensions = { "mp3", "flac", "ogg" };

void grovel(const string& base_path, MusicDatabase& db)
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

    INFO("Found " << files.size() << " files in " << directory_count << " directories.");

    // Next, go through the DB and remove any tracks for which there are no files or their file is unchanged since last grovel.

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
            DEBUG("File " << path << " not found; removing from DB");
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
                DEBUG("File " << path << " skipped due to MTime");
                files.erase(pos);
                skipped_count++;
            }
            else
            {
                // TODO: don't do this, but do an update instead.
                DEBUG("File " << path << " has changed; removing from DB");
                db.RemoveTrack(trackId);
            }
        }
    }

    INFO("Removed " << removed_count << " stale tracks.");
    INFO("Skipping " << skipped_count << " fresh tracks.");

    // Next, get metadata for remaining files and add to database.

    size_t groveled_count = 0;
    while (!files.empty())
    {
        string path = files.front();
        files.pop_front();

        MusicInfo info(path.c_str());

        DEBUG("file: " << path);

        if (info.has_tag())
        {
            string partial_path(path.c_str() + base_path.size(), path.size() - base_path.size());

            struct stat s;
            if (0 != stat(path.c_str(), &s))
            {
                PERROR("stat(" << path << ")");
                continue;
            }

            db.AddTrack(info, partial_path, s.st_mtime);
            groveled_count++;
        }
        else
        {
            DEBUG("no tag");
        }
    }

    INFO("Groveled " << groveled_count << " new/updated tracks.");
}
