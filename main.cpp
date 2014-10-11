#include <fuse.h>
#include <fuse_opt.h>

#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <sstream>
#include <cstddef>
#include <cstring>

#include <unistd.h>
#include <time.h>

int musicfs_log_level;
bool musicfs_log_stderr = false;
#include "logging.h"

#include "util.h"
#include "musicinfo.h"
#include "database.h"
#include "path_pattern.h"
#include "groveler.h"

using namespace std;

const char default_database_name[] = "music.db";

#define countof(_) (sizeof(_) / sizeof(*(_)))
        
struct musicfs_opts
{
    char *backing_fs;
    char *pattern;
    MusicDatabase *db;
    char *database_path;
    time_t startup_time;
};
static musicfs_opts musicfs = {};

void usage()
{
    //TODO
    cout << "usage\n";
}

int stat_real_file(const char *path, struct stat *stbuf)
{
    string real_path = musicfs.backing_fs;
    real_path += "/";
    real_path += path;

    if (-1 == stat(real_path.c_str(), stbuf))
    {
        PERROR(__FUNCTION__ << ": failure to stat real file: " << real_path);
        return -errno;
    }

    // Remove write permissions.
    stbuf->st_mode &= ~0222;

    return 0;
}

void fake_directory_stat(struct stat *stbuf)
{
    stbuf->st_mode = S_IFDIR | 0555; // dr-xr-xr-x
    stbuf->st_uid  = getuid();
    stbuf->st_gid  = getgid();

    // Directory atime, mtime, and ctime are set to our startup time.
    stbuf->st_ctim.tv_sec = musicfs.startup_time;
    stbuf->st_ctim.tv_nsec = 0;
    stbuf->st_atim = stbuf->st_mtim = stbuf->st_ctim;

    // This value needs to be at least 1.
    // The actual value doesn't seem to matter much, and is expensive to compute.
    stbuf->st_nlink = 1;
}

int musicfs_access(const char *path, int mode)
{
    DEBUG("access (" << mode << ") " << path);

    string partialRealPath;
    bool exists = musicfs.db->GetRealPath(path, partialRealPath);

    if (strcmp(path, "/") != 0 && !exists)
        return -ENOENT;

    // Writing is never OK.
    if (mode & W_OK)
        return -EACCES;

    if (partialRealPath.empty())
    {
        return 0;
    }
    else
    {
        if (mode & X_OK)
            return -EACCES;
        else
            return 0;
    }
}

int musicfs_getattr(const char *path, struct stat *stbuf)
{
    DEBUG("getattr " << path);
    if (strcmp(path, "/") == 0)
    {
        fake_directory_stat(stbuf);
        return 0;
    }
    else
    {
        string partialRealPath;
        bool exists = musicfs.db->GetRealPath(path, partialRealPath);

        if (!exists)
            return -ENOENT;

        if (partialRealPath.empty())
        {
            fake_directory_stat(stbuf);
            return 0;
        }
        else
        {
            return stat_real_file(partialRealPath.c_str(), stbuf);
        }
    }
}

int musicfs_opendir(const char *path, fuse_file_info *fi)
{
    DEBUG("opendir" << path);

    int path_id = 0;
    if (strcmp(path, "/") != 0)
    {
        path_id = musicfs.db->GetPathId(path);
        if (path_id == 0)
            return -ENOENT;
    }

    fi->fh = path_id;
    return 0;
}

int musicfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    DEBUG("readdir " << path);

    int path_id = fi->fh;

    vector<string> entries = musicfs.db->GetChildrenOfPath(path_id);

    filler(buf, ".", nullptr, 0);
    filler(buf, "..", nullptr, 0);

    for (const string& entry : entries)
    {
        size_t path_len = strlen(path);
        if (path_len > 1) path_len++;

        string basename = entry.substr(path_len);
        filler(buf, basename.c_str(), nullptr, 0);
    }

    return 0;
}

int musicfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    DEBUG("releasedir " << path);
    return 0;
}

int musicfs_open(const char *path, struct fuse_file_info *fi)
{
    DEBUG("open " << path);

    //TODO: check fi->flags ?

    string partialRealPath;
    bool found = musicfs.db->GetRealPath(path, partialRealPath);
    if (!found || partialRealPath.empty())
    {
        return -ENOENT;
    }

    string realPath = musicfs.backing_fs;
    realPath += partialRealPath;
    int fd = open(realPath.c_str(), fi->flags);
    if (fd == -1)
    {
        PERROR("open");
        return -errno;
    }

    fi->fh = fd;

    return 0;
}

int musicfs_read(const char *path, char *buf, size_t buf_size, off_t offset, struct fuse_file_info *fi)
{
    DEBUG("read " << buf_size << "@" << offset << " " << path);

    ssize_t r = pread(fi->fh, buf, buf_size, offset);
    if (r == -1)
    {
        PERROR("read");
        return -errno;
    }
    else if (r != (ssize_t)buf_size)
    {
        DEBUG("fewer bytes read than requestd");
    }
    return r;
}

int musicfs_release(const char *path, struct fuse_file_info *fi)
{
    DEBUG("release " << path);

    int result = close(fi->fh);
    if (result == -1)
    {
        PERROR("close");
        return -errno;
    }
    return 0;
}

static fuse_operations MusicFS_Opers = {};
void musicfs_init_fuse_operations()
{
#define IMPL(_func) MusicFS_Opers._func = musicfs_##_func
    IMPL(access);
    IMPL(getattr);
    IMPL(opendir);
    IMPL(readdir);
    IMPL(releasedir);
    IMPL(open);
    IMPL(read);
    IMPL(release);
#undef IMPL
}

enum
{
    KEY_VERBOSE,
    KEY_DEBUG,
    KEY_HELP,
    KEY_VERSION,
    KEY_PATTERN,
};

