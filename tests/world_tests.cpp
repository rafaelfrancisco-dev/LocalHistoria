#include "localhistoria/world.h"
#include "localhistoria/store.h"
#include <iostream>
using namespace lh;
void check(bool x,const char *what="Assertion failed"){if(!x)throw std::runtime_error(what);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const ValidationError &){bad=true;}check(bad,"Expected rejection");}
int main(){try{
 WorldState w;w.player="old-state";w.campaignId="fixture";
 w.actors=parse(R"({"old-state":{"name":"Old","color":"#123456"},"neighbor":{"name":"Neighbor","color":"#654321"}})");
 w.territories=parse(R"({"west":{"name":"West","initial_affiliation":"old-state","owner":"old-state","controller":"old-state"},"east":{"name":"East","initial_affiliation":"old-state","owner":"old-state","controller":"old-state"}})");
 auto g=parse(R"({"type":"Polygon","coordinates":[[[0,0],[10,0],[10,10],[0,10],[0,0]],[[4,4],[6,4],[6,6],[4,6],[4,4]]]})");Json::Value geo;geo["west"]=g;geo["east"]=g;
 check(containsPoint(g,1,1));check(!containsPoint(g,5,5));check(!containsPoint(g,20,20));
 auto base=resolve(w);WorldDelta d;
 d.events=parse(R"([{"id":"t1-change","actor":"old-state","actors":["old-state"],"regions":["west"],"entities":[],"causes":[],"text":"West changes hands.","background":false}])");
 auto change=[&](std::string kind,std::string id,Json::Value before,Json::Value after){Json::Value c;c["kind"]=kind;c["id"]=id;c["before"]=before;c["after"]=after;c["event"]="t1-change";c["rationale"]="Fixture outcome";d.changes.append(c);};
 auto west=w.territories["west"];west["controller"]="neighbor";change("territory","west",w.territories["west"],west);
 auto occupied=applyDelta(base,d,geo);check(occupied.state.territories["west"]["owner"]=="old-state");check(occupied.state.territories["west"]["controller"]=="neighbor");
 d.changes[0]["after"]["owner"]="neighbor";auto transferred=applyDelta(base,d,geo);check(transferred.state.territories["east"]["owner"]=="old-state");
 auto saved=d;d.changes.append(d.changes[0]);rejects([&]{applyDelta(base,d,geo);});d=saved;
 d.changes[0]["before"]["owner"]="neighbor";rejects([&]{applyDelta(base,d,geo);});d=saved;
 d.changes=Json::arrayValue;change("actor","new-state",Json::nullValue,parse(R"({"name":"New State","color":"#abcdef"})"));
 west["owner"]="new-state";west["controller"]="new-state";change("territory","west",w.territories["west"],west);
 auto split=applyDelta(base,d,geo);check(split.state.hasActor("new-state")&&split.state.territories["west"]["owner"]=="new-state");
 auto entity=parse(R"({"name":"Harbor","kind":"port","actor":"old-state","region":"east","longitude":1,"latitude":1,"status":"active","origin_event":"t1-change"})");change("entity","port-1",Json::nullValue,entity);auto built=applyDelta(base,d,geo);check(built.state.entities.isMember("port-1"));
 saved=d;d.changes[2]["after"]["longitude"]=5;d.changes[2]["after"]["latitude"]=5;rejects([&]{applyDelta(base,d,geo);});d=saved;
 saved=d;d.changes[2]["after"]["status"]="under_construction";rejects([&]{applyDelta(base,d,geo);});d.changes[2]["after"]["completes_turn"]=4;applyDelta(base,d,geo);d=saved;
 check(encode(decode(encode(built.state)))==encode(built.state));check(encode(applyDelta(base,parseDelta(encode(d)),geo).state)==encode(built.state));
 auto moveBase=resolve(built.state);WorldDelta move;
 move.events=d.events;move.events[0]["id"]="t2-move";
 auto moved=built.state.entities["port-1"];moved["longitude"]=2;
 Json::Value movement;movement["kind"]="entity";movement["id"]="port-1";movement["before"]=built.state.entities["port-1"];movement["after"]=moved;movement["event"]="t2-move";movement["rationale"]="Relocation";move.changes.append(movement);
 check(applyDelta(moveBase,move,geo).state.entities["port-1"]["longitude"].asInt()==2);
 Store store(":memory:");store.create(w,false);auto job=store.submit(0,"key","{}");Json::Value refs;refs["delta"]=encode(d);store.commit(job["id"].asString(),built,{},refs);
 auto c=store.campaignContext(built.state,w.player);check(c.facts["related_history"].size()==1);
 auto current=built.state;
 for(int n=0;n<30;++n){auto next=resolve(current);auto j=store.submit(current.turn,"key-"+std::to_string(n),"{}");store.commit(j["id"].asString(),next,{},{});current=next.state;}
 check(store.campaignContext(current,w.player).facts["related_history"][0]["id"]=="t1-change");
 auto previous=store.campaign();auto j=store.submit(current.turn,"bad-causes","{}");auto next=resolve(current);WorldDelta bad;bad.events=d.events;bad.events[0]["id"]="t32-bad";bad.events[0]["causes"].append("missing");refs["delta"]=encode(bad);rejects([&]{store.commit(j["id"].asString(),next,{},refs);});check(store.campaign()==previous);

 // Model operations compile into validated deltas; physically impossible events are kept as failures without changes.
 WorldState m;m.player="old-state";m.campaignId="ops";
 m.actors=w.actors;m.actors["far"]=parse(R"({"name":"Far","color":"#999999"})");
 m.territories=parse(R"({"west":{"name":"West","owner":"old-state","controller":"old-state","longitude":1,"latitude":1},"east":{"name":"East","owner":"old-state","controller":"old-state","longitude":1,"latitude":1},
  "march":{"name":"March","owner":"neighbor","controller":"neighbor","longitude":1,"latitude":1},"heart":{"name":"Heart","owner":"neighbor","controller":"neighbor","longitude":1,"latitude":1},
  "hills":{"name":"Hills","owner":"neighbor","controller":"neighbor","longitude":1,"latitude":1},"island":{"name":"Island","owner":"far","controller":"far","longitude":1,"latitude":1}})");
 for(auto id:{"march","heart","hills","island"})geo[id]=g;
 Adjacency adjacency{{"west",{"east","march","hills"}},{"east",{"west","heart"}},{"march",{"west","heart"}},{"heart",{"march","east"}},{"hills",{"west"}},{"island",{}}};
 m.turn=1;auto opsBase=resolve(m);opsBase.state.turn=1;
 auto facts=encode(opsBase.state);auto schema=operationSchema(opsBase.state,facts,{{"old-state","Build a port"},{"neighbor","Wait"}});
 auto &outcomes=schema["properties"]["events"];
 check(outcomes["minItems"].asInt()==2&&outcomes["maxItems"].asInt()==2&&!outcomes.isMember("items")&&outcomes["prefixItems"][0]["properties"]["action"]["const"]=="Build a port"&&outcomes["prefixItems"][1]["properties"]["actor"]["const"]=="neighbor","One outcome per action");
 check(outcomes["prefixItems"][0]["properties"]["operations"]["items"]["anyOf"].size()==10,"Schema");
 auto &developments=schema["properties"]["world_events"];
 check(developments["maxItems"].asInt()==2&&!developments["items"]["properties"].isMember("operations")&&developments["items"]["properties"]["actor"]["enum"]==parse(R"(["far","neighbor"])"),"World events are narrative and never the player's decisions");
 auto split2=compileOperations(opsBase.state,parse(R"({"events":[{"action":"Build a port","actor":"old-state","blocked_by":"none","causes":[],"description":"Port begun","operations":[]}],"world_events":[{"actor":"neighbor","blocked_by":"none","causes":[],"description":"Harvest festival","operations":[]}]})"),{});
 auto invented=compileOperations(opsBase.state,parse(R"({"events":[],"world_events":[{"actor":"old-state","causes":[],"description":"Old builds a fortress"},{"actor":"neighbor","causes":[],"description":"Neighbor takes West","operations":[{"action":"occupy","region":"west","by":"neighbor"}]}]})"),{});
 check(invented.events.empty()&&invented.changes.empty(),"Invented player decisions and map changes are dropped");
 check(split2.events.size()==2&&!split2.events[0]["background"].asBool()&&split2.events[1]["background"].asBool(),"Outcomes then world events");
 auto compile=[&](const char *json){return compileOperations(opsBase.state,parse(json),adjacency);};
 auto ops=parse(R"({"events":[{"description":"West Secedes!","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[
  {"action":"create_actor","actor_id":"west-rep","name":"West Republic","color":"#00aa00"},{"action":"transfer_territory","region":"west","to":"west-rep"},
  {"action":"build","kind":"port","name":"Harbor","actor":"old-state","region":"east"},{"action":"build","kind":"port","name":"Harbor","actor":"old-state","region":"east"},
  {"action":"open_conflict","actors":["old-state","west-rep"],"text":"Dispute"}]},{"description":"West secedes","actor":"neighbor","blocked_by":"none","background":true,"causes":[],"operations":[{"action":"occupy","region":"west","by":"neighbor"}]}]})");
 auto compiled=compileOperations(opsBase.state,ops,adjacency);check(compiled.events[0]["id"]=="t1-west-secedes"&&compiled.events[1]["id"]=="t1-west-secedes-2","Event IDs");
 auto seceded=applyDelta(opsBase,compiled,geo);
 check(seceded.state.territories["west"]["owner"]=="west-republic"&&seceded.state.territories["west"]["controller"]=="neighbor","Secession and bordering occupation");
 check(seceded.state.entities["e1-harbor"]["origin_event"]=="t1-west-secedes"&&seceded.state.entities.size()==1&&seceded.state.conflicts.size()==1,"Deduplicated construction");
 check(seceded.state.entities["e1-harbor"]["status"]=="under_construction"&&seceded.state.entities["e1-harbor"]["completes_turn"]==1+constructionMonths("port"),"Construction takes months");
 auto later=resolve(seceded.state);for(int n=1;n<constructionMonths("port");++n)later=resolve(later.state);check(later.state.entities["e1-harbor"]["status"]=="active"&&later.events.size()==1,"Construction completes");
 auto infeasible=[&](const WorldDelta &x,Json::ArrayIndex index=0){return x.events[index]["blocked_by"]=="infeasible"&&x.events[index]["text"].asString().rfind("Could not happen this month",0)==0;};
 // Out of reach: Heart borders no land the aggressor controls.
 auto far=compile(R"({"events":[{"description":"Deep strike","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"occupy","region":"march","by":"old-state"},{"action":"occupy","region":"island","by":"old-state"}]}]})");
 check(infeasible(far)&&far.changes.empty(),"Unreachable region rejected with its whole event");
 auto blitz=compile(R"({"events":[{"description":"Blitz","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"occupy","region":"march","by":"old-state"},{"action":"open_conflict","actors":["old-state","neighbor"],"text":"War"}]},
  {"description":"Second front","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"occupy","region":"hills","by":"old-state"}]},
  {"description":"Third front","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"occupy","region":"heart","by":"old-state"}]}]})");
 check(!infeasible(blitz,0)&&!infeasible(blitz,1)&&infeasible(blitz,2),"At most two regions taken per month");
 auto war=applyDelta(opsBase,blitz,geo);check(war.state.territories["march"]["controller"]=="old-state"&&war.state.territories["march"]["owner"]=="neighbor"&&war.state.conflicts.size()==1,"Occupation keeps ownership");
 auto annexNow=compile(R"({"events":[{"description":"Annex","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"transfer_territory","region":"march","to":"old-state"}]}]})");
 check(infeasible(annexNow)&&annexNow.changes.empty(),"Annexation needs prior occupation");
 auto nextMonth=resolve(war.state);
 auto annexLater=compileOperations(nextMonth.state,parse(R"({"events":[{"description":"Annex","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"transfer_territory","region":"march","to":"old-state"},{"action":"occupy","region":"heart","by":"old-state"}]}]})"),adjacency);
 auto annexed=applyDelta(nextMonth,annexLater,geo);check(annexed.state.territories["march"]["owner"]=="old-state"&&annexed.state.territories["heart"]["controller"]=="old-state","Annexation after a month of occupation, then further advance");
 auto grab=compile(R"({"events":[{"description":"Grab","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"transfer_territory","region":"west","to":"neighbor"}]}]})");
 check(infeasible(grab),"Cannot take land by declaration");
 // Armies advance one region per month; unfinished units cannot move.
 auto army=parse(R"({"name":"First Army","kind":"army","actor":"old-state","region":"west","status":"active","origin_event":"initial","longitude":1,"latitude":1})");
 auto armed=opsBase;armed.state.entities["army-1"]=army;
 auto march=[&](const char *region){return compileOperations(armed.state,parse(std::string(R"({"events":[{"description":"March","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"move_entity","entity":"army-1","region":")")+region+R"("}]}]})"),adjacency);};
 check(!infeasible(march("march"))&&infeasible(march("heart")),"Army moves one region per month");
 armed.state.entities["army-1"]["status"]="under_construction";armed.state.entities["army-1"]["completes_turn"]=3;check(infeasible(march("march")),"Unfinished army cannot move");
 auto rush=compileOperations(armed.state,parse(R"({"events":[{"description":"Rush","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"set_entity_status","entity":"army-1","status":"active"}]}]})"),adjacency);
 check(infeasible(rush),"Construction cannot be completed by decree");
 auto foreignBuild=compile(R"({"events":[{"description":"Base","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"build","kind":"base","name":"Forward base","actor":"old-state","region":"march"}]}]})");
 check(infeasible(foreignBuild),"Building requires control");
 auto twoStates=compile(R"({"events":[{"description":"Split","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"create_actor","actor_id":"a","name":"Alpha","color":"#00aa00"},{"action":"transfer_territory","region":"west","to":"Alpha"}]},
  {"description":"Split again","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"create_actor","actor_id":"b","name":"Beta","color":"#00aa00"},{"action":"transfer_territory","region":"east","to":"Beta"}]}]})");
 check(!infeasible(twoStates,0)&&infeasible(twoStates,1)&&applyDelta(opsBase,twoStates,geo).state.territories["west"]["owner"]=="alpha","One new state per month, referenced by name");
 auto regionNamed=compile(R"({"events":[{"description":"Free West","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"create_actor","actor_id":"wr","name":"West Republic","color":"#00aa00"},{"action":"transfer_territory","region":"west","to":"West"}]}]})");
 check(applyDelta(opsBase,regionNamed,geo).state.territories["west"]["owner"]=="west-republic","Recipient named after its region resolves to the new state");
 auto orphan=compile(R"({"events":[{"description":"Orphan","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"create_actor","actor_id":"z","name":"Zed","color":"#00aa00"}]}]})");
 check(infeasible(orphan)&&orphan.changes.empty(),"New state needs land");
 auto reform=compile(R"({"events":[{"description":"Education reform begins","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[]}]})");
 check(reform.changes.empty()&&reform.events.size()==1&&!reform.events[0].isMember("blocked_by")&&applyDelta(opsBase,reform,geo).events.size()==1,"Narrative-only event");
 auto treaty=opsBase;treaty.state.commitments["treaty-9"]=parse(R"({"actors":["old-state","neighbor"],"status":"open","text":"No new states"})");
 auto refused=ops;refused["events"][0]["blocked_by"]="treaty-9";refused["events"][0]["operations"]=Json::arrayValue;auto denied=compileOperations(treaty.state,refused,adjacency);check(denied.events[0]["blocked_by"]=="treaty-9"&&denied.changes.size()==1,"Commitment block");
 auto broken=ops;broken["events"][0]["blocked_by"]="treaty-9";auto breach=compileOperations(treaty.state,broken,adjacency);
 check(!breach.events[0].isMember("blocked_by")&&breach.events[0]["breaches"]=="treaty-9"&&breach.events[0]["text"].asString().find("No new states")!=std::string::npos&&breach.changes.size()==compiled.changes.size(),"Treaty breach recorded");
 rejects([&]{compile(R"({"events":[{"description":"Ghost","actor":"nobody","blocked_by":"none","background":false,"causes":[],"operations":[]}]})");});
 auto noisy=parse(R"({"events":[]})");for(int n=0;n<5;++n)noisy["events"].append(parse(R"({"description":"Rumor","actor":"neighbor","blocked_by":"none","background":true,"causes":[],"operations":[]})"));
 check(compileOperations(opsBase.state,noisy,adjacency).events.size()==3,"Background events capped");
 // Wars and treaties get historians' names, kinds and intensity; effects are validated and recorded on events.
 check(outcomes["prefixItems"][0]["required"].size()==7&&outcomes["prefixItems"][0]["properties"]["effects"]["maxItems"]==3,"Events carry effects");
 auto named=compile(R"({"events":[{"description":"War is declared","actor":"old-state","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"old-state","economy":"decline","stability":"growth"},{"actor":"ghost","economy":"decline","stability":"decline"},{"actor":"neighbor","economy":"none","stability":"none"}],"operations":[{"action":"occupy","region":"march","by":"old-state"},{"action":"open_conflict","actors":["old-state","neighbor"],"kind":"war","intensity":"high","name":"March War","text":"War over the March"},{"action":"add_commitment","actors":["neighbor","far"],"kind":"defence_pact","name":"Defence Pact","text":"Mutual defence"}]},
  {"description":"Another war","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"open_conflict","actors":["neighbor","old-state"],"kind":"skirmish","name":"March War","text":"Again"}]}]})");
 auto namedWorld=applyDelta(opsBase,named,geo);
 check(namedWorld.state.conflicts["c1-1"]["name"]=="March War"&&namedWorld.state.conflicts["c1-1"]["set_intensity"]=="high"&&namedWorld.state.conflicts["c1-1"]["started_turn"]==1,("Named war: "+dump(namedWorld.state.conflicts)).c_str());
 check(namedWorld.state.commitments["k1-2"]["name"]=="Neighbor–Far Defence Pact"&&namedWorld.state.commitments["k1-2"]["kind"]=="defence_pact","Generic treaty name replaced");
 check(namedWorld.state.conflicts["c1-3"]["name"]=="Second March War"&&namedWorld.state.conflicts["c1-3"]["kind"]=="war","Repeated name numbered, invalid kind defaulted");
 check(named.events[0]["effects"].size()==1&&named.events[0]["effects"][0]["actor"]=="old-state"&&!named.events[1].isMember("effects"),"Only valid effects recorded");
 auto intensity=[&](const char *actor){return compileOperations(namedWorld.state,parse(std::string(R"({"events":[{"description":"Offensive","actor":")")+actor+R"(","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"set_conflict_intensity","conflict":"c1-1","intensity":"low"}]}]})"),adjacency);};
 check(!infeasible(intensity("neighbor"))&&infeasible(intensity("far")),"Only parties change a war's intensity");
 auto ended=compileOperations(namedWorld.state,parse(R"({"events":[{"description":"Peace","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"resolve_conflict","conflict":"c1-1"}]}]})"),adjacency);
 auto peaceWorld=applyDelta(namedWorld,ended,geo);check(peaceWorld.state.conflicts["c1-1"]["ended_turn"]==1&&peaceWorld.state.conflicts["c1-1"]["intensity"]=="none","Ended wars record when");
 auto unnamed=d;unnamed.changes=Json::arrayValue;Json::Value bare;bare["kind"]="conflict";bare["id"]="c9";bare["before"]=Json::nullValue;bare["after"]=parse(R"({"actors":["old-state","neighbor"],"status":"open","text":"War"})");bare["event"]="t1-change";bare["rationale"]="x";unnamed.changes.append(bare);
 rejects([&]{applyDelta(base,unnamed,geo);});
 auto legacy=base;legacy.state.conflicts["c9"]=bare["after"];applyDelta(legacy,WorldDelta{},geo); // Entries from before names stay valid.
 std::cout<<"World deltas: occupation, secession, entity placement, holes, construction schedule, reach, pacing, annexation, conflicts, reload, replay, old history and atomic rollback passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
