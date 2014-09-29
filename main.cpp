#include <fuse.h>
#include <fuse_opt.h>

#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <cstddef>
#include <cstring>

#include <unistd.h>

int musicfs_log_level;
bool musicfs_log_stderr = false;
#include "logging.h"

#include "util.h"
#include "musicinfo.h"
#include "database.h"
#include "groveler.h"

using namespace std;
        
static const char *default_pattern = "%albumartist%/[%year%] %album%/%track% - %title%.%ext%";

struct path_building_component
{
    enum class Type
    {
        Literal,
        Artist,
        AlbumArtist,
        Album,
        Genre,
        Year,
        Track,
        Title,
        Extension
    };

    path_building_component(string s)
        : type(Type::Literal)
        , literal(s)
    {}

    path_building_component(Type t)
        : type(t)
        , literal()
    {}

    Type type;
    string literal;
};

void parse_pattern(vector<vector<path_building_component>>& path_components, const char *pattern)
{
    typedef path_building_component::Type t;
    path_components.emplace_back();
    string buf;
    bool in_placeholder = false;
    for (size_t i = 0; pattern[i] != '\0'; i++)
    {
        char c = pattern[i];
        if (c == '%')
        {
            if (in_placeholder)
            {
                t type = t::Literal;
                if (buf == "artist")
                    type = t::Artist;
                else if (buf == "albumartist")
                    type = t::AlbumArtist;
                else if (buf == "album")
                    type = t::Album;
                else if (buf == "genre")
                    type = t::Genre;
                else if (buf == "year")
                    type = t::Year;
                else if (buf == "track")
                    type = t::Track;
                else if (buf == "title")
                    type = t::Title;
                else if (buf == "ext")
                    type = t::Extension;
                else
                {
                    cerr << "invalid token %" << buf << "% in path pattern.\n";
                    throw new exception();
                }
                in_placeholder = false;
                buf.clear();
                path_components.back().emplace_back(type);
            }
            else
            {
                if (!buf.empty())
                {
                    path_components.back().emplace_back(buf);
                    buf.clear();
                }
                in_placeholder = true;
            }
        }
        else if (c == '/')
        {
            path_components.emplace_back();
        }
        else
        {
            buf.push_back(c);
        }
    }

    // Debug:
    DEBUG("parsed path pattern:");
    for (const auto& component : path_components)
    {
        DEBUG("component:");
        for (const auto& part : component)
        {
            string type;
            if (part.type == t::Literal) type = "literal: " + part.literal;
            else if (part.type == t::Artist) type = "artist";
            else if (part.type == t::AlbumArtist) type = "albumartist";
            else if (part.type == t::Album) type = "album";
            else if (part.type == t::Genre) type = "genre";
            else if (part.type == t::Year) type = "year";
            else if (part.type == t::Track) type = "track";
            else if (part.type == t::Title) type = "title";
            else if (part.type == t::Extension) type = "ext";
            else type = "unknown";
            DEBUG("\tpart:" << type);
        }
    }
}

struct musicfs_opts
{
    char *backing_fs;
    char *pattern;
    vector<vector<path_building_component>> path_components;
    MusicDatabase *db;
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
    stbuf->st_mode = S_IFDIR | 0555;
    stbuf->st_uid  = getuid();
    stbuf->st_gid  = getgid();

    // TODO should use database's time.
    stbuf->st_atim = stbuf->st_mtim = stbuf->st_ctim = {0,123456789};

    // This value needs to be at least 1.
    // The actual value doesn't seem to matter much, and is expensive to compute.
    stbuf->st_nlink = 1;
}

