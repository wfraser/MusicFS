//
// MusicFS :: Filesystem Path Pattern Parser and Path Builder
// 
// Copyright (c) 2014-2015 by William R. Fraser
//

#pragma once

struct MusicAttributes;

class PathPattern
{
public:
    PathPattern(const char *pattern);
    void AppendPathComponent(std::string& path, const MusicAttributes& attrs, size_t level) const;
    size_t GetNumPathLevels() const;

private:
    struct Component
    {
        enum class Type
        {
            Literal,
            Artist,
            AlbumArtist,
            Album,
            Year,
            Track,
            Title,
            Extension
        };

        Component(const std::string& s) :
            type(Type::Literal),
            literal(s)
        {}

        Component(Type t) :
            type(t),
            literal()
        {}

        Type type;
        std::string literal;
    };

    std::vector<std::vector<Component>> m_components;
};

extern const char *default_pattern;
