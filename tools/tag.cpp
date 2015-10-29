//
// Tag Extractor Utility
//
// Copyright (c) 2014-2015 by William R. Fraser
//

#include <iostream>
#include <string>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

int main(int argc, char **argv)
{
    char* filename = nullptr;
    char* tagname = nullptr;

    if (argc == 3)
    {
        tagname = argv[2];
        filename = argv[1];
    }
    else if (argc == 2)
    {
        filename = argv[1];
    }
    else
    {
        std::cout << "usage: tag [<tagname>] <filename>\n";
        return -1;
    }

    TagLib::FileRef f(filename);

    if (f.file() == nullptr)
    {
        std::cout << "no tags\n";
        return 0;
    }

    if (tagname != nullptr)
    {
        std::cout << f.file()->properties()[tagname].toString().to8Bit(true)
            << std::endl;
    }
    else
    {
        const TagLib::PropertyMap properties = f.file()->properties();

        for (const auto& pair : properties)
        {
            const TagLib::String& name = pair.first;
            const TagLib::StringList& value = pair.second;

            std::cout << name.to8Bit(true) << "\t"
                << value.toString().to8Bit(true) << std::endl;
        }
    }

    return 0;
}
