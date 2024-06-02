#include "common.h"


struct DealConfig {
    DealType dealType;
    Seat firstSeat;
    std::unordered_map<Seat, std::vector<Card>> cards;
};




class ServerConfig {
private:
    static std::vector<DealConfig> readDealsFromFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            Reporter::error("Cannot open file: " + filename);
            exit(1);
        }

        std::vector<DealConfig> deals;
        /* Read file for multiple DealConfigs:
         * Format of one DealConfig:
         * <typ rozdania><miejsce przy stole klienta wychodzącego jako pierwszy w rozdaniu>\n
            <lista kart klienta N>\n
            <lista kart klienta E>\n
            <lista kart klienta S>\n
            <lista kart klienta W>\n
         */
        std::string line;
        while (std::getline(file, line)) {
            DealConfig dealConfig;
            dealConfig.dealType = static_cast<DealType>(line[0] - '0');
            dealConfig.firstSeat = Seat(line[1]);
            for (int i = 0; i < 4; ++i) {
                std::getline(file, line);
                auto cards = Parser::parseCards(line);
                dealConfig.cards[Seat("NESW"[i])] = cards;
            }
            deals.push_back(dealConfig);
        }
    }
public:
    std::optional<int> port;
    std::vector<DealConfig> deals;
    int timeout_seconds = 5;

    static ServerConfig FromArgs(int argc, char** argv) {
        ServerConfig config;
        int c;
        try {
            while ((c = getopt(argc, argv, "p:f:t:")) != -1) {
                switch (c) {
                    case 'p':
                        config.port = std::stoi(optarg);
                        break;
                    case 'f':
                        config.deals = readDealsFromFile(optarg);
                        break;
                    case 't':
                        config.timeout_seconds = std::stoi(optarg);
                        break;
                    default:
                        Reporter::error("Invalid argument");
                        break;
                }
            }
        }
        catch (std::invalid_argument& e) {
            Reporter::error("Argument error: " + std::string(e.what()));
            exit(1);
        }

        return config;
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
            PollBuffer buffer{};
            enum State {
                WaitingForIAM,
                Rejecting,
            } state;
            time_t connectionTime{};

            explicit Candidate(PollBuffer buffer, State state = State::WaitingForIAM)
                    : buffer(std::move(buffer)), state(state), connectionTime(time(nullptr)) {}

            explicit Candidate(struct pollfd* pollfd): Candidate(PollBuffer(pollfd)) {}
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
        // ----------- reset revents, run poll and updateBuffers the player poll players ------------
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
        // (1) updateBuffers disconnections and remove disconnected players or candidates
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
            // ----------- reset revents, run poll and updateBuffers the player poll players ------------
            _pollUpdate();

            // (1) updateBuffers disconnections and remove disconnected players
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
    // whether the state should updateBuffers poll before calling the state function
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

        // Send the taken message to all players (including the winner) and updateBuffers the history of taken cards
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
        //    - else if it's a TRICK message, run semantic checks and updateBuffers the game state:
        //        - if the trick is incorrect in the current context (e.g. wrong trick number), send WRONG to the player
        //            - check if the trick number is correct
        //            - check if the trick has exactly 1 card
        //            - check if the player has the card in his hand (store the taken cards separately, they cannot be played again)
        //            - if the card is not the first card in the trick AND the player put a card of a different suit than the first card,
        //                - check if the player has any cards of the first card's suit (if so, send WRONG and of course print the warning)
        //        - if the trick is correct, updateBuffers the game state and change the state to stateSendTrick
        //            - updateBuffers the cards on the table
        //            - if the player is NOT the last one in the trick (4th player),
        //                - change the current player to the next one and change the state to stateSendTrick()
        //            - if the player is the last one in the trick (4th player), do the following:
        //                - find the 'winner' of the trick (the player with the highest card of the first card's suit)
        //                - updateBuffers the trickWinnerSeat
        //                - updateBuffers the stats (scores etc.) according to the type of the current deal (e.g. No7AndLastTrick)
        //                - send the taken message to all players (including the winner) and updateBuffers the history of taken cards
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
            player.stats.takeNewDeal(game.currentDeal->cards[seat], game.currentDeal->dealType);
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



// #####################################################################################################################

int main(int argc, char** argv) {
    install_sigpipe_handler();
//    testParser();
    Busy b({Seat::N, Seat::E, Seat::S, Seat::W});
    Reporter::toUser(b.toStringVerbose());

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
