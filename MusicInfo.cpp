#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

#include "MusicInfo.h"

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
    return m_fileRef.tag()->_name().to8Bit(true); \
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

string MusicInfo::property(const string& name) const
{
    return m_fileRef.tag()->properties()[name].toString().to8Bit(true);
}

string MusicInfo::extension() const
{
    string filename = m_fileRef.file()->name();
    return filename.substr(filename.find_last_of(".") + 1);
}
