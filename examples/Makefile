CXXFLAGS=$(shell pkg-config --cflags libbitcoin-client) -ggdb
LIBS=$(shell pkg-config --libs libbitcoin-client)

default: all

.cpp.o:
	$(CXX) -o $@ -c $< $(CXXFLAGS)

watcher: watcher.o
	$(CXX) -o $@ $< $(LIBS)

picker: picker.o
	$(CXX) -o $@ $< $(LIBS)

all: watcher picker

clean:
	rm -f watcher
	rm -f *.o
