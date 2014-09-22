CXXFLAGS+=-std=c++14 -Wall -pedantic -g
LFLAGS+=-Wall -lstdc++ -ltag

all: musicfs

OBJS=main.o MusicInfo.o

musicfs: $(OBJS)
	$(CXX) $(OBJS) $(LFLAGS) -o musicfs

clean:
	rm -f *.o musicfs
