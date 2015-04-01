#define _DEFAULT_SOURCE
#include <iostream>
#include <vector>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

using namespace std;

bool is_dir(const char *path)
{
    struct stat statbuf;
    if (stat(path, &statbuf) == -1)
    {
        cerr << path << ": ";
        perror("stat");
        return false;
    }

    return S_ISDIR(statbuf.st_mode);
}

// Note: modifies path in place.
int check_dir(string *path)
{
    DIR *d = opendir(path->c_str());
    if (d == nullptr)
    {
        cerr << "error opening " << *path << ": " << strerror(errno) << endl;
        return 0;
    }

    dirent *ent;
    int num = 0;
    size_t original_path_len = path->length();
    while ((ent = readdir(d)) != nullptr)
    {
        if (strcmp(ent->d_name, ".") == 0
                || strcmp(ent->d_name, "..") == 0)
            continue;

        path->push_back('/');
        path->append(ent->d_name);

        if (is_dir(path->c_str()))
        {
            num += check_dir(path);
        }
        else
        {
            num++;
        }
        path->resize(original_path_len);
    }

    closedir(d);

    if (num == 0)
    {
        cout << *path << endl;
    }

    return num;
}

void usage(const char *progname)
{
//           ##############################################################################
    cerr << "usage: " << progname << " [path]\n"
            "   Checks the given directory tree (or the current directory, if omitted) for\n"
            "   directories which (recursively) contain no files.\n";
}

int main(int argc, char **argv)
{
    string path;
    if (argc == 2)
    {
        if (strcmp(argv[1], "-h") == 0
                || strcmp(argv[1], "--help") == 0)
        {
            usage(argv[0]);
            return -1;
        }
        else
        {
            path = argv[1];
        }
    }
    else if (argc == 1)
    {
        path = ".";
    }
    else if (argc == 3 && strcmp(argv[1], "--") == 0)
    {
        path = argv[2];
    }
    else
    {
        cerr << "error: too many arguments\n";
        usage(argv[0]);
        return -1;
    }
    check_dir(&path);
    return 0;
}
