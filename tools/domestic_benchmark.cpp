// Fixed synthetic compiler/monthly workloads; usable against both pre-31 and current libraries.
#include "localhistoria/lifecycle.h"
#include <chrono>
#include <iostream>
using namespace lh;
int main(int argc, char **argv) {
    const int actors = argc > 1 ? std::stoi(argv[1]) : 16;
    const int repeats = argc > 2 ? std::stoi(argv[2]) : 500;
    WorldState w; w.player = "A0"; w.campaignId = "benchmark";
    for (int n = 0; n < actors; ++n) {
        auto a = "A" + std::to_string(n);
        w.actors[a] = parse(R"({"color":"#123456","status":{"baseline_stability":60,"stability":60,"seeded_stability":60,"gdp_growth":0,"pop_growth":0,"government":"parliamentary_republic"}})");
        w.actors[a]["name"] = a;
        for (int r=0;r<2;++r) {
            auto id = a + "r" + std::to_string(r); auto &t=w.territories[id];
            t["owner"]=t["controller"]=t["initial_affiliation"]=a;t["name"]=id;
            t["kind"]="ADM1";t["population"]=1000000;t["gdp_usd"]=Json::Int64(100000000000LL);
        }
    }
    const auto refs=lifecycleReference(w);const auto base=beginTurn(w,refs);
    for (const std::string mode : {"policy", "investment"}) {
        Json::Value response;std::vector<std::pair<std::string,std::string>> actions;
        for (int n=0;n<8;++n) {
            auto e=parse(R"({"actor":"A0","blocked_by":"none","background":false,"causes":[],"operations":[],"effects":[{"actor":"A0","economy":"growth","stability":"growth"}]})");
            e["description"]="Fixed policy "+std::to_string(n);e["outcome_kind"]=mode;
            e["project"] = parse(R"({"id":"","action":"start","objective":"Output improvement","benefit":"infrastructure"})");
            e["project"]["target"]=n%2?"A0r0":"A0r1";
            response["events"].append(e);actions.emplace_back("A0",e["description"].asString());
        }
        auto stamp=[] {return std::chrono::steady_clock::now();};
        auto start=stamp();WorldDelta delta;
        for(int n=0;n<repeats;++n) delta=compileOperations(base.state,response,{});
        auto compiled=stamp();Resolution result;
        for(int n=0;n<repeats;++n) result=finishTurn(base,delta,{}, {},refs);
        auto finished=stamp();
        auto ms=[](auto x,auto y){return std::chrono::duration<double,std::milli>(y-x).count();};
        Json::Value row;row["actors"]=actors;row["mode"]=mode;row["repeats"]=repeats;
        row["compile_ms"]=ms(start,compiled);row["monthly_ms"]=ms(compiled,finished);
        row["state_bytes"]=Json::UInt64(dump(encode(result.state)).size());
        row["schema_bytes"]=Json::UInt64(dump(operationSchema(base.state,{},actions)).size());
        std::cout<<dump(row)<<'\n';
    }
}
