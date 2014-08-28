CXXFLAGS += $(shell pkg-config --cflags libbitcoin-watcher) -ggdb -std=c++11
LIBS += $(shell pkg-config --libs libbitcoin-watcher)

default: all

all: watcher

.cpp.o:
	$(CXX) -o $@ -c $< $(CXXFLAGS)

watcher: watcher.o
	$(CXX) $(LDFLAGS) -o $@ $< $(LIBS)

clean:
	rm -f watcher
	rm -f *.o
