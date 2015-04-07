//
// MusicFS :: Program Entry Point and Filesystem Functions
//
// Copyright (c) 2014-2015 by William R. Fraser
//

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

#include "configuration.h"
#include "util.h"
#include "musicinfo.h"
#include "database.h"
#include "path_pattern.h"
#include "aliases.h"
#include "groveler.h"

using namespace std;

const char default_database_name[] = "music.db";

#define countof(_) (sizeof(_) / sizeof(*(_)))
        
struct musicfs_data
{
    Config config;
    MusicDatabase *db;
    char *database_path;
    time_t startup_time;
};
static musicfs_data musicfs = {};

int stat_real_file(const char *path, struct stat *stbuf)
{
    if (-1 == stat(path, stbuf))
    {
        PERROR(__FUNCTION__ << ": failure to stat real file: " << path);
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

    string realPath;
    bool exists = musicfs.db->GetRealPath(path, realPath);

    if (strcmp(path, "/") != 0 && !exists)
        return -ENOENT;

    // Writing is never OK.
    if (mode & W_OK)
        return -EACCES;

    if (realPath.empty())
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
        string realPath;
        bool exists = musicfs.db->GetRealPath(path, realPath);

        if (!exists)
            return -ENOENT;

        if (realPath.empty())
        {
            fake_directory_stat(stbuf);
            return 0;
        }
        else
        {
            return stat_real_file(realPath.c_str(), stbuf);
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

int filetype_ranking(const string& path)
{
    if (path.size() == 0)
        return 1;

    for (const string& ext : musicfs.config.extension_priority)
    {
        if ((ext == "*") || iendsWith(path, ext))
            return -1;
    }

    return 2;
}

int file_preference(const string& a, const string& b)
{
    return filetype_ranking(a) < filetype_ranking(b);
}

int musicfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    DEBUG("readdir " << path);

    int path_id = fi->fh;
    vector<string> entries = musicfs.db->GetChildrenOfPath(path_id, file_preference);

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

    string realPath;
    bool found = musicfs.db->GetRealPath(path, realPath);
    if (!found || realPath.empty())
    {
        return -ENOENT;
    }

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

static const char REALPATH_XATTR_NAME[] = "user.musicfs.real_path";

int musicfs_listxattr(const char *path, char *list, size_t size)
{
    DEBUG("listxattr " << path);

    if (strcmp(path, "/") == 0)
        return 0;

    string realPath;
    bool exists = musicfs.db->GetRealPath(path, realPath);

    if (!exists)
        return -ENOENT;

    if (realPath.empty())
        return 0;

    size_t requiredSize = sizeof(REALPATH_XATTR_NAME);

    if (size == 0)
        return requiredSize;

    if (size < requiredSize)
        return -ERANGE;

    memcpy(list, REALPATH_XATTR_NAME, requiredSize);
    return requiredSize;
}

int musicfs_getxattr(const char *path, const char *name, char *value, size_t size)
{
    DEBUG("getxattr(" << name << ") " << path);

    string realPath;
    bool exists = musicfs.db->GetRealPath(path, realPath);

    if (!exists)
        return -ENOENT;

    if (realPath.empty())
        return -EINVAL;

    if (strcmp(name, REALPATH_XATTR_NAME) == 0)
    {
        if (size == 0)
            return realPath.size();

        if (size < realPath.size())
            return -ERANGE;

        memcpy(value, realPath.c_str(), realPath.size());
        return realPath.size();
    }
    else
    {
        return -EINVAL;
    }
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
    IMPL(listxattr);
    IMPL(getxattr);
#undef IMPL
}

enum
{
    KEY_VERBOSE,
    KEY_DEBUG,
    KEY_HELP,
    KEY_VERSION,
    KEY_EXTENSIONS,
    KEY_ALIASES,
    KEY_BACKING_FS,
    KEY_PATH_PATTERN,
};

enum
{
    FUSE_OPT_ERROR = -1,
    FUSE_OPT_DISCARD = 0,
    FUSE_OPT_KEEP = 1
};

void usage()
{
    cerr <<
    // Limit to 80 columns:
    //   ###############################################################################
        "usage: musicfs [options] <backing> <mount point>\n"
        "\n"
        "MusicFS options:\n"
        "   -o backing_fs=<path>    Path to source music files (required here or\n"
        "                               as the first non-option argument)\n"
        "   -o pattern=<pattern>    Path generation pattern. A string containing any\n"
        "                               of the following: %albumartist%, %artist%,\n"
        "                               %album%, %year%, %track%, %title%, %ext%.\n"
        "                               Defaults to: \"%albumartist%/[%year%] %album%/\n"
        "                               %track% - %title%.%ext%\"\n"
        "   -o database=<path>      Path to the database file to be used. Defaults to\n"
        "                               music.db in the current directory.\n"
        "   -o extensions=<list>    Semicolon-delimited list of file extensions. When\n"
        "                               multiple files are available for the same\n"
        "                               track, extensions earlier in this list will be\n"
        "                               given precedence and hide the others. End with\n"
        "                               a '*' to include un-matched files. Defaults to\n"
        "                               \"flac;mp3;*\"\n"
        "   -o aliases=<path>       Path to a file listing artist aliases. The file\n"
        "                               should list the canonical name first, followed\n"
        "                               by aliases indented on subsequent lines.\n"
        "   -o\n"
        "   -v\n"
        "   --verbose               Enable informational messages.\n"
        "   -d\n"
        "   --debug\n"
        "   -o debug                Enable debugging mode. MusicFS will not fork to\n"
        "                               background, and enables all debugging messages.\n"
        "\n"
        "   Note that the backing filesystem can be specified multiple times. All of\n"
        "   the paths given will be used to build the filesystem.\n"
        "\n";
}

static fuse_opt musicfs_opts_spec[] = {
    { "database=%s",    offsetof(struct musicfs_data, database_path),   0 },
    FUSE_OPT_KEY("pattern=%s",  KEY_PATH_PATTERN),
    FUSE_OPT_KEY("backing_fs=%s", KEY_BACKING_FS),
    FUSE_OPT_KEY("extensions=%s", KEY_EXTENSIONS),
    FUSE_OPT_KEY("aliases=%s",  KEY_ALIASES),
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

static void skip_to_arg_value(const char **arg)
{
    while (**arg != '=' && **arg != '\0')
        (*arg)++;
    if (**arg == '=')
        (*arg)++;
}

vector<string> nonopt_args;

int musicfs_opt_proc(void *data, const char *arg, int key,
        fuse_args *outargs)
{
    switch (key)
    {
    case FUSE_OPT_KEY_OPT:
        // Unknown option-argument. Pass it along to FUSE I guess?
        return FUSE_OPT_KEEP;

    case KEY_BACKING_FS:
        skip_to_arg_value(&arg);
        if (*arg == '\0')
            return FUSE_OPT_DISCARD;
        // fall-through:

    case FUSE_OPT_KEY_NONOPT:
        // We take at least two non-option arguments.
        // The last one is the mount point. THis needs to be tacked on to the outargs,
        // because FUSE handles it. But during parsing we don't know how many there are,
        // so just save them for later, and main() will fix it.
        nonopt_args.push_back(arg);
        return FUSE_OPT_DISCARD;

    case KEY_EXTENSIONS:
        skip_to_arg_value(&arg);

        for (size_t start = 0, end = 0; ; end++)
        {
            if (arg[end] == ';' || arg[end] == '\0')
            {
                string ext(arg + start, (end - start));
                if (ext != "*")
                {
                    ext = "." + ext;
                }
                musicfs.config.extension_priority.push_back(ext);
                start = end + 1;
            }
            if (arg[end] == '\0')
                break;
        }
        break;

    case KEY_PATH_PATTERN:
        skip_to_arg_value(&arg);
        musicfs.config.path_pattern.assign(arg);
        return FUSE_OPT_DISCARD;

    case KEY_ALIASES:
        skip_to_arg_value(&arg);
        musicfs.config.aliases_conf.assign(arg);
        return FUSE_OPT_DISCARD;

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
    DEBUG("Version " MUSICFS_VERSION);

    musicfs_init_fuse_operations();

    fuse_args args = FUSE_ARGS_INIT(argc, argv);

    if (fuse_opt_parse(&args, &musicfs, musicfs_opts_spec, musicfs_opt_proc) == -1)
    {
        cerr << "MusicFS: argument parsing failed.\n";
        return 1;
    }

    if (nonopt_args.size() >= 2)
    {
        fuse_opt_add_arg(&args, nonopt_args.back().c_str());
        musicfs.config.backing_fs_paths.assign(nonopt_args.begin(), nonopt_args.end() - 1);
    }
    else
    {
        cerr << "MusicFS: error: you need to specify a mount point and at least one backing filesystem path.\n";
        usage();
        fuse_opt_add_arg(&args, "-ho");
        fuse_main(args.argc, args.argv, &MusicFS_Opers, nullptr);
        return -1;
    }

    if (musicfs.config.path_pattern.empty())
    {
        musicfs.config.path_pattern.assign(default_pattern);
        INFO("No path pattern specified, using default: " << default_pattern);
    }

    PathPattern pathPattern(musicfs.config.path_pattern.c_str());

    if (musicfs.config.extension_priority.empty())
    {
        musicfs.config.extension_priority = { ".flac", ".mp3", "*" };
    }

    INFO("File extension priority: ");
    for (const auto& ext : musicfs.config.extension_priority) INFO("\t" << ext);

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

    ArtistAliases aliases;
    if (!musicfs.config.aliases_conf.empty())
    {
        DEBUG("Artist aliases file: " << musicfs.config.aliases_conf);
        ArtistAliases aliases;
        bool ok = aliases.ParseFile(musicfs.config.aliases_conf);
        if (!ok)
        {
            cerr << "MusicFS: specified artist aliases file \""
                << musicfs.config.aliases_conf
                << "\" could not be opened: "
                << strerror(errno) << endl;
            return -1;
        }
    }

    db.BeginTransaction();

    cout << "Groveling music. This may take a while...\n";
    vector<pair<int,int>> groveled_ids = grovel(musicfs.config.backing_fs_paths, db);

    db.EndTransaction();
    db.BeginTransaction();

    cout << "Computing paths...\n";
    build_paths(db, pathPattern, groveled_ids, aliases);

    db.EndTransaction();

    cout << "Ready to go!\n";
    musicfs.startup_time = time(nullptr);
    musicfs.db = &db;
    fuse_main(args.argc, args.argv, &MusicFS_Opers, nullptr);

    return 0;
}
