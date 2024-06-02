# Compiler settings
CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra

# Source files
SRCS_SERVER = kierki-serwer.cpp
SRCS_CLIENT = kierki-klient.cpp

# Object files
OBJS_SERVER = obj/$(SRCS_SERVER:.cpp=.o)
OBJS_CLIENT = obj/$(SRCS_CLIENT:.cpp=.o)

# Executable name
EXEC_SERVER = server
EXEC_CLIENT = client

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