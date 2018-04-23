//
// MusicFS :: Artist Alias Dictionary
// 
// Copyright (c) 2014-2015 by William R. Fraser
// 

#include <algorithm>
#include <fstream>
#include <functional>
#include <cctype>
#include <iostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include "aliases.h"

#define MUSICFS_LOG_SUBSYS "ArtistAliases"
#include "logging.h"

using namespace std;

ArtistAliases::ArtistAliases()
{
}

void trim_string_end(string& s)
{
    if (s.size() == 0)
        return;

    const char* start = s.c_str();
    for (char* c = &s[s.size() - 1]; c != start; c--)
    {
        if (isspace(*c))
        {
            s.pop_back();
        }
        else
        {
            break;
        }
    }
}

void trim_string_front(string& s)
{
    if (s.size() == 0)
        return;

    size_t start = 0;
    for (; start < s.size(); start++)
    {
        if (!isspace(s[start]))
            break;
    }

    if (start != 0)
    {
        s.replace(0, s.size() - start, s, start, -1);
        s.resize(s.size() - start);
    }
}

bool ArtistAliases::ParseFile(const string& path)
{
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

        trim_string_end(line);
        
        if (line.size() == 0) // empty line after trimming
            continue;

        if (isspace(line[0]))
        {
            if (canonical == nullptr)
            {
                cerr << "Error parsing aliases list: line " << line_number
                    << ": indented name cannot be first.\n";
                throw exception();
            }

            trim_string_front(line);
            string lower;
            transform(line.begin(), line.end(), back_inserter(lower), ::tolower);

            DEBUG(lower << " -> " << *canonical);
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
            canonical = &(*m_canonical.emplace(line).first);
        }
    }

    return true;
}

const string* ArtistAliases::Lookup(const string& query) const
{
    string lower;
    transform(query.begin(), query.end(), back_inserter(lower), ::tolower);

    auto pos = m_map.find(lower);
    if (pos != m_map.end())
    {
        return pos->second;
    }

    return nullptr;
}
