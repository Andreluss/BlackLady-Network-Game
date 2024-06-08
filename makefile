# Compiler settings
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -O2

# Source files
SRCS_SERVER = kierki-serwer.cpp 
SRCS_CLIENT = kierki-klient.cpp

# Object files
OBJS_SERVER = obj/kierki-serwer.o common.h
OBJS_CLIENT = obj/kierki-klient.o common.h

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
	rm -fr obj $(EXEC_SERVER) $(EXEC_CLIENT)
