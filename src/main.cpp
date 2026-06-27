#include "Game.h"

int main(int, char**) {
    Game game;
    if (!game.init()) return 1;
    return game.run();
}