int musicfs_access(const char *path, int mode)
{
    DEBUG("access (" << mode << ") " << path);

    if (strcmp(path, "/") != 0 && !musicfs.db->PathExists(path))
        return -ENOENT;

    // Writing is never OK.
    if (mode & W_OK)
        return -EACCES;

    string partialRealPath = musicfs.db->GetRealPath(path);
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
        if (!musicfs.db->PathExists(path))
            return -ENOENT;

        string partialRealPath = musicfs.db->GetRealPath(path);
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

int musicfs_opendir(const char *path, fuse_file_info *ffi)
{
    DEBUG("opendir" << path);

    auto constraints = new MusicAttributesById();

    // Here's the trick:
    // To narrow the search space, we need to specify which properties NEED to be null,
    // based on what level in the directory tree we're at.
    // Setting them to 0 in the constraints specifies "must be null".

    // Count slashes to see what level in the tree we're at.
    size_t level = 0;
    for (size_t i = 1; path[i] != '\0'; i++)
    {
        if (path[i] == '/')
            level++;
    }

    // Starting at the next level in the path pattern, any properties used to make that
    // component need to be specified as null.
    for (size_t i = level + 1, n = musicfs.path_components.size(); i < n; i++)
    {
        typedef path_building_component::Type t;
        for (const path_building_component& component : musicfs.path_components[i])
        {
            switch (component.type)
            {
            case t::Literal:
                break;
            case t::Artist:
                constraints->artist_id = 0;
                break;
            case t::AlbumArtist:
                constraints->albumartist_id = 0;
                break;
            case t::Album:
                constraints->album_id = 0;
                break;
            case t::Genre:
                constraints->genre_id = 0;
                break;
            case t::Year:
                constraints->year_id = 0;
                break;
            case t::Track:
            case t::Title:
            case t::Extension:
                constraints->track_id = 0;
                break;
            }
        }
    }

    bool found = (strcmp(path, "/") == 0) || musicfs.db->GetPathAttributes(path, *constraints);
    if (!found)
    {
        return -ENOENT;
    }
    else
    {
        ffi->fh = reinterpret_cast<uint64_t>(constraints);
        return 0;
    }
}

int musicfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    DEBUG("readdir " << path);

    auto constraints = reinterpret_cast<MusicAttributesById*>(fi->fh);

    vector<pair<string, bool>> entries = musicfs.db->GetChildrenOfPath(path, *constraints);

    for (const pair<string, bool>& entry : entries)
    {
        size_t path_len = strlen(path);
        if (path_len > 1) path_len++;

        string basename = entry.first.substr(path_len);
        DEBUG("adding path: " << basename);

        struct stat stbuf;
        if (entry.second)
        {
            // If this is a file, go and stat it now to save syscalls later.
            stat_real_file(entry.first.c_str(), &stbuf);
        }
        else
        {
            fake_directory_stat(&stbuf);
        }

        filler(buf, basename.c_str(), &stbuf, 0);
    }

    return 0;
}

int musicfs_releasedir(const char *path, struct fuse_file_info *fi)
{
    DEBUG("releasedir " << path);

    if (fi->fh != 0)
    {
        auto constraints = reinterpret_cast<MusicAttributesById*>(fi->fh);
        delete constraints;
    }
    return 0;
}

