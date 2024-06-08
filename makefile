# Compiler settings
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2

# Source files
SRCS_SERVER = kierki-serwer.cpp common.h
SRCS_CLIENT = kierki-klient.cpp common.h

# Object files
OBJS_SERVER = obj/kierki-serwer.o
OBJS_CLIENT = obj/kierki-klient.o

# Executable name
EXEC_SERVER = kierki-serwer
EXEC_CLIENT = kierki-klient

all: $(EXEC_SERVER) $(EXEC_CLIENT)

$(EXEC_SERVER): $(OBJS_SERVER)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(EXEC_CLIENT): $(OBJS_CLIENT)
	$(CXX) $(CXXFLAGS) -o $@ $^

obj/%.o: %.cpp
	mkdir -p obj
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS_SERVER) $(OBJS_CLIENT) $(EXEC_SERVER) $(EXEC_CLIENT)
