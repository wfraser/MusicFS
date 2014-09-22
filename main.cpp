#include <iostream>
#include <sstream>

#include "MusicInfo.h"

using namespace std;

string atoi(int i)
{
    stringstream ss;
    ss << i;
    return ss.str();
}

string make_path(const MusicInfo& info, const string& format)
{
    string result;

    bool in_field = false;
    string field;
    for (size_t i = 0, n = format.size(); i < n; i++)
    {
        char c = format[i];
        if (c == '%')
        {
            if (in_field)
            {
                if (field == "year")
                {
                    result.append(atoi(info.year()));
                }
                else if (field == "track")
                {
                    result.append(atoi(info.track()));
                }
                else if (field == "ext")
                {
                    result.append(info.extension());
                }
                else
                {
                    result.append(info.property(field));
                }
                field.clear();
                in_field = false;
            }
            else
            {
                in_field = true;
            }
        }
        else if (in_field)
        {
            field.push_back(c);
        }
        else
        {
            result.push_back(c);
        }
    }

    return result;
}

int main(int argc, char **argv)
{
    string format;
    if (argc > 2)
    {
        format = argv[1];
        argv++;
    }
    else
    {
        format = "%artist%/[%year%] %album%/%track% - %title%.%ext%";
    }

    MusicInfo f(argv[1]);

    cout << make_path(f, format) << endl;

    return 0;
}
