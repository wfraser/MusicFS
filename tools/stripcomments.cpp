//
// Comment Tag Stripper Utility
//
// Copyright (c) 2017 by William R. Fraser
//

#include <iostream>
#include <string>
#include <vector>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

int main(int argc, char **argv)
{
    char* filename = nullptr;
    
    if (argc == 2)
    {
        filename = argv[1];
    }
    else
    {
        std::cout << "usage: stripcomments <filename>\n";
        return -1;
    }

    TagLib::FileRef f(filename);

    if (f.file() == nullptr)
    {
        std::cout << "no tags\n";
        return 1;
    }

    TagLib::PropertyMap properties = f.file()->properties();

    std::vector<TagLib::String> toRemove;
    for (auto& pair : properties)
    {
        const TagLib::String& name = pair.first;
        if (name.find("COMMENT") == 0)
        {
            std::cout << "found \"" << name.to8Bit(true) << "\"\n";
            toRemove.push_back(name);
        }
    }

    if (!toRemove.empty())
    {
        for (const auto& name : toRemove)
        {
            properties.erase(name);
        }

        TagLib::PropertyMap newProps = f.file()->setProperties(properties);
        if (newProps.isEmpty())
        {
            std::cout << "removing comment tags.\n";
            f.file()->save();
            return 0;
        } else {
            std::cout << "not saving file, because these properties would get lost:\n";
            for (const auto& pair : newProps)
            {
                std::cout << "\t" << pair.first.to8Bit(true)
                    << "\t" << pair.second.toString().to8Bit(true)
                    << std::endl;
            }
            return 1;
        }
    }
}
