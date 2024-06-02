#include "common.h"

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
            Reporter::logError("Missing mandatory arguments. Usage: " + std::string(argv[0]) + " -h host -p port -[4|6] -[N|E|S|W] [-a]");

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
        return AF_UNSPEC;
    }
};

class Client {
    ClientConfig config;
    std::array<pollfd, 3> fds{pollfd{.fd = -1, .events = 0, .revents = 0}, pollfd{.fd = -1, .events = 0, .revents = 0},
                              pollfd{.fd = -1, .events = 0, .revents = 0}};
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
        Server = PollBuffer(&fds[fdServerIdx]);

        if (config.isAutomatic) {
            fds[fdStdinIdx].fd = -1;
            human.connectStdIn(&fds[fdStdinIdx]);
        }
    }

    PollBuffer Server;

    struct HumanPlayer {
        // queue of trick requests that the player has typed and sent via stdin
        std::queue<Card> cardsToTrick{};
        PollBuffer StdIn{}; // actually we don't need PollBuffer StdOut, because we can write to stdout directly
        PlayerStats* stats;

        void updateBuffers() {
            StdIn.update();
            if (StdIn.hasError()) { // validate obvious errors
                Reporter::error("Standard input error. Exiting.");
                exit(1);
            }
        }
        const std::string ShowHandCommand = "cards";   // wyświetlenie listy kart na ręce;
        const std::string ShowTricksCommand = "tricks"; // wyświetlenie listy lew wziętych w ostatniej rozgrywce w kolejności wzięcia – każda lewa to lista kart w osobnej linii.
        const std::regex TrickRequestRegex = std::regex("!([2-9]|10|J|Q|K|A)(S|H|D|C)");

        void _handleMessage(const std::string& raw) {
            if (raw == ShowHandCommand) {
                auto cardsStr = listToString(stats->hand.begin(), stats->hand.end(),
                                             [](const Card &card) { return card.toString(); });
                Reporter::toUser("Cards in your hand: " + cardsStr + ".");
            }
            else if (raw == ShowTricksCommand) {
                Reporter::toUser("Tricks taken in the last deal:");
                for (const auto& cards: stats->tricks_taken) {
                    Reporter::toUser(listToString<Card>(cards, [](const Card &card) { return card.toString(); }));
                }
                Reporter::toUser("--- End of list ---");
            }
            else if (std::regex_match(raw, TrickRequestRegex)) {
                // todo check if the user can send a trick request at the moment?
                // parse the user trick request
                auto cardStr = raw.substr(1);
                auto card = Card(cardStr);
                cardsToTrick.push(card);
                Reporter::debug(Color::Green, "Received a trick request: " + card.toString() + ".");
            }
            else {
                Reporter::logWarning("Unexpected message from the user: " + raw + " (skipping...)");
            }
        }

        void handleMessages() {
            while (StdIn.hasMessage()) {
                auto raw = StdIn.readMessage();
                _handleMessage(raw);
            }
        }

        void connectStdIn(pollfd* fd) {
            StdIn = PollBuffer(fd);
        }
        explicit HumanPlayer(PlayerStats* stats): stats(stats) {}
    } human = HumanPlayer(&stats);

    std::function<void()> state = [] { throw std::runtime_error("State not set."); };
    void ChangeState(std::function<void()> newState) {
        state = std::move(newState);
        state(); // move to the new state immediately to prevent lock on poll
    }

    PlayerStats stats;

    void _updateStatsWithTaken(const std::shared_ptr<Taken>& taken) {
        if (taken->takerSeat == config.seat) {
            stats.takeTrick(taken->cardsOnTable, 0); // player doesn't need to count points [feature]
        }
    }

    void stateWaitForNewDeal() {
        if (Server.hasError()) {
            // sever disconnected, but that's a good moment, because a new hasn't started
            Reporter::log("------- Game over. Server disconnected. -------");
            exit(0);
        }

        if (!Server.hasMessage()) { return; }

        auto raw = Server.readMessage();
        auto msg = Parser::parse(raw);

        if (auto deal= std::dynamic_pointer_cast<Deal>(msg)) {
            Reporter::toUser(deal->toStringVerbose());
            stats.takeNewDeal(deal->cards, deal->dealType);
            ChangeState([this] { stateWaitForTrick(); });
        }
        else if (auto busy = std::dynamic_pointer_cast<Busy>(msg)) {
            Reporter::toUser(busy->toStringVerbose());
            exit(1); // exit with error because the seat is taken
        }
        else {
            Reporter::logWarning("Unexpected message from the server: " + raw + " (skipping...)");
        }
    }

    void _exit1IfServerError() const {
        if (Server.hasError()) {
            Reporter::logError("Server disconnected unexpectedly. Exiting.");
            exit(1);
        }
    }

    // function for read and parse in one line that you can auto [msg, raw] = readAndParse();
    auto readAndParse() {
        auto raw = Server.readMessage();
        auto msg = Parser::parse(raw);
        return std::make_pair(msg, raw);
    }

    void stateWaitForTakenOrWrong(const Card& trickedCard, const Trick& serverTrick) {
        _exit1IfServerError();

        if (!Server.hasMessage()) { return; }

        auto [msg, raw] = readAndParse();

        if (auto taken = std::dynamic_pointer_cast<Taken>(msg)) {
            Reporter::toUser(taken->toStringVerbose());
            stats.removeCard(trickedCard); // successful trick request
            _updateStatsWithTaken(taken);
            ChangeState([this] { stateWaitForTrick(); });
        }
        else if (auto wrong = std::dynamic_pointer_cast<Wrong>(msg)) {
            Reporter::toUser(wrong->toStringVerbose());
            ChangeState([this, &serverTrick] { stateWaitForTrickWaitForPlayerTrick(serverTrick); });
        }
        else {
            Reporter::logWarning("Unexpected message from the server: " + raw + " (skipping...)");
        }
    }

    std::optional<Card> _chooseCardToTrick(const Trick& serverTrick) {
        if (config.isAutomatic) {
            return robot.chooseCardToTrick(serverTrick);
        }
        else {
            // check if player has already made a request to trick a card
            if (human.cardsToTrick.empty()) { return std::nullopt; }

            // and pop the first player decision
            auto cardToTrick = human.cardsToTrick.front();
            human.cardsToTrick.pop();
            return cardToTrick;
        }
    }


    void stateWaitForTrickWaitForPlayerTrick(const Trick& serverTrick) {
        _exit1IfServerError();

        // here we expect the server only to resend us the trick message
        if (Server.hasMessage()) {
            auto raw = Server.readMessage();
            auto msg = Parser::parse(raw);
            if (auto trick = std::dynamic_pointer_cast<Trick>(msg)) {
                Reporter::toUser(trick->toStringVerbose());
                // print available cards in hand
                Reporter::toUser(stats.handToString());
            }
            else {
                Reporter::logWarning("Unexpected message from the server: " + raw + " (skipping...)");
            }
        }

        // choose the card to trick (or exit if the player hasn't made a decision yet)
        auto optionalCardToTrick = _chooseCardToTrick(serverTrick);
        if (!optionalCardToTrick.has_value()) { return; } // wait for the player to make a decision
        Card cardToTrick = optionalCardToTrick.value();

        // send the trick message to the server
        Trick trick(stats.getCurrentTrickNumber(), {cardToTrick});
        Server.writeMessage(trick);

        Reporter::debug(Color::Green, "Processed and sent a trick request to the server: " + trick.toString() + ".");
        ChangeState([this, &cardToTrick, &serverTrick] { stateWaitForTakenOrWrong(cardToTrick, serverTrick); });
    }

    struct Robot {
        PlayerStats* stats;
        explicit Robot(PlayerStats* stats): stats(stats) {}
        [[nodiscard]] Card chooseCardToTrick(const Trick& serverTrick) const {
            if (stats->hand.empty()) {
                throw std::runtime_error("Player has NO CARDS in hand, but was asked to TRICK");
            }
            // choose the first suitable card from the stats->hand
            if (serverTrick.cards.empty()) {
                return *stats->hand.begin();
            }
            for (const auto& card: stats->hand) {
                if (card.suit == serverTrick.cards[0].suit) {
                    return card;
                }
            }
            return *stats->hand.begin(); // if no suitable card, return the first one
        }
    } robot = Robot(&stats);

    void stateWaitForTrick() {
        _exit1IfServerError();

        if (!Server.hasMessage()) { return; }

        auto [msg, raw] = readAndParse();

        if (auto taken = std::dynamic_pointer_cast<Taken>(msg)) {
            Reporter::toUser(taken->toStringVerbose());
            // if we are receiving a history of this seat's player, updateBuffers the stats
            _updateStatsWithTaken(taken);
        }
        else if (auto score = std::dynamic_pointer_cast<Score>(msg)) {
            Reporter::toUser(score->toStringVerbose());
            ChangeState([this] { stateWaitForTotal(); });
        }
        else if (auto total = std::dynamic_pointer_cast<Total>(msg)) {
            Reporter::toUser(total->toStringVerbose());
            ChangeState([this] { stateWaitForNewDeal(); });
        }
        else if (auto trick = std::dynamic_pointer_cast<Trick>(msg)) {
            // the server wants us to send the trick message
            Reporter::toUser(trick->toStringVerbose());
            Reporter::toUser(stats.handToString());
            ChangeState([this, &trick] { stateWaitForTrickWaitForPlayerTrick(*trick); }); // very important! is to move to next state immediately
        }
        else {
            Reporter::logWarning("Unexpected message from the server: " + raw + " (skipping...)");
        }
    }

    void stateWaitForTotal() {
        _exit1IfServerError();

        if (!Server.hasMessage()) { return; }

        auto raw = Server.readMessage();
        auto msg = Parser::parse(raw);
        if (auto total = std::dynamic_pointer_cast<Total>(msg)) {
            Reporter::toUser(total->toStringVerbose());
            Reporter::log("Waiting for the next deal...");
            ChangeState([this] { stateWaitForNewDeal(); });
        }
        else {
            Reporter::logWarning("Unexpected message from the server: " + raw + " (skipping...)");
        }
    }


    void RePoll() {
        // reset revents, run poll and updateBuffers the buffers
        for (auto &fd: fds) {
            fd.revents = 0;
        }
        ::poll(fds.data(), fds.size(), -1); // blocking

        Server.update();
        if (!config.isAutomatic) {
            human.updateBuffers();
        }
    }

public:
    explicit Client(ClientConfig config): config(std::move(config)), Server() {}

    [[noreturn]] void run() {
        setup_poll_and_buffers();

        // send IAM message to the server
        Server.writeMessage(IAm(config.seat));


        while (true) {
            // make sure there is some event and StdIn is not broken
            RePoll();

            if (!config.isAutomatic) {
                // handle player input (if any), but if there's a trick input, don't handle it yet
                human.handleMessages(); // (just add to queue of trick requests or something)
            }

            // call current state function (possibly with one buffer error - server disconnected)
            state();
        }
    }
};

int main(int argc, char* argv[]) {
    auto config = ClientConfig::FromArgs(argc, argv);
    Client client(config);
    client.run();
}