enum
{
    FUSE_OPT_ERROR = -1,
    FUSE_OPT_DISCARD = 0,
    FUSE_OPT_KEEP = 1
};

static fuse_opt musicfs_opts_spec[] = {
    { "backing_fs=%s",  offsetof(struct musicfs_opts, backing_fs),      0 },
    { "pattern=%s",     offsetof(struct musicfs_opts, pattern),         0 },
    { "database=%s",    offsetof(struct musicfs_opts, database_path),   0 },
    FUSE_OPT_KEY("verbose",     KEY_VERBOSE),
    FUSE_OPT_KEY("-v",          KEY_VERBOSE),
    FUSE_OPT_KEY("--verbose",   KEY_VERBOSE),
    FUSE_OPT_KEY("debug",       KEY_DEBUG),
    FUSE_OPT_KEY("-d",          KEY_DEBUG),
    FUSE_OPT_KEY("--debug",     KEY_DEBUG),
    FUSE_OPT_KEY("-h",          KEY_HELP),
    FUSE_OPT_KEY("--help",      KEY_HELP),
    FUSE_OPT_KEY("-V",          KEY_VERSION),
    FUSE_OPT_KEY("--version",   KEY_VERSION),
    FUSE_OPT_END
};

int num_nonopt_args_read = 0;
char * nonopt_arguments[2] = { nullptr, nullptr };

int musicfs_opt_proc(void *data, const char *arg, int key,
        fuse_args *outargs)
{
    switch (key)
    {
    case FUSE_OPT_KEY_OPT:
        // Unknown option-argument. Pass it along to FUSE I guess?
        return FUSE_OPT_KEEP;

    case FUSE_OPT_KEY_NONOPT:
        // We take either 1 or 2 non-option arguments.
        // The last one is the mount point. THis needs to be tacked on to the outargs,
        // because FUSE handles it. But during parsing we don't know how many there are,
        // so just save them for later, and main() will fix it.
        if (num_nonopt_args_read < 2)
        {
            const_cast<const char**>(nonopt_arguments)[num_nonopt_args_read++] = arg;
            return FUSE_OPT_DISCARD;
        }
        else
        {
            cerr << "MusicFS: too many arguments: don't know what to do with \""
                << arg << "\"\n";
            return FUSE_OPT_ERROR;
        }
        break;

    case KEY_VERBOSE:
        musicfs_log_level = LOG_LEVEL_INFO;
        musicfs_log_stderr = true;
        return FUSE_OPT_DISCARD;

    case KEY_DEBUG:
        musicfs_log_level = LOG_LEVEL_DEBUG;
        musicfs_log_stderr = true;
        fuse_opt_add_arg(outargs, "-d");
        return FUSE_OPT_DISCARD;

    case KEY_HELP:
        fuse_opt_add_arg(outargs, "-h");
        usage();
        fuse_main(outargs->argc, outargs->argv, &MusicFS_Opers, nullptr);
        exit(1);

    case KEY_VERSION:
        cerr << "MusicFS: " << MUSICFS_VERSION << endl;
        fuse_opt_add_arg(outargs, "--version");
        fuse_main(outargs->argc, outargs->argv, &MusicFS_Opers, nullptr);
        exit(0);

    default:
        // Shouldn't ever get here.
        cerr << "MusicFS: argument parsing error: hit default case!\n";
        abort();
    }

    return 0;
}

int main(int argc, char **argv)
{
    musicfs_init_fuse_operations();

    fuse_args args = FUSE_ARGS_INIT(argc, argv);

    if (fuse_opt_parse(&args, &musicfs, musicfs_opts_spec, musicfs_opt_proc) == -1)
    {
        cerr << "MusicFS: argument parsing failed.\n";
        return 1;
    }

    DEBUG("Version " MUSICFS_VERSION);

    if (num_nonopt_args_read > 0)
    {
        fuse_opt_add_arg(&args, nonopt_arguments[num_nonopt_args_read - 1]);
        if (num_nonopt_args_read == 2)
        {
            musicfs.backing_fs = nonopt_arguments[0];
        }
    }
    else
    {
        cerr << "MusicFS: error: you need to specify a mount point.\n";
        usage();
        fuse_opt_add_arg(&args, "-ho");
        fuse_main(args.argc, args.argv, &MusicFS_Opers, nullptr);
        return -1;
    }

    if (musicfs.pattern == nullptr)
    {
        musicfs.pattern = const_cast<char*>(default_pattern);
        INFO("No path pattern specified, using default: " << default_pattern);
    }

    PathPattern pathPattern(musicfs.pattern);

    if (musicfs.database_path == nullptr)
    {
        INFO("No database path specified, using \"" << default_database_name
            << "\" in the current directory.");

        musicfs.database_path = get_current_dir_name();
        size_t len = strlen(musicfs.database_path);

        musicfs.database_path = reinterpret_cast<char*>(realloc(musicfs.database_path,
                len + countof(default_database_name) + 2 /* one for trailing NUL, one for slash */));

        musicfs.database_path[len] = '/';
        strcpy(musicfs.database_path + len + 1, default_database_name);
    }

    cout << "Opening database (" << musicfs.database_path << ")...\n";
    MusicDatabase db(musicfs.database_path);

    db.BeginTransaction();

    cout << "Groveling music. This may take a while...\n";
    vector<int> groveled_ids = grovel(musicfs.backing_fs, db);

    cout << "Computing paths...\n";
    build_paths(db, pathPattern, groveled_ids);

    db.EndTransaction();

    cout << "Ready to go!\n";
    musicfs.startup_time = time(nullptr);
    musicfs.db = &db;
    fuse_main(args.argc, args.argv, &MusicFS_Opers, nullptr);

    return 0;
}
