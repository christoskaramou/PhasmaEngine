#include "GameKit.h"
#include "ScriptModule.h"
#include <cstdlib>

namespace
{
    // A game the game driver plays (PhasmaGameDriver --policy local): walk the cube onto the coin, 200 moves a game.
    // Each move is scored by the distance it leaves, so the local policy walks straight there.
    class CoinGrid
    {
    public:
        CoinGrid(const phasma::ScriptApi &api, phasma::Node)
            : world(api),
              player(world.CreateCube("CoinGridPlayer", 0.8f)),
              coin(world.CreateSphere("CoinGridCoin", 0.3f)),
              driver(api, Spec())
        {
            Reset(1);
        }

        ~CoinGrid()
        {
            world.Destroy(player);
            world.Destroy(coin);
        }

        void Update(double) {}

    private:
        static constexpr int Size = 9;
        static constexpr int MovesPerGame = 200;

        struct Move
        {
            const char *id;
            const char *description;
            int dx;
            int dz;
        };

        static constexpr Move Moves[] = {
            {"north", "Move one cell north (+z).", 0, 1},
            {"south", "Move one cell south (-z).", 0, -1},
            {"east", "Move one cell east (+x).", 1, 0},
            {"west", "Move one cell west (-x).", -1, 0},
        };

        phasma::gamekit::Game Spec()
        {
            phasma::gamekit::Game game;
            game.id = "coin_grid";
            game.description = "Walk onto the coin on a 9x9 grid; each coin scores 1 and a new one appears. "
                               "A game lasts 200 moves.";
            game.observe = [this](phasma::gamekit::State &state)
            {
                Observe(state);
            };
            game.act = [this](const std::string &id)
            {
                Act(id);
            };
            game.reset = [this](uint32_t seed)
            {
                Reset(seed);
            };
            return game;
        }

        static std::string Cell(int cellX, int cellZ)
        {
            return "[" + std::to_string(cellX) + "," + std::to_string(cellZ) + "]";
        }

        void Observe(phasma::gamekit::State &state) const
        {
            state.terminal = movesLeft == 0;
            state.mode = state.terminal ? "over" : "playing";
            state.score = coins;
            state.observation = "{\"player\":" + Cell(x, z);
            state.observation += ",\"coin\":" + Cell(coinX, coinZ);
            state.observation += ",\"moves_left\":" + std::to_string(movesLeft) + "}";
            if (state.terminal)
                return;

            for (const Move &move : Moves)
            {
                const int nx = x + move.dx;
                const int nz = z + move.dz;
                const bool insideX = nx >= 0 && nx < Size;
                const bool insideZ = nz >= 0 && nz < Size;
                if (!insideX || !insideZ)
                    continue;

                const int distance = std::abs(nx - coinX) + std::abs(nz - coinZ);
                state.actions.push_back({move.id, move.description, -static_cast<double>(distance)});
            }
        }

        void Act(const std::string &id)
        {
            for (const Move &move : Moves)
            {
                if (id == move.id)
                {
                    x += move.dx;
                    z += move.dz;
                }
            }

            --movesLeft;
            if (x == coinX && z == coinZ)
            {
                ++coins;
                PlaceCoin();
            }
            Show();
        }

        void Reset(uint32_t seed)
        {
            rng = seed;
            x = Size / 2;
            z = Size / 2;
            coins = 0;
            movesLeft = MovesPerGame;
            PlaceCoin();
            Show();
        }

        void PlaceCoin()
        {
            do
            {
                rng = rng * 1664525u + 1013904223u; // the same coins for a seed on every platform
                coinX = static_cast<int>((rng >> 16) % Size);
                coinZ = static_cast<int>((rng >> 8) % Size);
            }
            while (coinX == x && coinZ == z);
        }

        void Show() const
        {
            const float half = Size / 2;
            world.SetPosition(player, {x - half, 0.4f, z - half});
            world.SetPosition(coin, {coinX - half, 0.3f, coinZ - half});
        }

        phasma::World world;
        phasma::Node player;
        phasma::Node coin;
        int x = 0;
        int z = 0;
        int coinX = 0;
        int coinZ = 0;
        int coins = 0;
        int movesLeft = 0;
        uint32_t rng = 0;
        phasma::gamekit::Controller driver; // last: its tools read the state above
    };
} // namespace
PHASMA_NODE_SCRIPT(CoinGrid)
