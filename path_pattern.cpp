#include <iostream>
#include <string>
#include <vector>

#include "logging.h"
#include "database.h"
#include "path_pattern.h"

using namespace std;

const char *default_pattern = "%albumartist%/[%year%] %album%/%track% - %title%.%ext%";

PathPattern::PathPattern(const char *pattern)
{
    typedef Component::Type t;
    m_components.emplace_back();
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
                m_components.back().emplace_back(type);
            }
            else
            {
                if (!buf.empty())
                {
                    m_components.back().emplace_back(buf);
                    buf.clear();
                }
                in_placeholder = true;
            }
        }
        else if (c == '/')
        {
            m_components.emplace_back();
        }
        else
        {
            buf.push_back(c);
        }
    }

    // Debug:
    DEBUG("parsed path pattern:");
    for (const auto& component : m_components)
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

size_t PathPattern::GetNumPathLevels() const
{
    return m_components.size();
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
    while (result.size() > 0 && result.back() == '.')
    {
        // Windows doesn't like paths that end in a dot.
        result.pop_back();
    }
    if (result.size() == 0)
    {
        // The path should have at least something in it.
        result.push_back('_');
    }

    // Note: Windows also doesn't like paths that end in whitespace,
    // but MusicInfo should have taken care of that by trimming tag contents.

    return result;
}

void PathPattern::AppendPathComponent(string& path, const MusicAttributes& attrs, size_t level) const
{
    if (level >= m_components.size())
    {
        ERROR("invalid level passed to " << __FUNCTION__ << ": " << level << " >= " << m_components.size());
        throw new exception();
    }

    typedef Component::Type t;

    path.push_back('/');

    for (const Component& comp : m_components[level])
    {
        switch (comp.type)
        {
        case t::Artist:
            if (attrs.Artist.empty())
                path += "(unknown artist)";
            else
                path += sanitize_path(attrs.Artist);
            break;
        case t::AlbumArtist:
            if (attrs.AlbumArtist.empty())
                path += "(unknown artist)";
            else
                path += sanitize_path(attrs.AlbumArtist);
            break;
        case t::Album:
            if (attrs.Album.empty())
                path += "(unknown album)";
            else
                path += sanitize_path(attrs.Album);
            break;
        case t::Genre:
            if (attrs.Genre.empty())
                path += "(unknown genre)";
            else
                path += sanitize_path(attrs.Genre);
            break;
        case t::Year:
            if (attrs.Year.empty())
                path += "____";
            else
                path += attrs.Year;
            break;
        case t::Extension:
            path += attrs.Path.substr(attrs.Path.find_last_of('.') + 1);
            break;
        case t::Title:
            if (attrs.Title.empty())
            {
                auto slash = attrs.Path.find_last_of('/');
                auto dot = attrs.Path.find_last_of('.');
                path += attrs.Path.substr(slash + 1, dot - slash - 1);
            }
            else
                path += sanitize_path(attrs.Title);
            break;
        case t::Track:
            if (attrs.Track.empty())
                path += "__";
            else
            {
                auto pos = attrs.Disc.find('/');
                bool showDisc = false;
                if (pos == string::npos)
                {
                    showDisc = (atoi(attrs.Disc.c_str()) > 1);
                }
                else
                {
                    showDisc = (atoi(attrs.Disc.c_str() + pos + 1) > 1);
                }

                if (showDisc)
                {
                    path += attrs.Disc.substr(0, pos) + ".";
                }

                if (attrs.Track.size() == 1)
                    path += "0";
                path += attrs.Track;
            }
            break;

        case t::Literal:
            path += comp.literal;
            break;
        } // switch
    } // for comp

    // No return; path was modified in-place.
}

