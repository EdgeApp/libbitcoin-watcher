CXXFLAGS += $(shell pkg-config --cflags libbitcoin-watcher) -ggdb -std=c++11
LIBS += $(shell pkg-config --libs libbitcoin-watcher)

default: all

.cpp.o:
	$(CXX) -o $@ -c $< $(CXXFLAGS)

watcher: watcher.o read_line.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

picker: picker.o
	$(CXX) $(LDFLAGS) -o $@ $^ $(LIBS)

all: picker watcher

clean:
	rm -f picker watcher
	rm -f *.o
