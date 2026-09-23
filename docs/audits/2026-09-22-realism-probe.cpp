#include "localhistoria/status.h"
#include <iostream>
using namespace lh;
WorldState fixture() {
    WorldState w; w.campaignId="audit"; w.player="AA"; w.turn=4;
    for (auto a : {"AA","BB","CC"}) {
        auto &v=w.actors[a]; v["name"]= a==std::string("AA")?"Aland":a==std::string("BB")?"Bland":"Cland";
        v["color"]="#123456";
        v["status"]=parse(R"({"stability":70,"baseline_stability":70,"seeded_stability":70,"government":"parliamentary_republic","gdp_growth":0,"pop_growth":0,"military":{"active":100000,"reserve":0,"aircraft":100,"ships":30}})");
        std::string id=a;
        auto &t=w.territories[id]; t["name"]=id; t["owner"]=a;t["controller"]=a;t["initial_affiliation"]=a;
        t["kind"]="ADM1";t["population"]=10000000;t["gdp_usd"]=Json::Int64(200000000000);
        t["longitude"]= a==std::string("AA")?0:1;t["latitude"]=0;t["coastal"]=false;
    }
    w.conflicts["war"]=parse(R"({"actors":["AA","BB"],"kind":"war","name":"Test War","text":"War","status":"open","started_turn":0,"set_intensity":"high","intensity":"high","theatre":["AA","BB"]})");
    return w;
}
Json::Value event(std::string a,std::string text,Json::Value ops) {
    auto e=parse(R"({"blocked_by":"none","background":false,"causes":[]})");e["actor"]=a;e["description"]=text;e["operations"]=ops;return e;
}
WorldDelta compile(WorldState w,Json::Value events) {
    Json::Value response;response["events"]=events;return compileOperations(w,response,{{"AA",{"BB"}},{"BB",{"AA","CC"}},{"CC",{"BB"}}});
}
Json::Value list(Json::Value e) {Json::Value x(Json::arrayValue);x.append(e);return x;}
int main() {
    auto w=fixture();
    auto d=compile(w,list(event("AA","Aland stops the war.",parse(R"([{"action":"resolve_conflict","conflict":"war"}])"))));
    std::cout<<"unilateral_end="<<dump(d.changes)<<"\n";
    auto es=list(event("AA","Aland offers trade with Bland.",parse(R"([{"action":"add_commitment","actors":["AA","BB"],"kind":"trade_agreement","text":"Trade","name":"Test Accord"}])")));
    es.append(event("BB","Bland rejects Aland's trade offer.",Json::Value(Json::arrayValue)));
    d=compile(w,es);std::cout<<"rejection_as_consent="<<dump(d.changes)<<"\n";
    auto bad=event("AA","Aland builds a city in foreign territory.",parse(R"([{"action":"build","actor":"AA","region":"BB","kind":"city","name":"Impossible City"}])"));
    bad["effects"]=parse(R"([{"actor":"AA","economy":"strong_growth","stability":"strong_growth"}])");
    d=compile(w,list(bad));std::cout<<"failed_effects="<<dump(d.events)<<"\n";
    w.entities["fleet"]=parse(R"({"name":"Fleet","actor":"AA","kind":"fleet","region":"AA","status":"active","ships":3,"longitude":0,"latitude":0})");
    d=compile(w,list(event("AA","Move inland.",parse(R"([{"action":"move_entity","entity":"fleet","region":"CC"}])"))));
    std::cout<<"inland_fleet="<<dump(d.changes)<<"\n";
    w=fixture();
    Resolution r{w,{}};
    for(int i=0;i<24;i++){r=applyMonthlyStatus(resolve(r.state),{},{{"AA",{"BB"}},{"BB",{"AA"}}});}
    std::cout<<"after_24_war_months="<<dump(r.state.actors["AA"]["status"]["military"])<<"\n";
    w=fixture();w.conflicts["war"]["actors"].append("CC");
    w.commitments["truce"]=parse(R"({"actors":["AA","BB"],"kind":"ceasefire","status":"open"})");
    r=applyMonthlyStatus(resolve(w),{},{});std::cout<<"partial_truce="<<dump(r.state.conflicts["war"])<<"\n";
    w=fixture();w.conflicts=Json::objectValue;w.territories["BB"]["controller"]="AA";
    r=applyMonthlyStatus(resolve(w),{},{});std::cout<<"stable_occupier_unrest="<<dump(r.state.territories["BB"]["unrest"])<<"\n";
    w=fixture();w.conflicts=Json::objectValue;
    w.territories["AA"]["displaced"]=1000;
    r=applyMonthlyStatus(resolve(w),{},{});std::cout<<"refugees_no_return_route="<<dump(r.state.territories["AA"])<<"\n";
}
