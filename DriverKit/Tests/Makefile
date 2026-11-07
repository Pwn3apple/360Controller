CXX ?= c++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -g

test_protocol: test_protocol.cpp ../Core/XboxProtocol.cpp ../Core/XboxProtocol.h ../Shared/XboxDriverShared.h
	$(CXX) $(CXXFLAGS) -o $@ test_protocol.cpp ../Core/XboxProtocol.cpp

test: test_protocol
	./test_protocol

clean:
	rm -f test_protocol

.PHONY: test clean
