#include <stdexcept>
#include <optional>
#include <string>
#include <sys/poll.h>
#include <utility>
#include <vector>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <functional>
#include <cassert>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>


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

#include <sys/types.h>
#include <sys/socket.h>
#include <cinttypes>
#include <netdb.h>
#include <cstddef>
#include <unistd.h>
#include <csignal>

uint16_t read_port(char const *string) {
    char *endptr;
    errno = 0;
    unsigned long port = strtoul(string, &endptr, 10);
    if (errno != 0 || *endptr != 0 || port > UINT16_MAX) {
        fatal("%s is not a valid port number", string);
    }
    return (uint16_t) port;
}

struct sockaddr_in get_server_address(char const *host, uint16_t port) {
    struct addrinfo hints{};
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET; // IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo *address_result;
    int errcode = getaddrinfo(host, nullptr, &hints, &address_result);
    if (errcode != 0) {
        fatal("getaddrinfo: %s", gai_strerror(errcode));
    }

    struct sockaddr_in send_address{};
    send_address.sin_family = AF_INET;   // IPv4
    send_address.sin_addr.s_addr =       // IP address
            ((struct sockaddr_in *) (address_result->ai_addr))->sin_addr.s_addr;
    send_address.sin_port = htons(port); // port from the command line

    freeaddrinfo(address_result);

