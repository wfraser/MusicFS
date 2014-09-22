VERSION=MusicFS v0.0pre\
$(shell test -d .git && echo "\ngit revision" && git log --pretty="format:%h %ai" -n1)\
\nbuilt $(shell date "+%Y-%m-%d %H:%M:%S %z")\n

DEFINES=-DFUSE_USE_VERSION=28 \
        -D_FILE_OFFSET_BITS=64 \
        -DMUSICFS_VERSION="\"$(VERSION)\"" \

CXXFLAGS+=-std=c++14 -Wall -pedantic $(DEFINES) -I/usr/include/fuse -g
LFLAGS+=-Wall -lstdc++ -ltag -lfuse

all: musicfs

OBJS=main.o MusicInfo.o

musicfs: $(OBJS)
	$(CXX) $(OBJS) $(LFLAGS) -o musicfs

clean:
	rm -f *.o musicfs