int musicfs_open(const char *path, struct fuse_file_info *fi)
{
    DEBUG("open " << path);

    //TODO: check fi->flags ?

    string partialRealPath = musicfs.db->GetRealPath(path);
    if (partialRealPath.empty())
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
    else if (r != buf_size)
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
    { "backing_fs=%s",  offsetof(struct musicfs_opts, backing_fs),  0 },
    { "pattern=%s",     offsetof(struct musicfs_opts, pattern),     0 },
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

string sanitize_path(const string& s)
{
    string result;
    for (const char& c : s)
    {
        switch (c)
        {
        // This is the set of restricted path characters used by Windows.
        // Unix only forbids '/'.
        // Let's play nice here and use Windows' set.
        case '\\':
        case '/':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
            result.push_back('_');
            break;
        default:
            result.push_back(c);
        }
    }
    return result;
}

#if 0
void build_paths2(
    MusicDatabase& db,
    const vector<int>& trackIds,
    const vector<vector<path_building_component>>& path_components
    )
{
    for (int trackId : trackIds)
    {

    }
}
#endif

void build_paths(
    MusicDatabase& db,
    const MusicAttributesById& constraints,
    const vector<vector<path_building_component>>& path_components,
    size_t level,
    const string& base_path
    )
{
    vector<string> columns;
    typedef path_building_component::Type t;

    for (const path_building_component& comp : path_components[level])
    {
        switch (comp.type)
        {
        case t::Artist:         columns.push_back("artist"); break;
        case t::AlbumArtist:    columns.push_back("albumartist"); break;
        case t::Album:          columns.push_back("album"); break;
        case t::Genre:          columns.push_back("genre"); break;
        case t::Year:           columns.push_back("year"); break;

        case t::Track:          columns.push_back("track"); break;
        case t::Extension:      columns.push_back("path"); break;
        case t::Title:
            columns.push_back("title");
            // Also add the path in case the title is empty.
            columns.push_back("path");
            break;

        case t::Literal:
        default:
            continue;
        }
    }

    vector<vector<pair<int, string>>> values = db.GetValues(columns, constraints);

    for (vector<pair<int, string>> cols : values)
    {
        MusicAttributesById constraints2 = constraints;
        string path = base_path + '/';

        bool track_specified = false;

        for (size_t i = 0, j = 0, n = path_components[level].size(); i < n; i++)
        {
            const path_building_component& comp = path_components[level][i];
            switch (comp.type)
            {
            case t::Literal:
                path += comp.literal;
                break;

            case t::Artist:
                if (cols[j].second.empty())
                    path += "(unknown artist)";
                else
                    path += sanitize_path(cols[j].second);
                constraints2.artist_id = cols[j].first;
                j++;
                break;

            case t::AlbumArtist:
                if (cols[j].second.empty())
                    path += "(unknown artist)";
                else
                    path += sanitize_path(cols[j].second);
                constraints2.albumartist_id = cols[j].first;
                j++;
                break;

            case t::Album:
                if (cols[j].second.empty())
                    path += "(unknown album)";
                else
                    path += sanitize_path(cols[j].second);
                constraints2.album_id = cols[j].first;
                j++;
                break;

            case t::Genre:
                if (cols[j].second.empty())
                    path += "(unknown genre)";
                else
                    path += sanitize_path(cols[j].second);
                constraints2.genre_id = cols[j].first;
                j++;
                break;

            case t::Year:
                if (cols[j].second.empty() || cols[j].second == "0")
                    path += "____";
                else
                    path += cols[j].second;
                constraints2.year_id = cols[j].first;
                j++;
                break;

            case t::Track:
                if (cols[j].second.empty() || cols[j].second == "0")
                    path += "__";
                else
                {
                    if (cols[j].second.size() == 1)
                        path += '0';
                    path += cols[j].second;
                }
                j++;
                track_specified = true;
                break;

            case t::Title:
                // This fetched the title as cols[j] and the path as cols[j+1].
                if (cols[j].second.empty())
                {
                    //path += "(untitled)";
                    string& filename = cols[j+1].second;
                    auto slash = filename.find_last_of('/');
                    auto dot = filename.find_last_of('.');
                    path += filename.substr(slash + 1, dot - slash - 1);
                }
                else
                    path += sanitize_path(cols[j].second);
                constraints2.track_id = cols[j].first;
                j += 2;
                track_specified = true;
                break;

            case t::Extension:
                path += cols[j].second.substr(cols[j].second.find_last_of('.') + 1);
                j++;
                track_specified = true;
                break;
            }
        }

        if (track_specified && (level != path_components.size() - 1))
        {
            ERROR("In pattern: %track%, %title%, and %ext% can only be specified in the last path component.");
            throw new exception();
        }

        DEBUG("adding path: " << path);
        db.AddPath(path, constraints2);

        if (level < path_components.size() - 1)
        {
            build_paths(db, constraints2, path_components, level + 1, path);
        }
    }
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
    parse_pattern(musicfs.path_components, musicfs.pattern);

    cout << "opening database...\n";

    MusicDatabase db("music.db");

    db.BeginHeavyWriting();

    cout << "groveling music...\n";

    grovel(musicfs.backing_fs, db);

    cout << "computing paths...\n";

    MusicAttributesById constraints;
    build_paths(db, constraints, musicfs.path_components, 0, "");

    db.EndHeavyWriting();

#if 0
    // DEBUG
    MusicAttributes c;
    c.artist = make_unique<string>("C418");
    c.year = make_unique<string>("2008");
    for (const vector<pair<int, string>>& strings : db.GetValues({"album", "year", "title", "track"}, c))
    {
        stringstream ss;
        for (const pair<int, string>& p : strings)
        {
            ss << p.first << "/" << p.second << ", ";
        }

        DEBUG("db: " << ss.str());
    }
    return 0;
    // END DEBUG
#endif

    cout << "ready to go!\n";

    musicfs.db = &db;

    fuse_main(args.argc, args.argv, &MusicFS_Opers, nullptr);

    return 0;
}
