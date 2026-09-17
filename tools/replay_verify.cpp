#include "localhistoria/status.h"
#include <fstream>
#include <iostream>
using namespace lh;
int main(int argc, char **argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("Usage: replay_verify campaign-export.json");
        std::ifstream file(argv[1]);
        if (!file)
            throw std::runtime_error("Cannot open replay");
        auto input = parse(std::string((std::istreambuf_iterator<char>(file)), {}));
        if (input["initial_state"].isNull())
            throw std::runtime_error("Replay lacks initial state");
        auto world = decode(input["initial_state"]);
        Adjacency adjacency;
        for (auto &a : input["adjacency"].getMemberNames())
            for (auto &b : input["adjacency"][a]) adjacency[a].insert(b.asString());
        int turns = 0;
        for (const auto &turn : input["turns"]) {
            for (auto &actor : turn["intentions"].getMemberNames())
                for(const auto &action:turn["intentions"][actor]) {Json::Value single(Json::arrayValue);single.append(action);parseOrders(single);}
            // Saves that predate national status were seeded from frozen inputs on their first later month.
            if (turn["reference"].isMember("status_seed")) seedStatus(world, decodeSeed(turn["reference"]["status_seed"]), false);
            auto result = resolve(world);
            WorldDelta delta;
            if(turn["reference"].isMember("delta")){delta=parseDelta(turn["reference"]["delta"]);result=applyDelta(result,delta,input["geography"]);}
            if (turn["reference"].isMember("status_version")) result = applyMonthlyStatus(result, delta, adjacency);
            if (encode(result.state) != encode(decode(turn["state"])) || encode(result.events) != turn["events"])
                throw std::runtime_error("Replay mismatch at turn " +
                                         std::to_string(result.state.turn));
            world = result.state;
            turns++;
        }
        if (encode(world) != encode(decode(input["campaign"])))
            throw std::runtime_error("Final campaign mismatch");
        std::cout << turns << " turns replayed identically without inference\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
