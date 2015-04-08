//
// MusicFS :: SQLite3 Database Helper Functions
//
// Copyright (c) 2015 by William R. Fraser
//

#pragma once

namespace DBHelpers
{
    // Intentionally left undefined, because there is no sane default behavior.
    // Will cause a link error if none of the template specializations below match.
    template <typename T>
    T GetColumn(sqlite3_stmt *prepared, int columnIndex);

    template <>
    const char* GetColumn(sqlite3_stmt *prepared, int columnIndex)
    {
        return reinterpret_cast<const char*>(sqlite3_column_text(prepared, columnIndex));
    }

    template <>
    std::string GetColumn(sqlite3_stmt *prepared, int columnIndex)
    {
        // This is just an alternate form for:
        return GetColumn<const char*>(prepared, columnIndex);
    }

    template <>
    std::vector<std::string> GetColumn(sqlite3_stmt *prepared, int columnIndex)
    {
        int nBytes = sqlite3_column_bytes(prepared, columnIndex);
        auto bytes = reinterpret_cast<const char*>(sqlite3_column_blob(prepared, columnIndex));
        assert(bytes[nBytes - 1] == '\0');

        std::vector<std::string> strings(1, "");
        for (int i = 0; i < nBytes - 1; i++)
        {
            if (bytes[i] == '\0')
            {
                strings.emplace_back("");
            }
            else
            {
                strings.back().push_back(bytes[i]);
            }
        }

        return strings;
    }

    template <>
    int GetColumn(sqlite3_stmt *prepared, int columnIndex)
    {
        return sqlite3_column_int(prepared, columnIndex);
    }

    template <>
    int64_t GetColumn(sqlite3_stmt *prepared, int columnIndex)
    {
        return sqlite3_column_int64(prepared, columnIndex);
    }

    template <typename T>
    T GetColumnOrDefault(sqlite3_stmt *prepared, int columnIndex, T&& defaultValue = T())
    {
        if (sqlite3_column_type(prepared, columnIndex) == SQLITE_NULL)
            return defaultValue;
        else
            return GetColumn<T>(prepared, columnIndex);
    }

    // Intentionally left undefined, because there is no sane default behavior.
    // Will cause a link error if none of the template specializations below match.
    template <typename T>
    int BindColumn(sqlite3_stmt *prepared, int columnIndex, const T&);

    template <>
    int BindColumn(sqlite3_stmt *prepared, int columnIndex, const std::string& s)
    {
        return sqlite3_bind_text(prepared, columnIndex, s.c_str(), s.size(), nullptr);
    }

    template <>
    int BindColumn(sqlite3_stmt *prepared, int columnIndex, const std::vector<std::string>& vec)
    {
        std::vector<char> blob;
        for (const std::string& s : vec)
        {
            blob.insert(blob.end(), s.begin(), s.end());
            blob.push_back('\0');
        }

        return sqlite3_bind_blob(prepared, columnIndex, blob.data(), blob.size(), SQLITE_TRANSIENT);
    }
   
    template <>
    int BindColumn(sqlite3_stmt *prepared, int columnIndex, const int& value)
    {
        return sqlite3_bind_int(prepared, columnIndex, value);
    }

    template <>
    int BindColumn(sqlite3_stmt *prepared, int columnIndex, const unsigned int& value)
    {
        return sqlite3_bind_int(prepared, columnIndex, value);
    }

    template <>
    int BindColumn(sqlite3_stmt *prepared, int columnIndex, const long& value)
    {
        return sqlite3_bind_int(prepared, columnIndex, value);
    }

    template <>
    int BindColumn(sqlite3_stmt *prepared, int columnIndex, const std::nullptr_t&)
    {
        return sqlite3_bind_null(prepared, columnIndex);
    }
}
