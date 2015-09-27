TARGETS = lsozw

CXXFLAGS = -Wall -g -Wno-unknown-pragmas
CPPFLAGS = -I/usr/include/openzwave
LDLIBS = -lpthread -lopenzwave

all: $(TARGETS)

clean:
	rm -f *~ *.o a.out
	rm -f $(TARGETS)
	rm -f zwscene.xml zwcfg_*.xml OZW_Log.txt
