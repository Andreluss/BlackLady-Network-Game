#ifndef UNTITLED4_COMMON_H
#define UNTITLED4_COMMON_H

#define BlackLadyDebug 0

// ------------------------- Common includes -------------------------

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
#include <chrono>
#include <iomanip>


// ------------------------- Common functions -------------------------

[[noreturn]] void syserr(const char* fmt, ...) {
    va_list fmt_args;
    int org_errno = errno;

    fprintf(stderr, "\tERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);

    fprintf(stderr, " (%d; %s)\n", org_errno, strerror(org_errno));
    exit(1);
}

[[noreturn]] void fatal(const char* fmt, ...) {
    va_list fmt_args;

    fprintf(stderr, "\tERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);

    fprintf(stderr, "\n");
    exit(1);
}

void error(const char* fmt, ...) {
    va_list fmt_args;
    int org_errno = errno;

    fprintf(stderr, "\tERROR: ");

    va_start(fmt_args, fmt);
    vfprintf(stderr, fmt, fmt_args);
    va_end(fmt_args);

    if (org_errno != 0) {
        fprintf(stderr, " (%d; %s)", org_errno, strerror(org_errno));
    }
    fprintf(stderr, "\n");
}

uint16_t read_port(char const *string) {
    char *endptr;
    errno = 0;
    unsigned long port = strtoul(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number", string);
    }
    return (uint16_t) port;
}

struct sockaddr_storage get_server_address(char const *host, uint16_t port, int ai_family = AF_UNSPEC) {
    struct addrinfo hints{};
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = ai_family; // AF_UNSPEC for IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Convert port to string
    std::string port_str = std::to_string(port);

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, port_str.c_str(), &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_storage send_address{};
    memcpy(&send_address, address_result->ai_addr, address_result->ai_addrlen);

    freeaddrinfo(address_result);

    return send_address;
}

// Function that returns the string with ip and port of the given address (works for both IPv4 and IPv6)
std::string getIPAndPort(const struct sockaddr_storage& address) {
    if (address.ss_family == AF_INET) {
        char ipstr[INET_ADDRSTRLEN];
        inet_ntop(address.ss_family, &((struct sockaddr_in*)&address)->sin_addr, ipstr, sizeof(ipstr));
        int port = ntohs(((struct sockaddr_in*)&address)->sin_port);
        return std::string(ipstr) + ":" + std::to_string(port);
    } else if (address.ss_family == AF_INET6) {
        char ipstr[INET6_ADDRSTRLEN];
        inet_ntop(address.ss_family, &((struct sockaddr_in6*)&address)->sin6_addr, ipstr, sizeof(ipstr));
        int port = ntohs(((struct sockaddr_in6*)&address)->sin6_port);
        return std::string(ipstr) + ":" + std::to_string(port);
    }
    return "<ip-port-unknown>";
}

void getSocketAddresses(int socket_fd, std::string& localIpPort, std::string& remoteIpPort) {
    struct sockaddr_storage local_address{}, remote_address{};
    socklen_t address_length = sizeof(struct sockaddr_storage);

    // Get local address
    if (getsockname(socket_fd, (struct sockaddr*)&local_address, &address_length) == -1) {
        error("getsockname");
        localIpPort = "<ip-port-unknown>";
    }
    localIpPort = getIPAndPort(local_address);

    // Get remote address
    if (getpeername(socket_fd, (struct sockaddr*)&remote_address, &address_length) == -1) {
        error("getpeername");
        remoteIpPort = "<ip-port-unknown>";
    }
    remoteIpPort = getIPAndPort(remote_address);
}

// Function that returns the string with ip and port of the other side of the given socket (works for both IPv4 and IPv6)
std::string getSocketIPAndPort(int socket_fd) {
    struct sockaddr_storage address{};
    socklen_t address_length = sizeof(struct sockaddr_storage);

    // Get remote address
    if (getpeername(socket_fd, (struct sockaddr*)&address, &address_length) == -1) {
        return "<ip-port-unknown>";
    }
    // Get the IP and port of the remote side
    return getIPAndPort(address);
}

void install_signal_handler(int signal, void (*handler)(int), int flags) {
    struct sigaction action{};
    sigset_t block_mask;

    sigemptyset(&block_mask);
    action.sa_handler = handler;
    action.sa_mask = block_mask;
    action.sa_flags = flags;

    if (sigaction(signal, &action, nullptr) < 0 ){
        syserr("sigaction");
    }
}

void install_sigpipe_handler() {
    struct sigaction action{};
    action.sa_handler = SIG_IGN; // Set handler to SIG_IGN to ignore the signal
    action.sa_flags = 0;
    if (sigemptyset(&action.sa_mask) == -1) {
        syserr("Failed to set SIGPIPE handler");
    }
    if (sigaction(SIGPIPE, &action, nullptr) == -1) {
        syserr("Failed to set SIGPIPE handler");
    }
}

// helper function that results the string with the list of elements separated by the separator (with no trailing separator)
template<class It>
std::string listToString(It itFirst, It itEnd, std::function<std::string(decltype(*itFirst))> elementToStringConverters, const std::string& separator = ", ") {
    std::string result;
    for (decltype(itFirst++) it = itFirst; it != itEnd; ) {
        result += elementToStringConverters(*it);
        it++;
        if (it != itEnd) {
            result += separator;
        }
    }
    return result;
}
template<class T>
std::string listToString(const std::vector<T>& list, std::function<std::string(T)> elementToStringConverter, const std::string& separator = ", ") {
    return listToString(list.begin(), list.end(), elementToStringConverter, separator);
}

// Function that returns the string with time in a format like this: 2024-04-25T18:21:00.010 (with parts of seconds)
// https://gist.github.com/bschlinker/844a88c09dcf7a61f6a8df1e52af7730
std::string getCurrentTime() {
    const auto now = std::chrono::system_clock::now();
    const auto nowAsTimeT = std::chrono::system_clock::to_time_t(now);
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
    std::stringstream nowSs;
    nowSs << std::put_time(std::localtime(&nowAsTimeT), "%FT%T")
          << '.' << std::setfill('0') << std::setw(3) << nowMs.count();
    return nowSs.str();
}

// ----------------------------------- Common classes -----------------------------------

struct Color {
    static constexpr const char* Red = "\033[31m";
    static constexpr const char* Green = "\033[32m";
    static constexpr const char* Yellow = "\033[33m";
    static constexpr const char* Blue = "\033[34m";
    static constexpr const char* Magenta = "\033[35m";
    static constexpr const char* Cyan = "\033[36m";
    static constexpr const char* Reset = "\033[0m";
};

class Reporter {
public:
    static void debug(std::string color, std::string message) {
        if (not BlackLadyDebug) return;
        std::cerr << color << message << Color::Reset << std::endl << std::flush;
    }
    static void error(std::string message) {
        std::cerr << "############### " << Color::Red << message << Color::Reset << " ###############" << std::endl;
    }
    static void log(std::string message) {
        std::cerr << Color::Green << message << Color::Reset << std::endl;
    }
    static void logError(std::string message) {
        std::cerr << Color::Red << "[Error] " << Color::Reset << message << std::endl;
    }
    static void logWarning(std::string message) {
        std::cerr << Color::Yellow << "[Warning] " << Color::Reset << message << std::endl;
    }

    static void report(const std::string &senderIpPort, const std::string &receiverIpPort,
                       const std::string &time, const std::string &message) {
        if (BlackLadyDebug) std::cerr << std::flush;
        std::cout << "[" << senderIpPort << "," << receiverIpPort << "," << time << "] " << message << std::flush; // << std::endl;
        if (BlackLadyDebug) std::cerr << std::flush;
    }
    static void toUser(const std::string &message) {
        std::cout << message << std::endl;
    }
};

// ------ Seat enum with nextSeat and seatToString functions ------
enum class Seat {
    N = 'N',
    E = 'E',
    S = 'S',
    W = 'W',
};
Seat nextSeat(Seat seat) {
    switch (seat) {
        case Seat::N: return Seat::E;
        case Seat::E: return Seat::S;
        case Seat::S: return Seat::W;
        case Seat::W: return Seat::N;
    }
    throw std::invalid_argument("Invalid seat");
}
std::string seatToString(Seat seat) {
    switch (seat) {
        case Seat::N: return "N";
        case Seat::E: return "E";
        case Seat::S: return "S";
        case Seat::W: return "W";
    }
    Reporter::error("Invalid seat");
    return "";
}
// -----------------------------------------------------------

enum class DealType {
    // nie brać lew, za każdą wziętą lewę dostaje się 1 punkt;
    //nie brać kierów, za każdego wziętego kiera dostaje się 1 punkt;
    //nie brać dam, za każdą wziętą damę dostaje się 5 punktów;
    //nie brać panów (waletów i króli), za każdego wziętego pana dostaje się 2 punkty;
    //nie brać króla kier, za jego wzięcie dostaje się 18 punktów;
    //nie brać siódmej i ostatniej lewy, za wzięcie każdej z tych lew dostaje się po 10 punktów;
    //rozbójnik, punkty dostaje się za wszystko wymienione powyżej.
    NoTricks = 1,
    NoHearts = 2,
    NoQueens = 3,
    NoKingsJacks = 4,
    NoKingOfHearts = 5,
    No7AndLastTrick = 6,
    Robber = 7
};

enum class CardValue {
    Two,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
    Ace
};

enum class CardSuit {
    Clubs,
    Diamonds,
    Hearts,
    Spades
};

struct Card {
public:
//    Card(const Card& card) {
//        value = card.value;
//        suit = card.suit;
//    }
    Card(CardSuit suit, CardValue value) : value(value), suit(suit) {}

    CardValue value;
    CardSuit suit;
    // comparator, preserving the order of (value, suit)
    bool operator<(const Card& other) const {
        if (value < other.value) {
            return true;
        } else if (value == other.value) {
            return suit < other.suit;
        }
        return false;
    }
    bool operator==(const Card& other) const = default;

    [[nodiscard]] std::string toString() const {
        std::string valueStr;
        switch (value) {
            case CardValue::Two: valueStr = "2"; break;
            case CardValue::Three: valueStr = "3"; break;
            case CardValue::Four: valueStr = "4"; break;
            case CardValue::Five: valueStr = "5"; break;
            case CardValue::Six: valueStr = "6"; break;
            case CardValue::Seven: valueStr = "7"; break;
            case CardValue::Eight: valueStr = "8"; break;
            case CardValue::Nine: valueStr = "9"; break;
            case CardValue::Ten: valueStr = "10"; break;
            case CardValue::Jack: valueStr = "J"; break;
            case CardValue::Queen: valueStr = "Q"; break;
            case CardValue::King: valueStr = "K"; break;
            case CardValue::Ace: valueStr = "A"; break;
        }
        std::string suitStr;
        switch (suit) {
            case CardSuit::Clubs: suitStr = "C"; break;
            case CardSuit::Diamonds: suitStr = "D"; break;
            case CardSuit::Hearts: suitStr = "H"; break;
            case CardSuit::Spades: suitStr = "S"; break;
        }
        return valueStr + suitStr;
    }
    explicit Card(const std::string& cardStr) {
        std::regex card_regex(R"((10|[23456789JQKA])([CDHS]))");
        std::smatch match;
        if (std::regex_match(cardStr, match, card_regex)) {
            std::string valueStr = match[1].str();
            std::string suitStr = match[2].str();

            // Parse the value
            if (valueStr == "10") {
                value = CardValue::Ten;
            } else {
                switch (valueStr[0]) {
                    case '2': value = CardValue::Two; break;
                    case '3': value = CardValue::Three; break;
                    case '4': value = CardValue::Four; break;
                    case '5': value = CardValue::Five; break;
                    case '6': value = CardValue::Six; break;
                    case '7': value = CardValue::Seven; break;
                    case '8': value = CardValue::Eight; break;
                    case '9': value = CardValue::Nine; break;
                    case 'J': value = CardValue::Jack; break;
                    case 'Q': value = CardValue::Queen; break;
                    case 'K': value = CardValue::King; break;
                    case 'A': value = CardValue::Ace; break;
                    default: throw std::invalid_argument("Invalid card value");
                }
            }

            // Parse the suit
            switch (suitStr[0]) {
                case 'C': suit = CardSuit::Clubs; break;
                case 'D': suit = CardSuit::Diamonds; break;
                case 'H': suit = CardSuit::Hearts; break;
                case 'S': suit = CardSuit::Spades; break;
                default: throw std::invalid_argument("Invalid card suit");
            }
        }
        else {
            throw std::invalid_argument("Invalid card string");
        }
    }
};

class Msg {
public:
    [[nodiscard]] virtual std::string toString() const = 0;
    explicit operator std::string() const {
        return toString();
    }
    [[nodiscard]] virtual std::string toStringVerbose() const {
        return toString();
    }
};

class IAm : public Msg {
public:
    Seat seat;
    explicit IAm(Seat seat) : seat(seat) {}
    [[nodiscard]] std::string toString() const override {
        return "IAM" + ::seatToString(seat) + "\r\n";
    }
};
/*
 * Verbose versions of string messages:
 * Komunikacja klienta z użytkownikiem
Klient działający jako pośrednik udostępnia użytkownikowi interfejs tekstowy. Interfejs użytkownika powinien być intuicyjny. Klient wypisuje na standardowe wyjście informacje dla użytkownika i prośby od serwera o położenie karty. Klient czyta ze standardowego wejścia decyzje i polecenia użytkownika, na przykład polecenie wyświetlenia kart na ręce i wziętych lew. Klient w każdej chwili powinien móc spełnić polecenie użytkownika. Komunikacja z serwerem nie może blokować interfejsu użytkownika.

Informacje od serwera formatowane są następująco:

BUSY<lista zajętych miejsc przy stole>
Place busy, list of busy places received: <lista zajętych miejsc przy stole>.

DEAL<typ rozdania><miejsce przy stole klienta wychodzącego jako pierwszy w rozdaniu><lista kart>
New deal <typ rozdania>: staring place <miejsce przy stole klienta wychodzącego jako pierwszy w rozdaniu>, your cards: <lista kart>.

WRONG<numer lewy>
Wrong message received in trick <numer lewy>.

TAKEN<numer lewy><lista kart><miejsce przy stole klienta biorącego lewę>
A trick <numer lewy> is taken by <miejsce przy stole klienta biorącego lewę>, cards <lista kart>.

SCORE<miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów>
The scores are:
<miejsce przy stole klienta> | <liczba punktów>
<miejsce przy stole klienta> | <liczba punktów>
<miejsce przy stole klienta> | <liczba punktów>
<miejsce przy stole klienta> | <liczba punktów>

TOTAL<miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów>
The total scores are:
<miejsce przy stole klienta> | <liczba punktów>
<miejsce przy stole klienta> | <liczba punktów>
<miejsce przy stole klienta> | <liczba punktów>
<miejsce przy stole klienta> | <liczba punktów>

TRICK<numer lewy><lista kart>
Trick: (<numer lewy>) <lista kart>
Available: <lista kart, które gracz jeszcze ma na ręce>
W przypadku komunikatu TRICK użytkownik wybiera kartę do dołożenia, wpisując wykrzyknik i jej kod, np. "!10C", i naciskając enter. Ponadto użytkownik ma do dyspozycji takie polecenia, kończące się enterem:

cards – wyświetlenie listy kart na ręce;
tricks – wyświetlenie listy lew wziętych w ostatniej rozgrywce w kolejności wzięcia – każda lewa to lista kart w osobnej linii.
Wszystkie listy w komunikatach dla użytkownika są wypisywane rozdzielone przecinkami i spacjami.
 *
 */

class Busy : public Msg {
    std::vector<Seat> busy_seats;
public:
    explicit Busy(std::vector<Seat> busy_seats) : busy_seats(std::move(busy_seats)) {}
    [[nodiscard]] std::string toString() const override {
        std::string result = "BUSY";
        for (const auto& seat: busy_seats) {
            result += ::seatToString(seat);
        }
        result += "\r\n";
        return result;
    }

    [[nodiscard]] std::string toStringVerbose() const override {
        std::string result = this->toString();
        result += "Place busy, list of busy places received: ";
        result += listToString<Seat>(busy_seats, [](const Seat &seat) { return ::seatToString(seat); });
        result += ".\r\n";
        return result;
    }
};

class Deal : public Msg {
public:
    DealType dealType;
    Seat firstSeat;
    std::vector<Card> cards;
    Deal(DealType dealType, Seat firstSeat, std::vector<Card> cards) : dealType(dealType), firstSeat(firstSeat), cards(std::move(cards)) {}
    [[nodiscard]] std::string toString() const override {
        std::string result = "DEAL" + std::to_string(static_cast<int>(dealType)) + ::seatToString(firstSeat);
        for (const auto& card: cards) {
            result += card.toString();
        }
        result += "\r\n";
        return result;
    }

    [[nodiscard]] std::string toStringVerbose() const override {
        std::string result = this->toString();
        result += "New deal " + std::to_string(static_cast<int>(dealType)) + ": staring place " +
                ::seatToString(firstSeat) + ", your cards: ";
        result += listToString<Card>(cards, [](const Card &card) { return card.toString(); });
        result += ".\r\n";
        return result;
    }
};

class Trick : public Msg {
public:
    int trickNumber; // 1-13
    std::vector<Card> cards;
    static constexpr int FirstTrickNumber = 1;
    static constexpr int LastTrickNumber = 13;
    Trick(int trickNumber, std::vector<Card> cardsOnTable) : trickNumber(trickNumber), cards(std::move(cardsOnTable)) {
        assert(trickNumber >= FirstTrickNumber && trickNumber <= 13);
    }
    [[nodiscard]] std::string toString() const override {
        std::string result = "TRICK" + std::to_string(trickNumber);
        for (const auto & card : cards) {
            result += card.toString();
        }
        result += "\r\n";
        return result;
    }

    // CAUTION! The message 'Available: <lista kart, które gracz jeszcze ma na ręce>' should be printed by the caller!
    [[nodiscard]] std::string toStringVerbose() const override {
        std::string result = this->toString();
        result += "Trick: (" + std::to_string(trickNumber) + ") ";
        result += listToString<Card>(cards, [](const Card &card) { return card.toString(); });
        result += "." /*"\r\n"*/;
        return result;
    }
};

class Wrong : public Msg {
public:
    int trickNumber;
    explicit Wrong(int trickNumber) : trickNumber(trickNumber) {
        assert(trickNumber >= Trick::FirstTrickNumber && trickNumber <= 13);
    }
    [[nodiscard]] std::string toString() const override {
        return "WRONG" + std::to_string(trickNumber) + "\r\n";
    }

    [[nodiscard]] std::string toStringVerbose() const override {
        std::string result = this->toString();
        result += "Wrong message received in trick " + std::to_string(trickNumber) + ".\r\n";
        return result;
    }
};

class Taken : public Msg {
public:
    int trickNumber;
    std::vector<Card> cardsOnTable;
    Seat takerSeat;
    explicit Taken(int trickNumber, std::vector<Card> cardsOnTable, Seat takerSeat) : trickNumber(trickNumber), cardsOnTable(std::move(cardsOnTable)), takerSeat(takerSeat) {
        assert(trickNumber >= Trick::FirstTrickNumber && trickNumber <= Trick::LastTrickNumber);
    }
    [[nodiscard]] std::string toString() const override {
        std::string result = "TAKEN" + std::to_string(trickNumber);
        for (const auto & card : cardsOnTable) {
            result += card.toString();
        }
        result += ::seatToString(takerSeat) + "\r\n";
        return result;
    }

    [[nodiscard]] std::string toStringVerbose() const override {
        std::string result = this->toString();
        result += "A trick " + std::to_string(trickNumber) + " is taken by " + ::seatToString(takerSeat) + ", cards ";
        result += listToString<Card>(cardsOnTable, [](const Card &card) { return card.toString(); });
        result += ".\r\n";
        return result;
    }
};

class Score : public Msg {
public:
    std::unordered_map<Seat, int> scores;
    explicit Score(std::unordered_map<Seat, int> scores) : scores(std::move(scores)) {}
    [[nodiscard]] std::string toString() const override {
        std::string result = "SCORE";
        for (const auto& [seat, score]: scores) {
            result += ::seatToString(seat) + std::to_string(score);
        }
        result += "\r\n";
        return result;
    }
    [[nodiscard]] std::string toStringVerbose() const override {
        std::string result = this->toString();
        result += "The scores are:\n";
        for (const auto& [seat, score]: scores) {
            result += ::seatToString(seat) + " | " + std::to_string(score) + "\n";
        }
        return result;
    }
};

class Total : public Msg {
public:
    std::unordered_map<Seat, int> total_scores;
    explicit Total(std::unordered_map<Seat, int> total_scores) : total_scores(std::move(total_scores)) {}
    [[nodiscard]] std::string toString() const override {
        std::string result = "TOTAL";
        for (const auto& [seat, score]: total_scores) {
            result += ::seatToString(seat) + std::to_string(score);
        }
        result += "\r\n";
        return result;
    }
    [[nodiscard]] std::string toStringVerbose() const override {
        std::string result = this->toString();
        result += "The total scores are:\n";
        for (const auto& [seat, score]: total_scores) {
            result += ::seatToString(seat) + " | " + std::to_string(score) + "\n";
        }
        return result;
    }
};

class Parser {
public:
    static std::vector<Card> parseCards(std::string cardsStr) {
        std::vector<Card> cards;
        std::regex card_regex(R"(((10|[23456789JQKA])([CDHS])))");
        std::sregex_iterator it(cardsStr.begin(), cardsStr.end(), card_regex);
        std::sregex_iterator end;
        while (it != end) {
            std::smatch card_match = *it;
            std::string cardStr = card_match.str();
            cards.push_back(Card(cardStr));
            ++it;
        }
        return cards;
    }
    static std::shared_ptr<Msg> parse(std::string message) {
        std::smatch match;

        try {
            const std::string card_non_capturing_regex = "(?:(?:10|[23456789JQKA])[CDHS])";
            std::regex IAM_regex(R"(^IAM([NESW])\r\n$)");
            std::regex BUSY_regex(R"(^BUSY([NESW]+)\r\n$)");
            std::regex DEAL_regex(R"(^DEAL([1-7])([NESW])(((10|[23456789JQKA])[CDHS]){13})\r\n$)");
            std::regex TRICK_regex(R"(^TRICK([1-9]|1[0-3])(((10|[23456789JQKA])[CDHS]){0,3})\r\n$)");
            std::regex WRONG_regex(R"(^WRONG([1-9]|1[0-3])\r\n$)");
            std::regex TAKEN_regex(R"(^TAKEN([1-9]|1[0-3])((?:(?:10|[23456789JQKA])[CDHS]){4})([NESW])\r\n$)");
            std::regex SCORE_regex(R"(^SCORE([NESW])(\d+)([NESW])(\d+)([NESW])(\d+)([NESW])(\d+)\r\n$)");
            std::regex TOTAL_regex(R"(^TOTAL([NESW])(\d+)([NESW])(\d+)([NESW])(\d+)([NESW])(\d+)\r\n$)");

            if (std::regex_match(message, match, IAM_regex)) {
                Seat seat = Seat(match[1].str()[0]);
                return std::make_shared<IAm>(seat);
            } else if (std::regex_match(message, match, BUSY_regex)) {
                std::string seatsStr = match[1].str();
                std::vector<Seat> busySeats;
                for (char seatChar: seatsStr) {
                    busySeats.push_back(Seat(seatChar));
                }
                // ensure that there are no repeated seats
                std::set<Seat> busySeatsSet(busySeats.begin(), busySeats.end());
                if (busySeats.size() != busySeatsSet.size()) {
                    Reporter::error("Repeated seats in BUSY message");
                    return nullptr;
                }
                return std::make_shared<Busy>(busySeats);
            } else if (std::regex_match(message, match, DEAL_regex)) {
                DealType dealType = static_cast<DealType>(std::stoi(match[1].str()));
                Seat firstSeat = Seat(match[2].str()[0]);
                std::string cardsStr = match[3].str();
                std::vector<Card> cards = parseCards(cardsStr);
                // ensure that there are no repeated cards
                std::set<Card> cardsSet(cards.begin(), cards.end());
                if (cards.size() != cardsSet.size()) {
                    Reporter::error("Repeated cards in DEAL message");
                    return nullptr;
                }
                return std::make_shared<Deal>(dealType, firstSeat, cards);
            } else if (std::regex_match(message, match, TRICK_regex)) {
                int trickNumber = std::stoi(match[1].str());
                std::string cardsStr = match[2].str();
                auto cards = parseCards(cardsStr);
                return std::make_shared<Trick>(trickNumber, cards);
            } else if (std::regex_match(message, match, WRONG_regex)) {
                int trickNumber = std::stoi(match[1].str());
                return std::make_shared<Wrong>(trickNumber);
            } else if (std::regex_match(message, match, TAKEN_regex)) {
                int trickNumber = std::stoi(match[1].str());
                std::string cardsStr = match[2].str();
                auto cards = parseCards(cardsStr);
                Seat takerSeat = Seat(match[3].str()[0]);
                return std::make_shared<Taken>(trickNumber, cards, takerSeat);
            } else if (std::regex_match(message, match, SCORE_regex)) {
                std::unordered_map<Seat, int> scores;
                for (int i = 0; i < 4; ++i) {
                    Seat seat = Seat(match[i * 2 + 1].str()[0]);
                    int score = std::stoi(match[i * 2 + 2].str());
                    scores[seat] = score;
                }
                return std::make_shared<Score>(scores);
            } else if (std::regex_match(message, match, TOTAL_regex)) {
                std::unordered_map<Seat, int> total_scores;
                for (int i = 0; i < 4; ++i) {
                    Seat seat = Seat(match[i * 2 + 1].str()[0]);
                    int score = std::stoi(match[i * 2 + 2].str());
                    total_scores[seat] = score;
                }
                return std::make_shared<Total>(total_scores);
            }
        }
        catch (std::invalid_argument& e) {
            Reporter::debug(Color::Red, e.what());
        }

        return nullptr; // Message not recognized
    }
};



class PollBuffer {
private:
    std::string buffer_in_msg_separator;
    std::string buffer_in, buffer_out; // maybe not both used
    struct pollfd* pollfd;
    bool error = false;

    bool updateErrors() {
        if (pollfd->revents & POLLERR) {
            error = true;
            return true;
        }
        if (pollfd->revents & POLLHUP) {
            Reporter::debug(Color::Red, "POLLHUP detected.");
            error = true;
            return true;
        }
        error = false;
        return false;
    }
    void updatePollIn() {
        if (pollfd->revents & POLLIN) {
            static char buffer[1024];
            ssize_t size = read(pollfd->fd, buffer, sizeof(buffer));
            if (size < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    Reporter::debug(Color::Yellow, "Read would block - skipping.");
                    return;
                }
                Reporter::debug(Color::Red, "Connection closed " + getSocketIPAndPort(pollfd->fd) + " due to error.");
                error = true; return;
            }
            if (size == 0) {
                Reporter::debug(Color::Blue, "Connection with " + getSocketIPAndPort(pollfd->fd) + " closed with EOF.");
                error = true; // closed connection is also an error for SafePoll
                return;
            }

            buffer_in += std::string(buffer, size);
        }
    }
    void updatePollOut() {
        if (pollfd->revents & POLLOUT) {
            ssize_t size = write(pollfd->fd, buffer_out.c_str(), buffer_out.size());
            if (size < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    Reporter::debug(Color::Yellow, "Write would block - skipping.");
                    return;
                }
                Reporter::debug(Color::Red, "Connection with " + getSocketIPAndPort(pollfd->fd) + " closed due to error.");
                error = true; return;
            }
            if (size == 0) {
                Reporter::debug(Color::Blue, "Connection with " + getSocketIPAndPort(pollfd->fd) + " closed <-- EOF.");
                error = true; // closed connection is also an error for SafePoll
                return;
            }

            buffer_out.erase(0, size);
            if (buffer_out.empty()) {
                // remove the POLLOUT flag
                pollfd->events &= ~POLLOUT;
            }
        }
    }

    bool reporting_enabled = true;
public:
    explicit PollBuffer(struct pollfd *pollfd = nullptr, bool enable_reporting = true, std::string msg_separator = "\r\n") {
        buffer_in_msg_separator = std::move(msg_separator);
        this->reporting_enabled = enable_reporting;
        this->pollfd = pollfd;
        if (pollfd != nullptr && pollfd->fd != -1) {
            pollfd->events = POLLIN;
        }
    }


    // function for making sure the client is disconnected and clearing its players (it's called after a poll error)
    void disconnect() {
        // clear the buffers
        buffer_in.clear();
        buffer_out.clear();

        if (pollfd != nullptr) {
            // close the socket
            if (pollfd->fd != -1)
                close(pollfd->fd);

            // clear the pollfd structure
            pollfd->fd = -1;
            pollfd->events = 0;
            pollfd->revents = 0;

            pollfd = nullptr; // drop the pointer
        }
    }
    // function called when settings the PollBuffer object for a new client that has just connected (and it's descriptor is in the fds array)
    void connect(struct pollfd* _pollfd) {
        // clear the buffers
        buffer_in.clear();
        buffer_out.clear();

        // set the pollfd structure
        this->pollfd = _pollfd;
        assert(pollfd != nullptr);
        assert(pollfd->fd != -1);

        // for now, set the pollfd events to POLLIN *only*
        pollfd->events = POLLIN;
    }

    void update() {
        if (!isConnected()) {
            Reporter::error("Tried to update a disconnected buffer.");
            return;
        }
        // check if any error occurred and if so, the buffer is broken and the client should be disconnected and his data cleared
        if (updateErrors()) {
            disconnect();
            return;
        }
        updatePollIn();
        updatePollOut();
//        Reporter::debug(Color::Yellow, "Buffer in: " + buffer_in);
    }
    [[nodiscard]] bool hasError() const {
        return error;
    }
    [[nodiscard]] bool hasMessage() const {
        return buffer_in.find(buffer_in_msg_separator) != std::string::npos;
    }
    // ------------------------------->---------------------------->---------------------------------->
    // N E S W | TRICK -> N | wait (no msg) | safePoll (S disconnected) | safePoll ... |  safePoll ... | safePoll (S connected) | DEAL -> S
    //                                                                    N -> TRICK   |
    // TRICK -> N
    // DEAL -> S   time 4.1
    // N -> TRICK  time 4.2  (real time 1.3)
    //
    std::string readMessage() {
        assert(hasMessage());
        // return the message including the separator and remove it from the buffer
        auto pos = buffer_in.find(buffer_in_msg_separator);
        std::string message = buffer_in.substr(0, pos + buffer_in_msg_separator.size());
        buffer_in.erase(0, pos + buffer_in_msg_separator.size());

        if (reporting_enabled) {
            std::string localIpPort, remoteIpPort;
            getSocketAddresses(pollfd->fd, localIpPort,remoteIpPort);
            Reporter::report(remoteIpPort, localIpPort, getCurrentTime(), message);
        }

        return message;
    }

    bool isWriting() {
        return !buffer_out.empty();
    }

    void writeMessage(const std::string& message) {
        assert(!message.empty());
        buffer_out += message;
        pollfd->events |= POLLOUT; // add the POLLOUT flag

        if (reporting_enabled) {
            std::string localIpPort, remoteIpPort;
            getSocketAddresses(pollfd->fd, localIpPort,remoteIpPort);
            Reporter::report(localIpPort, remoteIpPort, getCurrentTime(), message);
        }
    }

    void writeMessage(const Msg& message) {
        writeMessage(message.toString());
    }

    [[nodiscard]] bool isConnected() const {
        return pollfd != nullptr && pollfd->fd != -1;
    }

    int _flushWrite() {
        while (!buffer_out.empty()) {
            ssize_t size = write(pollfd->fd, buffer_out.c_str(), buffer_out.size());
            if (size < 0) {
                Reporter::debug(Color::Red, "Flushing write buffer failed.");
                return -1;
            }
            if (size == 0) {
                Reporter::debug(Color::Blue, "Flushing write buffer stopped - connection closed with EOF.");
                return 0;
            }
            buffer_out.erase(0, size);
        }
        return 0;
    }
    // Danger: this function is *blocking* to flush the whole output buffer!
    // It writes directly to the socket and waits until the whole buffer is written.
    void flush() {
        // Change the socket to blocking mode.
        // This is necessary to ensure that the whole buffer is written.
        int flags = fcntl(pollfd->fd, F_GETFL, 0);
        fcntl(pollfd->fd, F_SETFL, flags & ~O_NONBLOCK);

        _flushWrite();

        // Change the socket back to non-blocking mode (if it was non-blocking before).
        fcntl(pollfd->fd, F_SETFL, flags);
    }
};

