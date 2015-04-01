#pragma once

class ArtistAliases;

std::vector<std::pair<int,int>> grovel(
    const std::string& path,
    MusicDatabase& db
    );

void build_paths(
    MusicDatabase& db,
    const PathPattern& pathPattern,
    const std::vector<std::pair<int,int>>& track_file_ids,
    const ArtistAliases& aliases
    );
