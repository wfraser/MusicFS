#pragma once

struct path_building_component
{
    enum class Type
    {
        Literal,
        Artist,
        AlbumArtist,
        Album,
        Genre,
        Year,
        Track,
        Title,
        Extension
    };

    path_building_component(std::string s)
        : type(Type::Literal)
        , literal(s)
    {}

    path_building_component(Type t)
        : type(t)
        , literal()
    {}

    Type type;
    std::string literal;
};

void parse_pattern(
    std::vector<std::vector<path_building_component>>& path_components,
    const char *pattern
    );

extern const char *default_pattern;
