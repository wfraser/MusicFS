#include <iostream>
#include <string>
#include <vector>

#include "path_pattern.h"

using namespace std;

const char *default_pattern = "%albumartist%/[%year%] %album%/%track% - %title%.%ext%";

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

#if 0
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
#endif
}
