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
#include "MusicInfo.h"
#include "database.h"
#include "groveler.h"

using namespace std;
        
static const char *default_pattern = "%artist%/[%year%] %album%/%track% - %title%.%ext%";

struct path_building_component
{
    enum class Type
    {
        Literal,
        Artist,
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
};
static musicfs_opts musicfs_opts = {};

void usage()
{
    //TODO
    cout << "usage\n";
}

int musicfs_getattr(const char *path, struct stat *stbuf)
{
    DEBUG("getattr " << path);
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0700;
        stbuf->st_uid  = getuid();
        stbuf->st_gid  = getgid();
        stbuf->st_atim = stbuf->st_mtim = stbuf->st_ctim = {0,123456789} /* todo, should use database's time */;
        return 0;

        //return stat(musicfs_opts.backing_fs, stbuf);
    }
    return -EIO;
}

int musicfs_opendir(const char *path, fuse_file_info *ffi)
{
    DEBUG("opendir" << path);

    if (strcmp(path, "/") == 0)
    {
        return 0;
    }

    return -EIO;
}

int musicfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    DEBUG("readdir " << path);

    if (strcmp(path, "/") == 0)
    {
        filler(buf, "lol", nullptr, 0);
        return 0;
    }
    
    return -EIO;
}

static fuse_operations MusicFS_Opers = {};
void musicfs_init_fuse_operations()
{
#define IMPL(_func) MusicFS_Opers._func = musicfs_##_func
    IMPL(getattr);
    IMPL(opendir);
    IMPL(readdir);
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
        case t::Artist: columns.push_back("artist"); break;
        case t::Album:  columns.push_back("album"); break;
        case t::Genre:  columns.push_back("genre"); break;
        case t::Year:   columns.push_back("year"); break;

        case t::Track:  columns.push_back("track"); break;
        case t::Title:  columns.push_back("title"); break;
        case t::Extension: columns.push_back("path"); break;

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
                path += cols[j].second;
                constraints2.artist_id = cols[j].first;
                j++;
                break;

            case t::Album:
                path += cols[j].second;
                constraints2.album_id = cols[j].first;
                j++;
                break;

            case t::Genre:
                path += cols[j].second;
                constraints2.genre_id = cols[j].first;
                j++;
                break;

            case t::Year:
                path += cols[j].second;
                constraints2.year_id = cols[j].first;
                j++;
                break;

            case t::Track:
            case t::Title:
                path += cols[j].second;
                j++;
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

    if (fuse_opt_parse(&args, &musicfs_opts, musicfs_opts_spec, musicfs_opt_proc) == -1)
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
            musicfs_opts.backing_fs = nonopt_arguments[0];
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

    if (musicfs_opts.pattern == nullptr)
    {
        musicfs_opts.pattern = const_cast<char*>(default_pattern);
        INFO("No path pattern specified, using default: " << default_pattern);
    }
    parse_pattern(musicfs_opts.path_components, musicfs_opts.pattern);

    cout << "opening database...\n";

    MusicDatabase db("music.db");

    db.BeginHeavyWriting();

    cout << "groveling music...\n";

    grovel(musicfs_opts.backing_fs, db);

    cout << "computing paths";

    db.ClearPaths();

    cout << "...\n";

    MusicAttributesById constraints = { -1, -1, -1, -1, -1 };
    build_paths(db, constraints, musicfs_opts.path_components, 0, "");

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

    fuse_main(args.argc, args.argv, &MusicFS_Opers, nullptr);

    return 0;
}
