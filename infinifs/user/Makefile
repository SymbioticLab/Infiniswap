CXX ?= g++
CPPFLAGS += -Wall -Werror -pedantic -std=c++11

mkfs.infinifs: mkfs.o block.o format.o
	$(CXX) $(LDFLAGS) mkfs.o block.o format.o -o mkfs.infinifs

mkfs.o: mkfs.cpp infinifs.hpp block.hpp
	$(CXX) $(CPPFLAGS) -c mkfs.cpp -o mkfs.o

block.o: block.cpp infinifs.hpp block.hpp
	$(CXX) $(CPPFLAGS) -c block.cpp -o block.o

format.o: format.cpp infinifs.hpp block.hpp format.hpp bit_iterator.hpp
	$(CXX) $(CPPFLAGS) -c format.cpp -o format.o

clean:
	rm -rf *.o mkfs.infinifs

.PHONY: clean
