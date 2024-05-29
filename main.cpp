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
#include <ranges>
#include <regex>
#include <set>
#include <unordered_set>

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

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, nullptr, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_storage send_address{};
    memcpy(&send_address, address_result->ai_addr, address_result->ai_addrlen);

    freeaddrinfo(address_result);

    return send_address;
}

void getSocketAddresses(int socket_fd, std::string& localIP, int& localPort, std::string& remoteIP, int& remotePort) {
    struct sockaddr_storage local_address{}, remote_address{};
    socklen_t address_length = sizeof(struct sockaddr_storage);

    // Get local address
    if (getsockname(socket_fd, (struct sockaddr*)&local_address, &address_length) == -1) {
        // Handle error
    }
    char ipstr[INET6_ADDRSTRLEN];
    inet_ntop(local_address.ss_family, &local_address, ipstr, sizeof(ipstr));
    localIP = ipstr;
    localPort = ntohs(((struct sockaddr_in*)&local_address)->sin_port);

    // Get remote address
    if (getpeername(socket_fd, (struct sockaddr*)&remote_address, &address_length) == -1) {
        // Handle error
    }
    inet_ntop(remote_address.ss_family, &remote_address, ipstr, sizeof(ipstr));
    remoteIP = ipstr;
    remotePort = ntohs(((struct sockaddr_in*)&remote_address)->sin_port);
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
        std::cerr << color << message << Color::Reset << std::endl;
    }
    static void error(std::string message) {
        std::cerr << "[------------]" << Color::Red << message << Color::Reset << std::endl;
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

    static void report(const std::string &senderIP, int senderPort, const std::string &receiverIP, int receiverPort,
                       const std::string &time, const std::string &message) {
        std::cout << "[" << senderIP << ":" << senderPort << "," << receiverIP << ":" << receiverPort << "," << time
                  << "] " << message << std::endl;
    }
};

// Write a function that returns the string with time in such a format: 2024-04-25T18:21:00.010 (with parts of seconds)
std::string getCurrentTime() {
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    char timeStr[30];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%dT%H:%M:%S", localtime(&ts.tv_sec));
    char msStr[5];
    sprintf(msStr, ".%03ld", ts.tv_nsec / 1000000);
    return std::string(timeStr) + msStr;
}

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

std::string toString(Seat seat) {
    switch (seat) {
        case Seat::N: return "N";
        case Seat::E: return "E";
        case Seat::S: return "S";
        case Seat::W: return "W";
    }
    Reporter::error("Invalid seat");
    return "";
}

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
    Card(const Card& card) {
        value = card.value;
        suit = card.suit;
    }

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
    }
};

/*
 * Serwer i klient przesyłają następujące komunikaty.

    IAM<miejsce przy stole>\r\n     (for example IAMN\r\n)
    Komunikat wysyłany przez klienta do serwera po nawiązaniu połączenia. Informuje, które miejsce przy stole chce zająć klient. Jeśli klient nie przyśle takiego komunikatu w czasie timeout, serwer zamyka połączenie z tym klientem. W ten sposób serwer traktuje również klienta, który po nawiązaniu połączenia przysłał błędny komunikat.

    Busy<lista zajętych miejsc przy stole>\r\n     (for example BusyNS\r\n)
    Komunikat wysyłany przez serwer do klienta, jeśli wybrane miejsce przy stole jest już zajęte. Jednocześnie informuje go, które miejsca przy stole są zajęte. Po wysłaniu tego komunikatu serwer zamyka połączenie z klientem. W ten sposób serwer traktuje również klienta, który próbuje podłączyć się do trwającej rozgrywki.

    DEAL<typ rozdania><miejsce przy stole klienta wychodzącego jako pierwszy w rozdaniu><lista kart>\r\n
    Komunikat wysyłany przez serwer do klientów po zebraniu się czterech klientów. Informuje o rozpoczęciu rozdania. Lista zawiera 13 kart, które klient dostaje w tym rozdaniu.

    TRICK<numer lewy><lista kart>\r\n    (for example TRICK1\r\n)
    Komunikat wysyłany przez serwer do klienta z prośbą o położenie karty na stole. Lista kart zawiera od zera do trzech kart aktualnie leżących na stole. Jeśli klient nie odpowie w czasie timeout, to serwer ponawia prośbę. Komunikat wysyłany przez klienta do serwera z kartą, którą klient kładzie na stole (lista kart zawiera wtedy jedną kartę).

    WRONG<numer lewy>\r\n
    Komunikat wysyłany przez serwer do klienta, który przysłał błędny komunikat w odpowiedzi na komunikat TRICK. Wysyłany również wtedy, gdy klient próbuje położyć kartę na stole nieproszony o to. Zawartość błędnego komunikatu jest ignorowana przez serwer.

    TAKEN<numer lewy><lista kart><miejsce przy stole klienta biorącego lewę>\r\n
    Komunikat wysyłany przez serwer do klientów. Informuje, który z klientów wziął lewę. Lista kart zawiera cztery karty składające się na lewę w kolejności, w jakiej zostały położone na stole.

    SCORE<miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów>\r\n
    Komunikat wysyłany przez serwer do klientów po zakończeniu rozdania. Informuje o punktacji w tym rozdaniu.

    TOTAL<miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów><miejsce przy stole klienta><liczba punktów>\r\n
    Komunikat wysyłany przez serwer do klientów po zakończeniu rozdania. Informuje o łącznej punktacji w rozgrywce.
 */

class Msg {
public:
    [[nodiscard]] virtual std::string toString() const = 0;
     explicit operator std::string() const {
        return toString();
    }
};

class IAm : public Msg {
public:
    Seat seat;
    explicit IAm(Seat seat) : seat(seat) {}
    [[nodiscard]] std::string toString() const override {
        return "IAM" + ::toString(seat) + "\r\n";
    }
};

class Busy : public Msg {
    std::vector<Seat> busy_seats;
public:
    explicit Busy(std::vector<Seat> busy_seats) : busy_seats(std::move(busy_seats)) {}
    [[nodiscard]] std::string toString() const override {
        std::string result = "BUSY";
        for (const auto& seat: busy_seats) {
            result += ::toString(seat);
        }
        result += "\r\n";
        return result;
    }
};

