TARGETS = lsozw readozw

CXXFLAGS = -Wall -g -Wno-unknown-pragmas
CPPFLAGS = -I/usr/include/openzwave
LDLIBS = -lpthread -lopenzwave

all: $(TARGETS)

$(TARGETS): %: %.o ozw_tools.o
	$(CXX) -o $@ $(LDFLAGS) $(LDLIBS) $^

%.o: %.cpp ozw_tools.h

clean:
	rm -f *~ *.o a.out
	rm -f $(TARGETS)
	rm -f zwscene.xml zwcfg_*.xml OZW_Log.txt