    return send_address;
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

enum class Seat {
    N,
    E,
    S,
    W
};
Seat nextSeat(Seat seat) {
    switch (seat) {
        case Seat::N: return Seat::E;
        case Seat::E: return Seat::S;
        case Seat::S: return Seat::W;
        case Seat::W: return Seat::N;
    }
}
std::string toString(Seat seat) {
    switch (seat) {
        case Seat::N: return "N";
        case Seat::E: return "E";
        case Seat::S: return "S";
        case Seat::W: return "W";
    }
}

// write a collection of color constants
struct Color {
    static constexpr const char* Red = "\033[31m";
    static constexpr const char* Green = "\033[32m";
    static constexpr const char* Yellow = "\033[33m";
    static constexpr const char* Blue = "\033[34m";
    static constexpr const char* Magenta = "\033[35m";
    static constexpr const char* Cyan = "\033[36m";
    static constexpr const char* Reset = "\033[0m";
};


enum class DealType {
    // nie brać lew, za każdą wziętą lewę dostaje się 1 punkt;
    //nie brać kierów, za każdego wziętego kiera dostaje się 1 punkt;
    //nie brać dam, za każdą wziętą damę dostaje się 5 punktów;
    //nie brać panów (waletów i króli), za każdego wziętego pana dostaje się 2 punkty;
    //nie brać króla kier, za jego wzięcie dostaje się 18 punktów;
    //nie brać siódmej i ostatniej lewy, za wzięcie każdej z tych lew dostaje się po 10 punktów;
    //rozbójnik, punkty dostaje się za wszystko wymienione powyżej.
    NoTrumps = 1,
    NoHearts = 2,
    NoQueens = 3,
    NoMen = 4,
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
    CardValue value;
    CardSuit suit;
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
};

/*
 * erwer i klient przesyłają następujące komunikaty.

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
    virtual std::string toString() = 0;
    virtual ~Msg() = default;
};

class IAm : public Msg {
    Seat seat;
public:
    explicit IAm(Seat seat) : seat(seat) {}
    std::string toString() override {
        return "IAM" + ::toString(seat) + "\r\n";
    }
};

class Busy : public Msg {
    std::vector<Seat> busy_seats;
public:
    explicit Busy(std::vector<Seat> busy_seats) : busy_seats(std::move(busy_seats)) {}
    std::string toString() override {
        std::string result = "BUSY";
        for (const auto& seat: busy_seats) {
            result += ::toString(seat);
        }
        result += "\r\n";
        return result;
    }
};

class Taken : public Msg {
    std::vector<Card> cards;
    Seat seat;
public:
    Taken(std::vector<Card> cards, Seat seat) : cards(std::move(cards)), seat(seat) {}
};

class Trick : public Msg {
    int trickNumber; // 1-13
    std::vector<Card> cardsOnTable;
public:
    static constexpr int FirstTrickNumber = 1;
    Trick(int trickNumber, std::vector<Card> cardsOnTable) : trickNumber(trickNumber), cardsOnTable(std::move(cardsOnTable)) {
        assert(trickNumber >= FirstTrickNumber && trickNumber <= 13);
    }
    std::string toString() override {
        std::string result = "TRICK" + std::to_string(trickNumber);
        for (const auto & card : cardsOnTable) {
            result += card.toString();
        }
        result += "\r\n";
        return result;
    }
};
// ...

class Parser {
public:
    static std::shared_ptr<Msg> parse(std::string message) {
        if (message.size() == 1) {
            return std::shared_ptr<IAm>(new IAm(Seat::N));
        }
        else {
            return std::make_shared<Busy>(std::vector<Seat>{Seat::N, Seat::E});
        }
    }
};

struct DealConfig {
    DealType dealType;
    Seat firstSeat;
    std::unordered_map<Seat, std::vector<Card>> cards;
};


class Reporter {
public:
    static void report(std::string message) {
        std::cout << message << std::endl;
    }
    static void debug(std::string message) {
        std::cerr << message << std::endl;
    }
    static void debug(std::string color, std::string message) {
        std::cerr << color << message << Color::Reset << std::endl;
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
                Reporter::debug(Color::Red, "Connection closed due to error.");
                error = true; return;
            }
            if (size == 0) {
                Reporter::debug(Color::Blue, "Connection closed with EOF.");
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
                Reporter::debug(Color::Red, "Connection closed due to error.");
                error = true; return;
            }
            if (size == 0) {
                Reporter::debug(Color::Blue, "Connection closed with EOF.");
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
    PollBuffer(std::string msg_separator, struct pollfd* pollfd) {
        buffer_in_msg_separator = std::move(msg_separator);
        this->pollfd = pollfd;
    }
    // function for making sure the client is disconnected and clearing its buffers (it's called after a poll error)
    void disconnect() {
        // close the socket
        close(pollfd->fd);
        // clear the buffers
        buffer_in.clear();
        buffer_out.clear();
        // clear the pollfd structure
        pollfd->fd = -1;
        pollfd->events = 0;
        pollfd->revents = 0;
        Reporter::debug("Client disconnected.");
    }
    // function called when settings the PollBuffer object for a new client that has just connected (and it's descriptor is in the pollfds array)
    void connect(struct pollfd* _pollfd) {
        // set the pollfd structure
        this->pollfd = _pollfd;
        // clear the buffers
        buffer_in.clear();
        buffer_out.clear();

        // for now, set the pollfd events to POLLIN *only*
        pollfd->events = POLLIN;
    }

    void update() {
        // check if any error occurred and if so, the buffer is broken and the client should be disconnected and his data cleared
        if (updateErrors()) {
            disconnect();
            return;
        }
        updatePollIn();
        updatePollOut();
    }
    bool hasError() const {
        return error;
    }
    bool hasMessage() {
        return buffer_in.find(buffer_in_msg_separator) != std::string::npos;
    }
    std::string readMessage() {
        assert(hasMessage());
        auto pos = buffer_in.find(buffer_in_msg_separator);
        // return the message including the separator and remove it from the buffer
        std::string message = buffer_in.substr(0, pos + buffer_in_msg_separator.size());
        buffer_in.erase(0, pos + buffer_in_msg_separator.size());
        return message;
    }
//    time_t lastMessageTime

    void writeMessage(const std::string& message) {
        assert(!message.empty());
        buffer_out += message;
        // add the POLLOUT flag
        pollfd->events |= POLLOUT;
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
        return config;
    }

};

struct PlayerStats {
    int points_deal = 0;
    int points_total = 0;
    // .
};

class Server {
private:
    ServerConfig config;


    // setter and getter for poll - seat mapping

    std::optional<Seat> pollIndexToSeat[PollConnections]{};
    int SeatToPollIndex(Seat seat) {
//        switch (seat) {
//            case Seat::N: return 0;
//            case Seat::E: return 1;
//            case Seat::S: return 2;
//            case Seat::W: return 3;
//        }
        return 0 + rand() % 2;
    }

    struct Polling {
        static constexpr int PollConnections = 6;
        struct pollfd pollfds[PollConnections];
        std::vector<PollBuffer> poll_buffers; // TODO - maybe let the poll buffers have a fixed mapping 0 - N, 1 - E, 2 - S, 3 - W, but different pointers to pollfds
        // function retruning the number of connected players
        int connectedPlayers() {
            int connected = 0;
            for (int i = 0; i < 4; i++) {
                if (pollfds[i].fd != -1) {
                    connected++;
                }
            }
            return connected;
        }
    } poll;

    void safePoll() {
        while (true) {
            ::poll(pollfds, PollConnections, 1000);

            // if poll has no errors then return
            if (rand() % 2) return;



        }
    }
;

    struct GameData {
        std::vector<DealConfig>::iterator currentDeal;
        std::vector<Taken> takenHistory;
        std::vector<Card> cardsOnTable;

        std::unordered_map<Seat, PlayerStats> playerStats;
        int trickNumber = Trick::FirstTrickNumber; // 1-13
        Seat firstPlayer{};
        Seat currentPlayer{};
        int currentPlayerPollIdx{};

        Seat trickWinner{};

        // Assume: trickNumber is set for the current trick.
        Seat getStartingPlayer() const {
            if (trickNumber == Trick::FirstTrickNumber) {
                return currentDeal->firstSeat;
            }
            return trickWinner;
        }
    } game;


    // ---------------- States ----------------

    // variable pointing to function that handles current state (use wrappers not raw function pointers)
    std::function<void()> state = [] { throw std::runtime_error("State not set."); };
    bool stateShouldPoll = true; // bending spoons

    void stateSendTrick() {
        auto idx = SeatToPollIndex(game.currentPlayer);
        Trick trick(game.trickNumber, game.cardsOnTable);
        poll_buffers[idx].writeMessage(trick.toString()); // todo reporting
        state = [this] { stateWaitForMessage(); };
    }

    void stateWaitForMessage() {
        // Poll is already called and has some revents (possibly only timeout)
        // 1) check if there was any timeout for any player,
        // 2) update the poll timeout to <=smallest interval to upcoming timeout event
        if (!poll_buffers[game.currentPlayerPollIdx].hasMessage()) {
            // Check if timeout has been reached
            Reporter::debug("no messages from the current player..");
            return;
        }

        auto raw_message = poll_buffers[game.currentPlayerPollIdx].readMessage();
        auto msg = Parser::parse(raw_message);

//        if (std::dynamic_pointer_cast<Trick>(msg))
    }

    void stateStartTrick(int trickNumber) {
        game.trickNumber = trickNumber;
        game.currentPlayer = game.firstPlayer = game.getStartingPlayer();
        game.cardsOnTable.clear();

        state = [this] { stateSendTrick(); };
    }

    // Assume: game.currentDeal points to the current deal in config.deals
    void stateStartDeal(std::vector<DealConfig>::iterator dealIt) {
        game.currentDeal = dealIt;

        state = [this] { stateStartTrick(Trick::FirstTrickNumber); };
        stateShouldPoll = false;
    }
    // ----------------------------------------

public:
    Server (ServerConfig _config): config(std::move(_config)) {
//        polldfs
        state = [this] { stateStartDeal(config.deals.begin()); };
    }

    [[noreturn]] void run() {
        while (true) {
            // after calling this function, pollfds are updated, all 4 players are present without any errors:
            if (stateShouldPoll)
                safePoll();
            stateShouldPoll = true;

            // call current state function
            state();


        }
    }
};


void run(int socket) {
    const int StdIn = 0;
    const int StdOut = 1;
    const int Socket = 2;
    const int Connections = 3;
    struct pollfd fds[Connections];
    fds[StdIn].fd = StdIn;
    fds[StdIn].events = POLLIN;
    fds[StdOut].fd = StdOut;
    fds[StdOut].events = 0/*POLLOUT*/; // only when there is something to write
    fds[Socket].fd = socket;
    fds[Socket].events = POLLIN | POLLOUT;

    PollBuffer input("\r\n", &fds[StdIn]);
    Parser parser;

    while (true) {
        for (auto & fd : fds) fd.revents = 0;

        if (poll(fds, Connections, -1) < 0) {
            syserr("poll");
        }

        input.update(); // throws exception on connection error ????

        if (fds[Socket].revents & POLLERR) {
            throw std::runtime_error("POLLERR");
        }

        if (fds[Socket].revents & POLLIN) {
            char buffer[1024];
            ssize_t size = read(socket, buffer, sizeof(buffer));
            if (size < 0) {
                syserr("read");
            }
            if (size == 0) {
                break;
            }
            if (write(StdOut, buffer, (size_t) size) < 0) {
                syserr("write");
            }
        }

        if (fds[Socket].revents & POLLHUP) {
            break;
        }
    }

}

int main(int argc, char** argv) {
    ServerConfig config = ServerConfig::FromArgs(argc, argv);
    Server server(config);
    server.run();
}