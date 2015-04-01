//
// MusicFS :: File Metadata Extractor
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#include <typeinfo>
#include <memory>

#include <strings.h>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>
#include <taglib/mpegfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/flacfile.h>

extern int musicfs_log_level;
extern bool musicfs_log_stderr;
#define MUSICFS_LOG_COMPONENT "MusicInfo"
#include "logging.h"

#include "musicinfo.h"

using namespace std;

MusicInfo::MusicInfo(const char *path)
    : m_fileRef(path)
{
}

bool MusicInfo::has_tag() const
{
    return (m_fileRef.tag() != nullptr);
}

#define STRMETHOD(_name) string MusicInfo::_name() const \
{ \
    return m_fileRef.tag()->_name().stripWhiteSpace().to8Bit(true); \
}

#define INTMETHOD(_name) unsigned int MusicInfo::_name() const \
{ \
    return m_fileRef.tag()->_name(); \
}

STRMETHOD(title)
STRMETHOD(artist)
STRMETHOD(album)
STRMETHOD(comment)
STRMETHOD(genre)

INTMETHOD(year)
INTMETHOD(track)

string MusicInfo::albumartist() const
{
    string val = property("ALBUMARTIST");
    if (val.empty())
    {
        //DEBUG("No ALBUMARTIST for file " << m_fileRef.file()->name());
        return artist();
    }
    return val;
}

string MusicInfo::disc() const
{
    string disc = property("DISCNUMBER");
    if (disc == "1/1")
        return "";
    else
        return disc;
}

string MusicInfo::property(const string& name) const
{
    return m_fileRef.file()->properties()[name].toString().stripWhiteSpace().to8Bit(true);
}

string MusicInfo::extension() const
{
    string filename = m_fileRef.file()->name();
    return filename.substr(filename.find_last_of(".") + 1);
}
