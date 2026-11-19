CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -O1

.PHONY: test clean
test: test_core
	./test_core

test_core: test_core.cpp $(wildcard ../netmon/src/core/*.h)
	$(CXX) $(CXXFLAGS) -o $@ test_core.cpp

clean:
	rm -f test_core
