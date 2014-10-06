#pragma once

std::vector<int> grovel(
    const std::string& path,
    MusicDatabase& db
    );

void build_paths(
    MusicDatabase& db,
    const PathPattern& pathPattern,
    const std::vector<int>& track_ids
    );
