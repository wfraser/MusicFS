#pragma once

#include <unordered_map>
#include <unordered_set>

class ArtistAliases
{
public:
    ArtistAliases();
    bool ParseFile(const std::string& file_path);
    const std::string* Lookup(const std::string& query) const;

private:
    std::unordered_set<std::string> m_canonical;
    std::unordered_map<std::string, const std::string*> m_map;
};
