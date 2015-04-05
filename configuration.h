//
// MusicFS :: Configuration
//
// Copyright (c) 2015 by William R. Fraser
//

#pragma once

class Config
{
public:
    std::vector<std::string> backing_fs_paths;
    std::vector<std::string> extension_priority;
    std::string              path_pattern;
    std::string              aliases_conf;
};