class Deal : public Msg {
    DealType dealType;
    Seat firstSeat;
    std::vector<Card> cards;
public:
    Deal(DealType dealType, Seat firstSeat, std::vector<Card> cards) : dealType(dealType), firstSeat(firstSeat), cards(std::move(cards)) {}
    [[nodiscard]] std::string toString() const override {
        std::string result = "DEAL" + std::to_string(static_cast<int>(dealType)) + ::toString(firstSeat);
        for (const auto& card: cards) {
            result += card.toString();
        }
        result += "\r\n";
        return result;
    }
};

class Trick : public Msg {
public:
    std::vector<Card> cards;
    int trickNumber; // 1-13
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
        result += ::toString(takerSeat) + "\r\n";
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
            result += ::toString(seat) + std::to_string(score);
        }
        result += "\r\n";
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
            result += ::toString(seat) + std::to_string(score);
        }
        result += "\r\n";
        return result;
    }
};

class Parser {
public:
    static std::shared_ptr<Msg> parse(std::string message) {
        std::smatch match;

        try {
            std::regex IAM_regex(R"(^IAM([NESW])\r\n$)");
            std::regex BUSY_regex(R"(^BUSY([NESW]+)\r\n$)");
            std::regex DEAL_regex(R"(^DEAL([1-7])([NESW])(((10|[23456789JQKA])[CDHS]){13})\r\n$)");
            std::regex TRICK_regex(R"(^TRICK([1-9]|1[0-3])(((10|[23456789JQKA])[CDHS]){0,3})\r\n$)");
            std::regex WRONG_regex(R"(^WRONG([1-9]|1[0-3])\r\n$)");
            std::regex TAKEN_regex(R"(^TAKEN([1-9]|1[0-3])((10|[23456789JQKA])[CDHS]){4}([NESW])\r\n$)");
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
                std::vector<Card> cards;
                std::string cardsStr = match[3].str();
                std::regex card_regex(R"(((10|[23456789JQKA])([CDHS])))");
                std::sregex_iterator it(cardsStr.begin(), cardsStr.end(), card_regex);
                std::sregex_iterator end;
                while (it != end) {
                    std::smatch card_match = *it;
                    std::string cardStr = card_match.str();
                    cards.push_back(Card(cardStr));
                    ++it;
                }
                // ensure that there are no repeated cards
                std::set<Card> cardsSet(cards.begin(), cards.end());
                if (cards.size() != cardsSet.size()) {
                    Reporter::error("Repeated cards in DEAL message");
                    return nullptr;
                }
                return std::make_shared<Deal>(dealType, firstSeat, cards);
            } else if (std::regex_match(message, match, TRICK_regex)) {
                int trickNumber = std::stoi(match[1].str());
                std::vector<Card> cards;
                std::string cardsStr = match[2].str();
                std::regex card_regex(R"(((10|[23456789JQKA])([CDHS])))");
                std::sregex_iterator it(cardsStr.begin(), cardsStr.end(), card_regex);
                std::sregex_iterator end;
                while (it != end) {
                    std::smatch card_match = *it;
                    std::string cardStr = card_match.str();
                    cards.push_back(Card(cardStr));
                    ++it;
                }
                return std::make_shared<Trick>(trickNumber, cards);
            } else if (std::regex_match(message, match, WRONG_regex)) {
                int trickNumber = std::stoi(match[1].str());
                return std::make_shared<Wrong>(trickNumber);
            } else if (std::regex_match(message, match, TAKEN_regex)) {
                int trickNumber = std::stoi(match[1].str());
                std::vector<Card> cards;
                std::string cardsStr = match[2].str();
                std::regex card_regex(R"(((10|[23456789JQKA])([CDHS])))");
                std::sregex_iterator it(cardsStr.begin(), cardsStr.end(), card_regex);
                std::sregex_iterator end;
                while (it != end) {
                    std::smatch card_match = *it;
                    std::string cardStr = card_match.str();
                    cards.push_back(Card(cardStr));
                    ++it;
                }
                Seat takerSeat = Seat(match[4].str()[0]);
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

template<class T>
void ensureTypeAndStringValue(const std::shared_ptr<Msg>& msg, const std::string& expectedStr) {
    auto castedMsg = std::dynamic_pointer_cast<T>(msg);
    if (castedMsg == nullptr) {
        Reporter::error("Invalid message type");
        assert(false);
    }
    std::cerr << "Expected: " << expectedStr << "     Got: " << castedMsg->toString() << std::endl;
    if (castedMsg->toString() != expectedStr) {
        Reporter::error("Invalid message value");
        throw std::invalid_argument("Invalid message value");
    }
}

template<class T>
void ensureType(const std::shared_ptr<Msg>& msg) {
    auto castedMsg = std::dynamic_pointer_cast<T>(msg);
    if (castedMsg == nullptr) {
        Reporter::error("Invalid message type");
        assert(false);
    }
}

void testParser() {
    std::string message = "IAMN\r\n";
    ensureTypeAndStringValue<IAm>(Parser::parse(message), message);

    message = "BUSYNS\r\n";
    ensureTypeAndStringValue<Busy>(Parser::parse(message), message);

    // busy with multiple repeated seats
    message = "BUSYNNSS\r\n";
    assert(Parser::parse(message) == nullptr);

    // deal with 13 cards
    message = "DEAL3E" "2C3C4C5C6C7C8C9C10C" "JC" "10H" "KS" "KD" "\r\n";
    ensureTypeAndStringValue<Deal>(Parser::parse(message), message);

    // deal with too few cards
    message = "DEAL3E" "2C3C4C5C6C7C8C9C10C" "JC" "10H" "\r\n";
    assert(Parser::parse(message) == nullptr);

    // deal with 13 cards, but with repetitions
    message = "DEAL3E" "2C3C4C5C6C7C8C9C10C" "KD" "AS" "KD" "KS" "\r\n";
    assert(Parser::parse(message) == nullptr);

    // trick with 3 cards
    message = "TRICK1" "2C3C4C\r\n";
    ensureTypeAndStringValue<Trick>(Parser::parse(message), message);

    // trick with 1 card
    message = "TRICK1" "10C\r\n";
    ensureTypeAndStringValue<Trick>(Parser::parse(message), message);

    // trick with 0 cards
    message = "TRICK1\r\n";
    ensureTypeAndStringValue<Trick>(Parser::parse(message), message);

    // trick with too many cards
    message = "TRICK1" "2C3C4C5C6C\r\n";
    assert(Parser::parse(message) == nullptr);

    // trick with non-existent card value
    message = "TRICK1" "2C3C4C5C6X\r\n";
    assert(Parser::parse(message) == nullptr);

    // trick with non-existing trick number
    message = "TRICK0" "2C3C4C5C6C\r\n";
    assert(Parser::parse(message) == nullptr);

    // wrong with existing trick number
    message = "WRONG1\r\n";
    ensureType<Wrong>(Parser::parse(message));

    // score with correct values
    message = "SCOREN0E0S0W0\r\n";
    ensureType<Score>(Parser::parse(message));

    // score with values
    message = "SCOREN0E0S0W1\r\n";
    ensureType<Score>(Parser::parse(message));

    // total with correct values
    message = "TOTALN0E42S0W999\r\n";
    ensureType<Total>(Parser::parse(message));

    // IAM with wrong seat
    message = "IAMX\r\n";
    assert(Parser::parse(message) == nullptr);

    // BUSY with wrong seat
    message = "BUSYX\r\n";
    assert(Parser::parse(message) == nullptr);

    // TRICK with wrong card
    message = "TRICK3" "10X" "\r\n";
    assert(Parser::parse(message) == nullptr);
}

struct DealConfig {
    DealType dealType;
    Seat firstSeat;
    std::unordered_map<Seat, std::vector<Card>> cards;
};

// Function that returns the string with ip and port of the other side of the given socket (works for both IPv4 and IPv6)
std::string getSocketIPAndPort(int socket_fd) {
    struct sockaddr_storage address{};
    socklen_t address_length = sizeof(struct sockaddr_storage);

    // Get remote address
    if (getpeername(socket_fd, (struct sockaddr*)&address, &address_length) == -1) {
        return "<ip-port-unknown>";
    }
    char ipstr[INET6_ADDRSTRLEN];
    inet_ntop(address.ss_family, &address, ipstr, sizeof(ipstr));
    int port = ntohs(((struct sockaddr_in*)&address)->sin_port);
    return std::string(ipstr) + ":" + std::to_string(port);
}


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
            Reporter::debug(Color::Red, "What the hell - POLLHUP after POLLERR detected.");
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

public:
     explicit PollBuffer(struct pollfd *pollfd = nullptr, std::string msg_separator = "\r\n") {
        buffer_in_msg_separator = std::move(msg_separator);
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
            Reporter::error("Tried to update a disconnected client.");
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

        std::string localIP, remoteIP; int localPort, remotePort;
        getSocketAddresses(pollfd->fd, localIP, localPort, remoteIP, remotePort);
        Reporter::report(remoteIP, remotePort, localIP, localPort, getCurrentTime(), message);

        return message;
    }

    bool isWriting() {
        return !buffer_out.empty();
    }

    void writeMessage(const std::string& message) {
        assert(!message.empty());
        buffer_out += message;
        pollfd->events |= POLLOUT; // add the POLLOUT flag

        std::string localIP, remoteIP; int localPort, remotePort;
        getSocketAddresses(pollfd->fd, localIP, localPort, remoteIP, remotePort);
        Reporter::report(localIP, localPort, remoteIP, remotePort, getCurrentTime(), message);
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


class ServerConfig {
private:
public:
    std::optional<int> port;
    std::vector<DealConfig> deals;
    int timeout_seconds = 5;//todo getter

    static ServerConfig FromArgs(int argc, char** argv) {
        ServerConfig config;
        throw std::runtime_error("Not implemented");
        return config;
    }

};


struct PlayerStats {
    int points_deal = 0;
    int points_total = 0;
    std::set<Card> hand;
    std::set<Card> cards_taken;

    bool hasCard(const Card& card) {
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
        cards_taken.insert(cards.begin(), cards.end());
        points_deal += points;
        points_total += points;
    }

    void takeNewDeal(const std::vector<Card>& newHand) {
        hand.clear();
        hand.insert(newHand.begin(), newHand.end());
        points_deal = 0;
    }
};

/*
 * Zasady gry
W kierki gra czterech graczy standardową 52-kartową talią. Gracze siedzą przy stole na miejscach N (ang. north), E (ang. east), S (ang south), W (ang. west). Rozgrywka składa się z rozdań. W każdym rozdaniu każdy z graczy otrzymuje na początku po 13 kart. Gracz zna tylko swoje karty. Gra składa się z 13 lew. W pierwszej lewie wybrany gracz wychodzi, czyli rozpoczyna rozdanie, kładąc wybraną swoją kartę na stół. Po czym pozostali gracze w kolejności ruchu wskazówek zegara dokładają po jednej swojej karcie. Istnieje obowiązek dokładania kart do koloru. Jeśli gracz nie ma karty w wymaganym kolorze, może położyć kartę w dowolnym innym kolorze. Nie ma obowiązku przebijania kartą starszą. Gracz, który wyłożył najstarszą kartę w kolorze karty położonej przez gracza wychodzącego, bierze lewę i wychodzi jako pierwszy w następnej lewie. Obowiązuje standardowe starszeństwo kart (od najsłabszej): 2, 3, 4, …, 9, 10, walet, dama, król, as.

W grze chodzi o to, żeby brać jak najmniej kart. Za branie kart otrzymuje się punkty. Wygrywa gracz, który w całej rozgrywce zbierze najmniej punktów. Jest siedem typów rozdań:

 Deal type:
1. nie brać lew, za każdą wziętą lewę (nie kartę, lewę!!!) dostaje się 1 punkt;
2. nie brać kierów, za każdego wziętego kiera dostaje się 1 punkt;
3. nie brać dam, za każdą wziętą damę dostaje się 5 punktów;
4. nie brać panów (waletów i króli), za każdego wziętego pana dostaje się 2 punkty;
5. nie brać króla kier, za jego wzięcie dostaje się 18 punktów;
6. nie brać siódmej i ostatniej lewy, za wzięcie każdej z tych lew dostaje się po 10 punktów;
7. rozbójnik, punkty dostaje się za wszystko wymienione powyżej.
 */
int countPoints(const std::vector<Card>& cards, DealType dealType, int trickNumber) {
    int points = 0;
    for (const auto& card: cards) {
        switch (dealType) {
            case DealType::NoHearts:
                if (card.suit == CardSuit::Hearts)
                    points += 1;
                break;
            case DealType::NoQueens:
                if (card.value == CardValue::Queen)
                    points += 5;
                break;
            case DealType::NoKingsJacks:
                if (card.value == CardValue::King || card.value == CardValue::Jack)
                    points += 2;
                break;
            case DealType::NoKingOfHearts:
                if (card.value == CardValue::King && card.suit == CardSuit::Hearts)
                    points += 18;
                break;
            case DealType::No7AndLastTrick:
                if (trickNumber == Trick::LastTrickNumber || trickNumber == 7)
                    points += 10;
                break;
            case DealType::Robber:
                if (card.suit == CardSuit::Hearts)
                    points += 1;
                if (card.value == CardValue::Queen)
                    points += 5;
                if (card.value == CardValue::King || card.value == CardValue::Jack)
                    points += 2;
                if (card.value == CardValue::King && card.suit == CardSuit::Hearts)
                    points += 18;
                if (trickNumber == Trick::LastTrickNumber || trickNumber == 7)
                    points += 10;
                break;
            default:
                break; // we'll handle Robber and NoTricks separately
        }
    }

    if (dealType == DealType::NoTricks || dealType == DealType::Robber)
        points += 1;

    return points;
}


class Server {
private:
    ServerConfig config;

    struct Polling {
        static constexpr int Connections = 32;
        // initialize the pollfd array with values {.fd = -1, .events = 0, .revents = 0}
        std::array<struct pollfd, Connections> fds{};
        const int fdAcceptIdx = 0;
        Polling() {
            for (auto& fd: fds) {
                fd.fd = -1;
                fd.events = 0;
                fd.revents = 0;
            }
        }
        struct Candidate {
            PollBuffer buffer;
            enum State {
                WaitingForIAM,
                Rejecting,
            } state;
            time_t connectionTime;

            explicit Candidate(PollBuffer buffer, State state = State::WaitingForIAM)
                    : buffer(std::move(buffer)), state(state), connectionTime(time(nullptr)) {}

            explicit Candidate(struct pollfd* pollfd): Candidate(PollBuffer(pollfd)){}
        };

        std::vector<Candidate> candidates{}; // in/out buffer wrappers for candidate players (without a seat yet)

        void startAccepting(int port) {
            fds[fdAcceptIdx].fd = socket(AF_INET6, SOCK_STREAM, 0);
            if (fds[fdAcceptIdx].fd < 0) {
                syserr("cannot create a socket");
            }

            // enable address and port reuse
            int optval = 1;
            if (setsockopt(fds[fdAcceptIdx].fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &optval, sizeof optval) < 0) {
                syserr("setsockopt SO_REUSEADDR");
            }

            struct sockaddr_in6 server_address{};
            server_address.sin6_family = AF_INET6; // IPv6
            server_address.sin6_addr = in6addr_any; // Listening on all interfaces.
            server_address.sin6_port = htons(port);
            if (bind(fds[fdAcceptIdx].fd, (struct sockaddr *) &server_address, (socklen_t) sizeof server_address) < 0) {
                syserr("bind");
            }

            const int QueueLength = 4;
            if (listen(fds[fdAcceptIdx].fd, QueueLength) < 0) {
                syserr("listen");
            }

            auto length = (socklen_t) sizeof server_address;
            if (getsockname(fds[fdAcceptIdx].fd, (struct sockaddr *) &server_address, &length) < 0) {
                syserr("getsockname");
            }
            Reporter::debug(Color::Green, "Server is listening on port " + std::to_string(ntohs(server_address.sin6_port)));

            fds[fdAcceptIdx].events = POLLIN;
            fds[fdAcceptIdx].revents = 0;
        }

        void pauseAccepting() {
            fds[fdAcceptIdx].events = 0;
        }

        void resumeAccepting() {
            fds[fdAcceptIdx].events = POLLIN;
        }

        void stopAccepting() {
            close(fds[fdAcceptIdx].fd);
            fds[fdAcceptIdx].fd = -1;
        }
    } poll;

    std::vector<Seat> getTakenSeats() {
        std::vector<Seat> takenSeats;
        for (const auto &[seat, player]: players) {
            if (player.buffer.isConnected())
                takenSeats.push_back(seat);
        }
        return takenSeats;
    }

    struct Player {
        PollBuffer buffer;
        time_t trickRequestTime{};
        Seat seat{};
        PlayerStats stats;

        Player(Seat seat, PollBuffer buffer) : buffer(std::move(buffer)), seat(seat) {}

        [[nodiscard]] bool isConnected() const {
            return buffer.isConnected();
        }

        void connect(PollBuffer new_buffer) {
            this->buffer = std::move(new_buffer);
        }

        void disconnect() {
            buffer.disconnect();
        }
    };

    std::unordered_map<Seat, Player> players{
            {Seat::N, Player(Seat::N, PollBuffer())},
            {Seat::E, Player(Seat::E, PollBuffer())},
            {Seat::S, Player(Seat::S, PollBuffer())},
            {Seat::W, Player(Seat::W, PollBuffer())}
    }; // in/out buffer wrappers for players (with a seat)


private:
    void _pollUpdate() {
        // ----------- reset revents, run poll and update the player poll players ------------
        for (auto &fd: poll.fds) {
            fd.revents = 0;
        }
        ::poll(poll.fds.data(), poll.fds.size(), config.timeout_seconds * 1000 / 2.5); // todo adjust granularity
        for (auto &[seat, player]: players) {
            if (player.buffer.isConnected())
                player.buffer.update();
        }
        for (auto &candidate: poll.candidates) {
            candidate.buffer.update();
        }
    }

    void _updateDisconnections() {
        // (1) update disconnections and remove disconnected players or candidates
        for (auto &[seat, player]: players) {
            if (player.isConnected()) {
                if (player.buffer.hasError()) {
                    // disconnect the player
                    player.buffer.disconnect();
                    Reporter::debug(Color::Red, "Player " + ::toString(seat) + " disconnected.");
                }
            }
        }

        for (auto candidate = poll.candidates.begin(); candidate != poll.candidates.end(); /*candidate++*/) {
            if (candidate->buffer.hasError()) {
                // disconnect the candidate
                candidate->buffer.disconnect();
                Reporter::debug(Color::Red, "Candidate disconnected due to error.");
                // remove the candidate
                candidate = poll.candidates.erase(candidate); // iteration still valid: https://en.cppreference.com/w/cpp/container/unordered_map/erase
            } else {
                candidate++;
            }
        }
    }

    void _updateNewConnections() {
        if (poll.fds[poll.fdAcceptIdx].revents & POLLIN) {
            struct sockaddr_in client_address{};
            socklen_t client_address_len = sizeof(client_address);
            int client_fd = accept(poll.fds[poll.fdAcceptIdx].fd, (struct sockaddr *) &client_address,
                                   &client_address_len);
            if (client_fd < 0) {
                syserr("accept");
            }

            // Set to nonblocking mode.
            if (fcntl(client_fd, F_SETFL, O_NONBLOCK)) {
                syserr("fcntl");
            }

            // find the first free pollfd
            for (auto& pollfd: poll.fds) {
                if (pollfd.fd == -1) {
                    pollfd.fd = client_fd;
                    pollfd.events = POLLIN;

                    auto new_candidate = Polling::Candidate(&pollfd);
                    poll.candidates.push_back(new_candidate);

                    Reporter::log("New candidate connected.");
                    return;
                }
            }
            // or close the connection
            Reporter::error("Is this a DDoS attack? No free pollfd for candidate.");
            close(client_fd);
        }
    }

    void acceptCandidateAsPlayer(Polling::Candidate& candidate, Seat seat) {
        assert(!players.at(seat).isConnected());
        auto& new_player = players.at(seat);
        new_player.connect(std::move(candidate.buffer));

        // Send the whole deal history to the new player.
        new_player.buffer.writeMessage(Deal(game.currentDeal->dealType, game.currentDeal->firstSeat, game.currentDeal->cards[seat]));
        for (auto& taken: game.takenHistory) {
            new_player.buffer.writeMessage(taken);
        }

        Reporter::debug(Color::Green, "Player " + ::toString(seat) + " connected and updated with history of (" + std::to_string(game.takenHistory.size()) + ") taken cards.");
        assert(players.at(seat).isConnected());
    }

    bool _processCandidateWaitingForIAM(Polling::Candidate& candidate) {
        assert(candidate.state == Polling::Candidate::State::WaitingForIAM);

        // Check timeout.
        if (time(nullptr) - candidate.connectionTime > config.timeout_seconds) {
            candidate.buffer.disconnect();
            Reporter::debug(Color::Red, "Candidate disconnected due to timeout.");
            return true;
        }

        // Check if the candidate has a message.
        if (!candidate.buffer.hasMessage()) {
            return false; // nothing to do yet
        }

        // Syntax check: IAM message.
        std::string raw_msg = candidate.buffer.readMessage();
        auto msg = Parser::parse(raw_msg);
        auto iam = std::dynamic_pointer_cast<IAm>(msg);
        if (iam == nullptr) {
            candidate.buffer.disconnect();
            Reporter::debug(Color::Red, "Candidate disconnected due to incorrect message (expected IAM, got " + raw_msg + ").");
            return true;
        }

        // Semantic check: seat is not taken.
        if (players.at(iam->seat).isConnected()) {
            candidate.buffer.writeMessage(Busy(getTakenSeats()));
            candidate.state = Polling::Candidate::State::Rejecting;
            return false; // we cannot remove the candidate yet, but we changed its state
        }

        // Accept the candidate.
        acceptCandidateAsPlayer(candidate, iam->seat);
        return true; // very important: remove the candidate to prevent double processing
    }

    // Updates the candidate messages and
    // - returns true iff the candidate should be removed (for example due to timeout, wrong message, disconnection etc.)
    bool _processCandidate(Polling::Candidate& candidate, int timeout_seconds) {
        assert(candidate.buffer.hasError() == false);
        assert(candidate.buffer.isConnected() == true);

        if (candidate.state == Polling::Candidate::State::WaitingForIAM) {
            return _processCandidateWaitingForIAM(candidate);
        }
        else if (candidate.state == Polling::Candidate::State::Rejecting) {
            // only if it has finished writing the rejection message
            if (!candidate.buffer.isWriting()) {
                candidate.buffer.disconnect();
                Reporter::debug(Color::Red, "Candidate successfully rejected and disconnected.");
                return true;
            }
        }
        else {
            Reporter::error("Invalid candidate state.");
        }
        return false;
    }

    void _updateCandidateMessages() {
        for (auto candidate = poll.candidates.begin(); candidate != poll.candidates.end(); /*candidate++*/) {
            if (_processCandidate(*candidate, config.timeout_seconds)) {
                // remove the candidate
                candidate = poll.candidates.erase(candidate);
            } else {
                ++candidate;
            }
        }
    }

    void safePoll() {
        while (true) {
            // ----------- reset revents, run poll and update the player poll players ------------
            _pollUpdate();

            // (1) update disconnections and remove disconnected players
            _updateDisconnections();

            // (2) check if there are any new connections
            _updateNewConnections();

            // (3) check if there are any new IAM messages from candidates
            _updateCandidateMessages();

            // return if all players are connected
            if (std::all_of(players.begin(), players.end(), [](const auto &p) { return p.second.isConnected(); })) {
                Reporter::debug(Color::Green, "Safe poll finished. All players connected!");
                return;
            }
            Reporter::debug(Color::Yellow, "Waiting for all players to connect...");
        }
    }


    struct GameData {
        std::vector<DealConfig>::iterator currentDeal;
        std::vector<Taken> takenHistory;
        std::vector<Card> cardsOnTable;

        int trickNumber = Trick::FirstTrickNumber; // 1-13
        Player* currentPlayer{};

        Seat trickWinnerSeat{};

        // Assume: trickNumber is set for the current trick.
        Seat getStartingSeat() const {
            if (trickNumber == Trick::FirstTrickNumber) {
                return currentDeal->firstSeat;
            }
            return trickWinnerSeat;
        }
    } game;


    // ------------------------------------------- State machine -------------------------------------------

    // ===================================================================================================
    // variable pointing to function that handles current state (use wrappers not raw function pointers)
    std::function<void()> state = [] { throw std::runtime_error("State not set."); };
    // whether the state should update poll before calling the state function
    bool stateShouldPoll = true;

    // Function to change the current state.
    void ChangeState(std::function<void()> newState, bool shouldPoll = true) {
        state = std::move(newState);
        stateShouldPoll = shouldPoll;
    }
    // ===================================================================================================

    void _checkOtherPlayersMessages() {
        for (auto& [seat, player]: players) {
            if (player.buffer.hasMessage() && seat != game.currentPlayer->seat) {
                auto msg = Parser::parse(player.buffer.readMessage());
                if (auto trick = std::dynamic_pointer_cast<Trick>(msg)) {
                    Reporter::logWarning("Player " + ::toString(seat) + " sent a TRICK message, but it's not his turn.");
                    player.buffer.writeMessage(Wrong(game.trickNumber));
                } else {
                    Reporter::logError("Player " + ::toString(seat) + ": unexpected message received. Closing connection.");
                    player.disconnect();
                }
            }
        }
    }

    Player* _whoTakesTrick() {
        assert(game.cardsOnTable.size() == 4);
        auto firstCardSuit = game.cardsOnTable[0].suit;
        auto winningCard = game.cardsOnTable[0];
        Player* winnerPlayer = &players.at(game.getStartingSeat());

        for (auto [i, player] = std::tuple(0, winnerPlayer); i < std::ssize(game.cardsOnTable); i++, player = &players.at(nextSeat(player->seat))) {
            if (game.cardsOnTable[i].suit == firstCardSuit && winningCard < game.cardsOnTable[i]) {
                winningCard = game.cardsOnTable[i];
                winnerPlayer = player;
            }
        }

        return winnerPlayer;
    }

    void _sendScoresAndTotals() {
        // Send the score and total messages to all players (it's done at the end of each deal)
        Score score(std::unordered_map<Seat, int>{
            {Seat::N, players.at(Seat::N).stats.points_deal},
            {Seat::E, players.at(Seat::E).stats.points_deal},
            {Seat::S, players.at(Seat::S).stats.points_deal},
            {Seat::W, players.at(Seat::W).stats.points_deal}
        });
        for (auto& [seat, player]: players) {
            player.buffer.writeMessage(score);
        }

        Total total(std::unordered_map<Seat, int>{
            {Seat::N, players.at(Seat::N).stats.points_total},
            {Seat::E, players.at(Seat::E).stats.points_total},
            {Seat::S, players.at(Seat::S).stats.points_total},
            {Seat::W, players.at(Seat::W).stats.points_total}
        });
        for (auto& [seat, player]: players) {
            player.buffer.writeMessage(total);
        }
    }

    void _finalizeDeal() {
        // Send the score and total messages to all players (it's done at the end of each deal)
        _sendScoresAndTotals();

        // If the deal is not over yet, continue with the next deal and set the state to stateStartTrick
        if (game.currentDeal != config.deals.end() - 1) {
            setCurrentDeal(game.currentDeal + 1);
            sendDealInfo(); // we assume that the players are 'atomically' still connected since the last safePoll
            ChangeState([this] { stateStartTrick(Trick::FirstTrickNumber); });
            return;
        }

        // *** The game is over! ***
        poll.stopAccepting();
        Reporter::log("Game is over. Disconnecting all players.");
        for (auto& [seat, player]: players) {
            player.buffer.flush(); // very important! this can block, but it's the last message anyway
            player.disconnect();
            Reporter::log("Player " + ::toString(seat) + " disconnected.");
        }

        Reporter::log("Exiting the server... o7");
        exit(0);
    }
    // TRICK -> N   | safePoll | wait (no msg) | safePoll | wait (no msg) | safePoll (N disconnected, N connected) | wait (no msg) - timeout - RESEND TRICK -> N |
    bool isDealResultDetermined() {
        return game.trickNumber == Trick::LastTrickNumber;
    }

    void _handleCorrectTrick(std::shared_ptr<Trick> trick) {
        // Update the cards on the table and in the player's hand
        game.cardsOnTable.push_back(trick->cards[0]);
        game.currentPlayer->stats.removeCard(trick->cards[0]);

        // If the current player is NOT the last one in the trick (4th player)...
        if (game.cardsOnTable.size() < 4) {
            game.currentPlayer = &players.at(nextSeat(game.currentPlayer->seat));
            ChangeState([this] { stateSendTrick(); });
            return;
        }

        // *** The trick is complete! ***

        // Find the 'winner' of the trick (the player with the highest card of the first card's suit)
        auto winner = _whoTakesTrick();
        game.trickWinnerSeat = winner->seat;

        // Update the stats of the players (actually just the winner, the rest is unchanged)
        int points = countPoints(game.cardsOnTable, game.currentDeal->dealType, game.trickNumber);
        winner->stats.takeTrick(game.cardsOnTable, points);

        // Send the taken message to all players (including the winner) and update the history of taken cards
        Taken taken(game.trickNumber, game.cardsOnTable, winner->seat);
        for (auto& [seat, player]: players) {
            player.buffer.writeMessage(taken);
        }
        game.takenHistory.push_back(taken);


        // If the deal is not over yet, continue with the next trick
        if (!isDealResultDetermined()) {
            game.trickNumber++; assert(game.trickNumber <= Trick::LastTrickNumber);
            ChangeState([this] { stateStartTrick(game.trickNumber); });
            return;
        }

        // *** The deal is over! ***
        _finalizeDeal();
    }

    void _handleMessageFromCurrentPlayer() {
        auto raw_msg = game.currentPlayer->buffer.readMessage();
        auto msg = Parser::parse(raw_msg);
        auto trick = std::dynamic_pointer_cast<Trick>(msg);

        // Syntax check: TRICK message
        if (trick == nullptr) {
            Reporter::logError("Player " + ::toString(game.currentPlayer->seat) + ": unexpected message received. Closing connection.");
            game.currentPlayer->disconnect();
            return; // and keep the WaitForTrick state
        }

        // Semantic check: trick number is correct
        if (trick->trickNumber != game.trickNumber) {
            Reporter::logWarning("Player " + ::toString(game.currentPlayer->seat) + " sent a TRICK message with incorrect trick number.");
            game.currentPlayer->buffer.writeMessage(Wrong(game.trickNumber));
            return; // and keep the WaitForTrick state
        }

        // Semantic check: trick has exactly 1 card
        if (trick->cards.size() != 1) {
            Reporter::logWarning("Player " + ::toString(game.currentPlayer->seat) + " sent a TRICK message with " + std::to_string(trick->cards.size()) + " cards.");
            game.currentPlayer->buffer.writeMessage(Wrong(game.trickNumber));
            return; // and keep the WaitForTrick state
        }

        // Semantic check: player has the card in his hand
        if (!game.currentPlayer->stats.hasCard(trick->cards[0])) {
            Reporter::logWarning("Player " + ::toString(game.currentPlayer->seat) + " sent a TRICK message with a card he doesn't have.");
            game.currentPlayer->buffer.writeMessage(Wrong(game.trickNumber));
            return; // and keep the WaitForTrick state
        }

        // Semantic check: if the card is not the first card in the trick AND the player put a card of a different suit than the first card,
        // check if the player has any cards of the first card's suit
        if (!game.cardsOnTable.empty() && trick->cards[0].suit != game.cardsOnTable[0].suit) {
            if (game.currentPlayer->stats.hasSuit(game.cardsOnTable[0].suit)) {
                Reporter::logWarning("Player " + ::toString(game.currentPlayer->seat) + " sent a TRICK message with a card of a different suit than the first card (but HAD a card of the first card's suit).");
                game.currentPlayer->buffer.writeMessage(Wrong(game.trickNumber));
                return; // and keep the WaitForTrick state
            }
        }

        // *** The trick is correct! ***
        _handleCorrectTrick(trick);
    }

    void stateWaitForTrick() {
        // Poll is already called and has some revents (possibly only timeout)
        // Assumption: all players are connected!
        for (auto& [seat, player]: players) {
            assert(player.isConnected());
            assert(player.buffer.hasError() == false);
        }

        // --------------------------------------------- Main logic ----------------------------------------------
        // 0) Check *other* players' messages (not the current player):
        //    - if there's a TRICK message, send WRONG to this player (because it's not his turn)
        //    - if there's any other message or an invalid message, disconnect the player.
        //
        // 1) check if there was a message from the current player:
        //    - if it's any other message, disconnect the player
        //    - else if it's a TRICK message, run semantic checks and update the game state:
        //        - if the trick is incorrect in the current context (e.g. wrong trick number), send WRONG to the player
        //            - check if the trick number is correct
        //            - check if the trick has exactly 1 card
        //            - check if the player has the card in his hand (store the taken cards separately, they cannot be played again)
        //            - if the card is not the first card in the trick AND the player put a card of a different suit than the first card,
        //                - check if the player has any cards of the first card's suit (if so, send WRONG and of course print the warning)
        //        - if the trick is correct, update the game state and change the state to stateSendTrick
        //            - update the cards on the table
        //            - if the player is NOT the last one in the trick (4th player),
        //                - change the current player to the next one and change the state to stateSendTrick()
        //            - if the player is the last one in the trick (4th player), do the following:
        //                - find the 'winner' of the trick (the player with the highest card of the first card's suit)
        //                - update the trickWinnerSeat
        //                - update the stats (scores etc.) according to the type of the current deal (e.g. No7AndLastTrick)
        //                - send the taken message to all players (including the winner) and update the history of taken cards
        //                - if deal has NOT finished (meaning that the trick number is Trick::LastTrickNumber OR [todo] all penalties have been taken)
        //                    - change the state to stateStartTrick(trick number + 1)
        //                - else
        //                    -> call _finishDeal() (it will send the scores and totals and change the deal to the next one or finish the game)
        //
        // 2) or else, if there was a timeout for the *current* player (only the current player can timeout):
        //       - resend the trick message to the current player and change the state to stateWaitForTrick() with repolling

        // 0) Check *other* players' messages (not the current player):
        _checkOtherPlayersMessages();

        // 1) check if there was a message from the current player:
        if (game.currentPlayer->buffer.hasMessage()) {
            _handleMessageFromCurrentPlayer();
        }
        // 2) or else, if there was a timeout for the *current* player (only the current player can timeout):
        else if (time(nullptr) - game.currentPlayer->trickRequestTime > config.timeout_seconds) {
            Reporter::logWarning("Player " + ::toString(game.currentPlayer->seat) + " did not respond in time. ");
            ChangeState([this] { stateSendTrick(); }, true);
        }

    }

    void stateSendTrick() {
        game.currentPlayer->buffer.writeMessage(Trick(game.trickNumber, game.cardsOnTable));
        game.currentPlayer->trickRequestTime = time(nullptr);

        ChangeState([this] { stateWaitForTrick(); });
    }

    void stateStartTrick(int trickNumber) {
        assert(Trick::FirstTrickNumber <= trickNumber && trickNumber <= Trick::LastTrickNumber);

        game.trickNumber = trickNumber;
        game.currentPlayer = &players.at(game.getStartingSeat());
        game.cardsOnTable.clear();

        ChangeState([this] { stateSendTrick(); }, false);
    }

    void setCurrentDeal(const std::vector<DealConfig>::iterator& dealIt) {
        game.currentDeal = dealIt;
        game.takenHistory.clear();
        for (auto& [seat, player]: players) {
            player.stats.takeNewDeal(game.currentDeal->cards[seat]);
        }
    }

    void sendDealInfo() {
        for (auto& [seat, player]: players) {
            player.buffer.writeMessage(Deal(game.currentDeal->dealType, game.currentDeal->firstSeat, game.currentDeal->cards[seat]));
        }
    }

    // -------------------------------------------------------------------------------------------------

public:
    explicit Server(ServerConfig _config): config(std::move(_config)) {
        state = [this] { std::runtime_error("Server state is not set.");};
    }

    [[noreturn]] void run() {
        poll.startAccepting(config.port.value_or(0));

        // after successful poll, start the first trick in the first deal:
        setCurrentDeal(config.deals.begin());
        ChangeState([this] { stateStartTrick(Trick::FirstTrickNumber); });

        while (true) {
            // after calling this function, fds are updated, all 4 players are present without any errors:
            if (stateShouldPoll)
                safePoll();
            stateShouldPoll = true;

            // call current state function
            state();
        }
    }
};

// #####################################################################################################################
// ################################################## Client ###########################################################
// #####################################################################################################################

//Parametry wywołania klienta
//        Parametry wywołania klienta mogą być podawane w dowolnej kolejności. Jeśli parametr został podany więcej niż raz lub podano sprzeczne parametry, to obowiązuje pierwsze lub ostatnie wystąpienie takiego parametru na liście parametrów.
//
//-h <host>
//        Określa adres IP lub nazwę hosta serwera. Parametr jest obowiązkowy.
//
//-p <port>
//        Określa numer portu, na którym nasłuchuje serwer. Parametr jest obowiązkowy.
//
//-4
//Wymusza w komunikacji z serwerem użycie IP w wersji 4. Parametr jest opcjonalny.
//
//-6
//Wymusza w komunikacji z serwerem użycie IP w wersji 6. Parametr jest opcjonalny.
//
//Jeśli nie podano ani parametru -4, ani -6, to wybór wersji protokołu IP należy scedować na wywołanie funkcji getaddrinfo, podając ai_family = AF_UNSPEC.
//
//                                                                                                                                              -N
//                                                                                                                                              -E
//                                                                                                                                              -S
//                                                                                                                                              -W
//Określa miejsce, które klient chce zająć przy stole. Parametr jest obowiązkowy.
//
//-a
//        Parametr jest opcjonalny. Jeśli jest podany, to klient jest automatycznym graczem. Jeśli nie jest podany, to klient jest pośrednikiem między serwerem a graczem-użytkownikiem.
//

struct ClientConfig {
    std::string host;
    int port{};
    enum IPAddressFamily {
        IPv4,
        IPv6,
        Unspecified
    } ipFamily = Unspecified;
    Seat seat{};
    bool isAutomatic = false;
public:
    // use getopt()
    static ClientConfig FromArgs(int argc, char** argv) {
        ClientConfig config;
        int c;
        // mandatory arguments
        bool hostSet = false;
        bool portSet = false;
        bool seatSet = false;
        while ((c = getopt(argc, argv, "h:p:46NESWa")) != -1) {
            switch (c) {
                case 'h':
                    config.host = optarg;
                    hostSet = true;
                    break;
                case 'p':
                    config.port = std::stoi(optarg);
                    portSet = true;
                    break;
                case '4':
                    config.ipFamily = IPAddressFamily::IPv4;
                    break;
                case '6':
                    config.ipFamily = IPAddressFamily::IPv6;
                    break;
                case 'N':
                    config.seat = Seat::N;
                    seatSet = true;
                    break;
                case 'E':
                    config.seat = Seat::E;
                    seatSet = true;
                    break;
                case 'S':
                    config.seat = Seat::S;
                    seatSet = true;
                    break;
                case 'W':
                    config.seat = Seat::W;
                    seatSet = true;
                    break;
                case 'a':
                    config.isAutomatic = true;
                    break;
                default:
                    Reporter::error("Invalid argument. Exiting.");
                    exit(1);
            }
        }

        if (!hostSet || !portSet || !seatSet) {
            Reporter::error("Missing mandatory arguments (" + std::string(hostSet ? "" : "host ")
                        + std::string(portSet ? "" : "port ") + std::string(seatSet ? "" : "seat ") + "). Exiting.");
            exit(1);
        }

        return config;
    }

    [[nodiscard]] decltype(AF_UNSPEC) getIPFamily() const {
        switch (ipFamily) {
            case IPv4:
                return AF_INET;
            case IPv6:
                return AF_INET6;
            case Unspecified:
                return AF_UNSPEC;
        }
    }
};

class Client {
    ClientConfig config;
    std::array<pollfd, 3> fds{
        pollfd{.fd = -1, .events = 0, .revents = 0},
        pollfd{.fd = -1, .events = 0, .revents = 0},
        pollfd{.fd = -1, .events = 0, .revents = 0}
    };
    const int fdServerIdx = 0;
    const int fdStdinIdx = 1;
    const int fdStdoutIdx = 2;

    // Creates and returns a socket connected to the server.
    [[nodiscard]] int server_socket() {
        // connect to the server according to config:
        int socket_fd = socket(config.getIPFamily(), SOCK_STREAM, 0);
        if (socket_fd < 0) {
            syserr("socket");
        }
        auto server_address = get_server_address(config.host.c_str(), config.port, config.getIPFamily());
        if (connect(socket_fd, (struct sockaddr *) &server_address, sizeof server_address) < 0) {
            syserr("connect");
        }

        // set to nonblocking mode
        if (fcntl(socket_fd, F_SETFL, O_NONBLOCK)) {
            syserr("fcntl");
        }

        // print the server info from server_address (convert the address to string)
        Reporter::log("Connected to server " + getSocketIPAndPort(socket_fd) + ".");

        return socket_fd;
    }

    void setup_poll_and_buffers() {
        fds[fdServerIdx].fd = server_socket();
        buffers.Server = PollBuffer(&fds[fdServerIdx]);

        fds[fdStdinIdx].fd = STDIN_FILENO;
        buffers.StdIn = PollBuffer(&fds[fdStdinIdx]);

        fds[fdStdoutIdx].fd = STDOUT_FILENO;
        buffers.StdOut = PollBuffer(&fds[fdStdoutIdx]); // sets the POLLIN but whatever
    }

    struct {
        PollBuffer Server;
        PollBuffer StdIn;
        PollBuffer StdOut;
    } buffers;

    std::function<void()> state = [] { throw std::runtime_error("State not set."); };
    void ChangeState(std::function<void()> newState) {
        state = std::move(newState);
    }

    void RePoll() {
        // reset revents, run poll and update the buffers
        for (auto &fd: fds) {
            fd.revents = 0;
        }
        ::poll(fds.data(), fds.size(), -1); // blocking

        buffers.Server.update();
        buffers.StdIn.update();
        buffers.StdOut.update();

        // validate obvious errors (do not check if server disconnected because the result depends on the state)
        if (buffers.StdIn.hasError()) {
            Reporter::error("Standard input error. Exiting.");
            exit(1);
        }
        if (buffers.StdOut.hasError()) {
            Reporter::error("Standard output error. Exiting.");
            exit(1);
        }
    }

public:
    explicit Client(ClientConfig config): config(std::move(config)) {}

    void run() {
        setup_poll_and_buffers();

        while (true) {
            // make sure there is some event and StdIn/Out is not closed
            RePoll();

            // call current state function (possibly with one buffer error - server disconnected)
            state();
        }
    }
};


// #####################################################################################################################

int main(int argc, char** argv) {
    install_sigpipe_handler();
    testParser();

//    ServerConfig config = ServerConfig::FromArgs(argc, argv);
    ServerConfig config {
            .port = 1234,
            .deals = {
                     {
                            .dealType = DealType::No7AndLastTrick,
                            .firstSeat = Seat::N,
                            .cards = {
                                    {Seat::N, {Card("2C"), Card("3C"), Card("4C"), Card("5C"), Card("6C"), Card("7C"), Card("8C"), Card("9C"), Card("10C"), Card("JC"), Card("QC"), Card("KC"), Card("AC")}},
                                    {Seat::E, {Card("2D"), Card("3D"), Card("4D"), Card("5D"), Card("6D"), Card("7D"), Card("8D"), Card("9D"), Card("10D"), Card("JD"), Card("QD"), Card("KD"), Card("AD")}},
                                    {Seat::S, {Card("2H"), Card("3H"), Card("4H"), Card("5H"), Card("6H"), Card("7H"), Card("8H"), Card("9H"), Card("10H"), Card("JH"), Card("QH"), Card("KH"), Card("AH")}},
                                    {Seat::W, {Card("2S"), Card("3S"), Card("4S"), Card("5S"), Card("6S"), Card("7S"), Card("8S"), Card("9S"), Card("10S"), Card("JS"), Card("QS"), Card("KS"), Card("AS")}}
                            }
                    }
            }
    };

    Server server(config);
    server.run();
}
