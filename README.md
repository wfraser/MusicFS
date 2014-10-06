MusicFS
=======

MusicFS is a FUSE virtual filesystem for Linux that presents an organized view of your music files, based on their tag metadata.

(It's still experimental at this time.)

Dependencies
------------

* FUSE
* TagLib
* SQLite
* A C++ compiler with C++14 support. G++ and Clang are both tested.

Why
---

If you're like me, you've got several thousand music files in various formats, accumulated over the years from numerous sources (ripped CDs, LP recordings, mixtapes from your friends, etc.).

If you've been less-than-fastidious with your organization, they're spread out in various folder structures in a pretty arbitrary way.

MusicFS goes through all these files, pulls out the information from their metadata (whatever format that is, using the awesome TagLib library), and presents them as a well-organized heirarchical tree.

By default, this goes `/<album artist>/[<year>] <album name>/<track number> - <track title>.<extension>`, but this is configurable at run-time.

How to use
----------

1. `git clone https://github.com/wfraser/MusicFS.git`
2. `cd MusicFS`
3. `make`
4. `./musicfs -v /path/to/your/music /some/mountpoint`

The first time this runs, it will take a bit of time to go through all your music.
Subsequent times will be much, much faster (it skips over un-changed files).

The organized view of your music is read-only.
If you need to update the metadata in a file, do so on the original file, unmount MusicFS, and run it again.
It will detect the changed files and update paths accordingly.

Note that MusicFS stores its data in a file named `music.db` in whatever directory `musicfs` is run from.
Make sure to always run it from this location, to save it lots of time re-examining your files.
A future update will make the location and name of the database file a configurable parameter.

Options
-------

In addition to the usual FUSE options, MusicFS supports an option for letting you configure the tree layout.
Specify `-o pattern=<pattern>` when running MusicFS. The format of the pattern is a string containing any of the following tokens (no leading slash, please):

* %albumartist%
* %artist%
* %album%
* %year%
* %genre%
* %track%
* %title%
* %ext%

The default pattern is `%albumartist%/[%year%] %album%/%track% - %title%.%ext%`.
Make sure when specifying `-o format` to use quotes as appropriate.
E.g.: `sudo ./musicfs -v -o allow_other,format="%ext%/%albumartist% - %album% (%year%)/%track% - %artist% - %title%.%ext%" /archive/music /srv/music`

Future ideas
------------

Some, all, or none of these might happen in the future:

* Read/Write mode? Allow editing metadata in-place, with MusicFS rearranging files immediately.
* Allow re-scanning files while the FS is mounted?
* Configurable database path.
