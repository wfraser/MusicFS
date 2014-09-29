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

unique_ptr<TagLib::File> FileSpecialized(const string& path)
{
    string ext = path.substr(path.find_last_of('.') + 1);
    if (strcasecmp(ext.c_str(), "flac") == 0)
    {
        return make_unique<TagLib::FLAC::File>(path.c_str());
    }
    else if (strcasecmp(ext.c_str(), "mp3") == 0)
    {
        return make_unique<TagLib::MPEG::File>(path.c_str());
    }
    else
    {
        return nullptr;
    }
    //TODO: implement this for other types
    // See my TagLib mailing list thread here:
    // http://mail.kde.org/pipermail/taglib-devel/2014-September/002700.html
}

string MusicInfo::albumartist() const
{
    string val = property("ALBUMARTIST");
    if (val.empty())
    {
        DEBUG("No ALBUMARTIST for file " << m_fileRef.file()->name());
        return artist();
    }
    return val;
    
#if 0
    string path = m_fileRef.file()->name();
    unique_ptr<TagLib::File> file = FileSpecialized(path);

    string val = (file == nullptr) ? "" : file->properties()["ALBUMARTIST"].toString().to8Bit(true);
        
    if (!val.empty())
    {
        return val;
    }
    else
    {
        DEBUG("No ALBUMARTIST in tag for file " << path);
        return artist();
    }
#endif
}

unsigned int MusicInfo::disc() const
{
    string val = property("DISCNUMBER");
    return atoi(val.c_str());

#if 0
    string path = m_fileRef.file()->name();
    unique_ptr<TagLib::File> file = FileSpecialized(path);
    
    for (const auto& p : file->properties())
    {
        cout << p.first << "/" << p.second << endl;
    }
    
    return file->properties()["DISCNUMBER"].toString().toInt();
#endif
}

string MusicInfo::property(const string& name) const
{
    for (const auto& p : m_fileRef.file()->properties())
    {
        DEBUG(p.first << "/" << p.second);
    }

    return m_fileRef.file()->properties()[name].toString().to8Bit(true);
}

string MusicInfo::extension() const
{
    string filename = m_fileRef.file()->name();
    return filename.substr(filename.find_last_of(".") + 1);
}