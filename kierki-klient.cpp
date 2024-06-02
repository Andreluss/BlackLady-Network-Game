#include <stdexcept>
#include <optional>
#include <string>
#include <sys/poll.h>
#include <utility>
#include <vector>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <cassert>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <cinttypes>
#include <netdb.h>
#include <unistd.h>
#include <csignal>
#include <fcntl.h>
#include <regex>
#include <set>
#include <unordered_set>
#include <fstream>
#include <queue>

#include "common.h"



int main() {
    Reporter::log("Hello, World!");
    std::cout << "hw";
}