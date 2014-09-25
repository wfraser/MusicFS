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

void grovel(const char *path, MusicDatabase& db)
{
    deque<string> directories;
    directories.push_back(path);

    deque<string> files;

    // First, take inventory of all the files in here.

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
            }
            else if (e->d_type == DT_REG || e->d_type == DT_LNK)
            {
                files.push_back(move(full_path));
            }
        }

        closedir(dir);
    }

    // Next, go through the DB and remove any tracks for which there are no files.

    // Next

    while (!files.empty())
    {
        string path = files.front();
        files.pop_front();

        MusicInfo info(path.c_str());

        DEBUG("file: " << path);

        if (info.has_tag())
        {
            DEBUG("artist(" << info.artist() << ") title(" << info.title() << ")");
            db.AddTrack(info, path);
        }
        else
        {
            DEBUG("no tag");
        }

        //TODO
    }
}
