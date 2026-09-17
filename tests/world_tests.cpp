#include "localhistoria/world.h"
#include "localhistoria/store.h"
#include <iostream>
using namespace lh;
void check(bool x){if(!x)throw std::runtime_error("Assertion failed");}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const ValidationError &){bad=true;}check(bad);}
int main(){try{
 WorldState w;w.player="old-state";w.campaignId="fixture";w.countries[w.player]={};w.countries["neighbor"]={};
 w.actors=parse(R"({"old-state":{"name":"Old","color":"#123456"},"neighbor":{"name":"Neighbor","color":"#654321"}})");
 w.territories=parse(R"({"west":{"name":"West","initial_affiliation":"old-state","owner":"old-state","controller":"old-state"},"east":{"name":"East","initial_affiliation":"old-state","owner":"old-state","controller":"old-state"}})");
 auto g=parse(R"({"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]],[[4,4],[6,4],[6,6],[4,6],[4,4]]]})");Json::Value geo;geo["west"]=g;geo["east"]=g;
 check(containsPoint(g,1,1));check(!containsPoint(g,5,5));check(!containsPoint(g,20,20));
 auto base=resolve(w,{});WorldDelta d;
 d.events=parse(R"([{"id":"t1-change","actor":"old-state","actors":["old-state"],"regions":["west"],"entities":[],"causes":[],"text":"West changes hands.","background":false}])");
 auto change=[&](std::string kind,std::string id,Json::Value before,Json::Value after){Json::Value c;c["kind"]=kind;c["id"]=id;c["before"]=before;c["after"]=after;c["event"]="t1-change";c["rationale"]="Fixture outcome";d.changes.append(c);};
 auto west=w.territories["west"];west["controller"]="neighbor";change("territory","west",w.territories["west"],west);
 auto occupied=applyDelta(base,d,geo);check(occupied.state.territories["west"]["owner"]=="old-state");check(occupied.state.territories["west"]["controller"]=="neighbor");
 d.changes[0]["after"]["owner"]="neighbor";auto transferred=applyDelta(base,d,geo);check(transferred.state.territories["east"]["owner"]=="old-state");
 auto saved=d;d.changes.append(d.changes[0]);rejects([&]{applyDelta(base,d,geo);});d=saved;
 d.changes[0]["before"]["owner"]="neighbor";rejects([&]{applyDelta(base,d,geo);});d=saved;
 d.changes=Json::arrayValue;change("actor","new-state",Json::nullValue,parse(R"({"name":"New State","color":"#abcdef"})"));
 west["owner"]="new-state";west["controller"]="new-state";change("territory","west",w.territories["west"],west);
 auto donor=encode(base.state)["countries"][w.player];auto reduced=donor;reduced["treasury"]=60;change("resources",w.player,donor,reduced);
 CountryState zero{0,0,0,0,0,"defensive","balanced"};WorldState z;z.countries["new-state"]=zero;auto empty=encode(z)["countries"]["new-state"],funded=empty;funded["treasury"]=50;change("resources","new-state",empty,funded);
 auto split=applyDelta(base,d,geo);check(split.state.countries.at("new-state").treasury==50);check(split.state.countries.at(w.player).treasury==60);
 saved=d;d.changes[3]["after"]["treasury"]=51;rejects([&]{applyDelta(base,d,geo);});d=saved;
 auto entity=parse(R"({"name":"Harbor","kind":"port","actor":"old-state","region":"east","longitude":1,"latitude":1,"status":"active","origin_event":"t1-change"})");change("entity","port-1",Json::nullValue,entity);auto built=applyDelta(base,d,geo);check(built.state.entities.isMember("port-1"));
 saved=d;d.changes[4]["after"]["longitude"]=5;d.changes[4]["after"]["latitude"]=5;rejects([&]{applyDelta(base,d,geo);});d=saved;
 check(encode(decode(encode(built.state)))==encode(built.state));check(encode(applyDelta(base,parseDelta(encode(d)),geo).state)==encode(built.state));
 auto moveBase=resolve(built.state,{});WorldDelta move;
 move.events=d.events;move.events[0]["id"]="t2-move";
 auto moved=built.state.entities["port-1"];moved["longitude"]=2;
 Json::Value movement;movement["kind"]="entity";movement["id"]="port-1";movement["before"]=built.state.entities["port-1"];movement["after"]=moved;movement["event"]="t2-move";movement["rationale"]="Relocation";move.changes.append(movement);
 check(applyDelta(moveBase,move,geo).state.entities["port-1"]["longitude"].asInt()==2);
 auto landless=w;landless.territories["east"]["owner"]="neighbor";landless.territories["west"]["owner"]="neighbor";
 check(resolve(landless,{{"old-state",{{"offer","neighbor",""}}}}).state.countries.contains("old-state"));
 auto incoming=resolve(w,{{"neighbor",{{"offer","old-state",""}}}});check(context(incoming.state,"old-state",{},{}).facts["offers"].size()==1);
 Store store(":memory:");store.create(w,false);auto job=store.submit(0,"key","{}");Json::Value refs;refs["delta"]=encode(d);store.commit(job["id"].asString(),built,{},refs);
 auto c=store.campaignContext(built.state,w.player);check(c.facts["related_history"].size()==1);
 auto current=built.state;
 for(int n=0;n<30;++n){auto next=resolve(current,{});auto j=store.submit(current.turn,"key-"+std::to_string(n),"{}");store.commit(j["id"].asString(),next,{},{});current=next.state;}
 check(store.campaignContext(current,w.player).facts["related_history"][0]["id"]=="t1-change");
 auto previous=store.campaign();auto j=store.submit(current.turn,"bad-causes","{}");auto next=resolve(current,{});WorldDelta bad;bad.events=d.events;bad.events[0]["id"]="t32-bad";bad.events[0]["causes"].append("missing");refs["delta"]=encode(bad);rejects([&]{store.commit(j["id"].asString(),next,{},refs);});check(store.campaign()==previous);
 // Compact model operations compile into validated exact deltas.
 auto opsBase=resolve(w,{});opsBase.state.territories["west"]["longitude"]=1;opsBase.state.territories["west"]["latitude"]=1;
 auto facts=encode(opsBase.state);auto schema=operationSchema(opsBase.state,facts);check(schema["properties"]["events"]["items"]["properties"]["operations"]["items"]["anyOf"].size()>=8);
 auto ops=parse(R"({"events":[{"description":"West Secedes!","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[
  {"action":"create_actor","actor_id":"west-rep","name":"West Republic","color":"#00aa00"},{"action":"transfer_territory","region":"west","to":"west-rep"},
  {"action":"transfer_resources","from":"old-state","to":"west-rep","treasury":30,"income":3,"army":0,"navy":0},
  {"action":"build","kind":"port","name":"Harbor","actor":"west-rep","region":"west"},{"action":"build","kind":"port","name":"Harbor","actor":"west-rep","region":"west"},{"action":"spend","actor":"west-rep","treasury":10,"army":0,"navy":0},
  {"action":"open_conflict","actors":["old-state","west-rep"],"text":"Dispute"}]},{"description":"West secedes","actor":"neighbor","blocked_by":"none","background":true,"causes":[],"operations":[{"action":"occupy","region":"east","by":"neighbor"}]}]})");
 opsBase.state.turn=1;auto compiled=compileOperations(opsBase.state,ops);check(compiled.events[0]["id"]=="t1-west-secedes"&&compiled.events[1]["id"]=="t1-west-secedes-2");
 auto seceded=applyDelta(opsBase,compiled,geo);
 check(seceded.state.territories["west"]["owner"]=="west-republic"&&seceded.state.territories["east"]["controller"]=="neighbor"&&seceded.state.territories["east"]["owner"]=="old-state");
 check(seceded.state.countries.at("west-republic").treasury==20&&seceded.state.countries.at("west-republic").income==3&&seceded.state.countries.at(w.player).treasury==opsBase.state.countries.at(w.player).treasury-30);
 check(seceded.state.entities["e1-harbor"]["origin_event"]=="t1-west-secedes"&&seceded.state.conflicts.size()==1);
 auto overspend=ops;overspend["events"][0]["operations"][5]["treasury"]=31;auto unpaid=compileOperations(opsBase.state,overspend);
 check(unpaid.events[0]["blocked_by"]=="cannot_pay"&&unpaid.changes.size()==1&&unpaid.changes[0]["kind"]=="territory"&&unpaid.events[1]["regions"].size()==1);
 auto badRegion=ops;badRegion["events"][0]["operations"][1]["region"]="nowhere";rejects([&]{compileOperations(opsBase.state,badRegion);});
 auto orphan=ops;orphan["events"][0]["operations"][1]["to"]="old-state";rejects([&]{applyDelta(opsBase,compileOperations(opsBase.state,orphan),geo);});
 auto blocked=ops;blocked["events"][0]["blocked_by"]="treaty-9";auto denied=compileOperations(opsBase.state,blocked);check(denied.changes.size()==1&&denied.events[0]["blocked_by"]=="treaty-9");
 auto alias=parse(R"({"events":[{"description":"Free","actor":"old-state","blocked_by":"none","causes":[],"background":false,"operations":[
  {"action":"create_actor","actor_id":"old-state","name":"Free West","color":"#00aa00"},{"action":"transfer_territory","region":"west","to":"Free_West"}]}]})");
 auto aliased=applyDelta(opsBase,compileOperations(opsBase.state,alias),geo);check(aliased.state.territories["west"]["owner"]=="free-west"&&aliased.state.actors["free-west"]["name"]=="Free West");
 std::cout<<"World deltas: partial transfer, occupation, secession, conservation, entity placement, holes, conflicts, reload, replay, old history and atomic rollback passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
