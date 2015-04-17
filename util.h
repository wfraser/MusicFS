//
// MusicFS :: Miscellaneous Utility Functions
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#pragma once

#define countof(_) (sizeof(_)/sizeof(*_))

inline std::string join(const std::vector<std::string>& vec, const std::string& separator)
{
    std::stringstream result;
    for (size_t i = 0, n = vec.size(); i < n; i++)
    {
        result << vec[i];
        if (i != n - 1)
            result << separator;
    }
    return result.str();
}

inline bool iendsWith(const std::string& haystack, const std::string& needle)
{
    return (haystack.size() >= needle.size())
        && (strcasecmp(needle.c_str(), haystack.c_str() + (haystack.size() - needle.size())) == 0);
}
