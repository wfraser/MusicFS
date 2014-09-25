#pragma once

#include <taglib/fileref.h>

class MusicInfo
{
public:
    MusicInfo(const char *path);

    bool has_tag() const;

    std::string title() const;
    std::string artist() const;
    std::string album() const;
    std::string comment() const;
    std::string genre() const;

    unsigned int year() const;
    unsigned int track() const;

    std::string property(const std::string& name) const;
    std::string extension() const;

private:
    const TagLib::FileRef m_fileRef;
};

