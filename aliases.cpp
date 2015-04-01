//
// MusicFS :: Artist Alias Dictionary
// 
// Copyright (c) 2014-2015 by William R. Fraser
// 

#include <iostream>
#include <fstream>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <locale>
#include <algorithm>
#include "aliases.h"

using namespace std;

ArtistAliases::ArtistAliases()
{
}

bool is_space(locale& loc, char c)
{
    return use_facet<ctype<char>>(loc).is(ctype<char>::space, c);
}

void trim_string(string& s, locale& loc)
{
    if (s.size() == 0)
        return;

    const char* start = s.c_str();
    for (char* c = &s[s.size() - 1]; c != start; c--)
    {
        if (is_space(loc, *c))
        {
            *c = '\0';
        }
        else
        {
            break;
        }
    }
}

bool ArtistAliases::ParseFile(const string& path)
{
    locale loc;
    auto tolower = [&loc](char c) -> char
    {
        return use_facet<ctype<char>>(loc).tolower(c);
    };

    ifstream f(path);
    if (!f.good())
        return false;

    const string* canonical = nullptr;
    int line_number = 0;
    string line;
    while (getline(f, line))
    {
        line_number++;

        if (line[0] == '#') // comment
            continue;

        string lower = line;
        transform(line.begin(), line.end(), lower.begin(), tolower);
        trim_string(lower, loc);
        
        if (lower.size() == 0) // empty line after trimming
            continue;

        if (is_space(loc, lower[0]))
        {
            if (canonical == nullptr)
            {
                cerr << "Error parsing aliases list: line " << line_number
                    << ": indented name cannot be first.\n";
                throw exception();
            }

            auto pair = m_map.emplace(lower, canonical);
            if (!pair.second)
            {
                cerr << "Error parsing aliases file: line " << line_number
                    << ": \"" << lower << "\" is already mapped to \""
                    << *(pair.first->second) << "\"\n";
                throw exception();
            }
        }
        else
        {
            canonical = &(*m_canonical.emplace(lower).first);
        }
    }

    return true;
}

const string* ArtistAliases::Lookup(const string& query) const
{
    locale loc;
    auto tolower = [&loc](char c) -> char
    {
        return use_facet<ctype<char>>(loc).tolower(c);
    };

    string lower;
    transform(query.begin(), query.end(), lower.begin(), tolower);

    auto pos = m_map.find(lower);
    if (pos != m_map.end())
    {
        return pos->second;
    }

    return nullptr;
}