struct PlayerStats {
    int points_deal = 0;
    int points_total = 0;
    std::set<Card> hand;
    std::vector<std::vector<Card>> tricks_taken; // in the last deal
    int getCurrentTrickNumber() const {
        // 1-based, deduced by hand size, it's 13 initially
        return 13 - hand.size() + 1;
    }
    DealType _currentDealType = DealType::Robber;
    [[nodiscard]] DealType getCurrentDealType() const {
        return _currentDealType;
    }

    [[nodiscard]] std::string availableCardsToString() const {
        std::string result = "Available: ";
        result += listToString(hand.begin(), hand.end(), [](Card card) {
            return card.toString();
        }, ", ");
//        result += "\r\n";
        return result;
    }

    [[nodiscard]] bool hasCard(const Card& card) {
        return hand.find(card) != hand.end();
    }
    [[nodiscard]] bool hasSuit(CardSuit suit) const {
        return std::any_of(hand.begin(), hand.end(), [suit](const Card& card) {
            return card.suit == suit;
        });
    }
    void removeCard(const Card& card) {
        hand.erase(card);
    }

    void takeTrick(const std::vector<Card>& cards, int points) {
        tricks_taken.push_back(cards);
        points_deal += points;
        points_total += points;
    }

    void takeNewDeal(const std::vector<Card>& newHand, DealType dealType) {
        _currentDealType = dealType;
        tricks_taken.clear();
        hand.clear();
        hand.insert(newHand.begin(), newHand.end());
        points_deal = 0;
    }
};



#endif //UNTITLED4_COMMON_H
