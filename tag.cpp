#include <iostream>
#include <string>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

int main(int argc, char **argv)
{
    if (argc != 3)
    {
        std::cout << "usage: tag <tagname> <filename>\n";
        return -1;
    }

    TagLib::FileRef f(argv[2]);

    if (f.file() == nullptr)
    {
        std::cout << "no tags\n";
        return 0;
    }

    std::cout << f.file()->properties()[argv[1]].toString().to8Bit(true)
        << std::endl;

    return 0;
}
