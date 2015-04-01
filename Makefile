VERSION=MusicFS v0.0pre\
$(shell test -d .git && echo "\ngit revision" && git log --pretty="format:%h %ai" -n1)\
\nbuilt $(shell date "+%Y-%m-%d %H:%M:%S %z")\n

DEFINES=-DFUSE_USE_VERSION=28 \
        -D_FILE_OFFSET_BITS=64 \
        -DMUSICFS_VERSION="\"$(VERSION)\"" \

CXXFLAGS+=-std=c++14 -Wall -pedantic $(DEFINES) -I/usr/include/fuse -g
LDFLAGS+=-Wall -lstdc++ -lm -ltag -lfuse -lsqlite3

all: musicfs

OBJS=main.o musicinfo.o database.o groveler.o path_pattern.o aliases.o

musicfs: $(OBJS)
	$(CXX) $(OBJS) $(LDFLAGS) -o musicfs

.PHONY: tools
tools: tools/checkempty tools/tag

clean:
	rm -f *.o musicfs tools/checkempty tools/tag
