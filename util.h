#pragma once

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
