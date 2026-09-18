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
 check(outcomes["minItems"].asInt()==2&&outcomes["maxItems"].asInt()==2&&!outcomes.isMember("items")&&outcomes["prefixItems"][0]["properties"]["action"]["const"]=="Build a port"&&actionOpening("Venezuela mobilizes the National Guard in Zulia and Lara")=="Venezuela mobilizes the National Guard in…"&&outcomes["prefixItems"][0]["properties"]["actor"]["const"]=="old-state"&&outcomes["prefixItems"][1]["properties"]["actor"]["const"]=="neighbor","One outcome per action");
 check(outcomes["prefixItems"][0]["properties"]["operations"]["items"]["anyOf"].size()==10,"Schema");
 auto &developments=schema["properties"]["world_events"];
 check(developments["maxItems"].asInt()==2&&!developments["items"]["properties"].isMember("operations")&&developments["items"]["properties"]["actor"]["enum"]==parse(R"(["far","neighbor"])"),"World events are narrative and never the player's decisions");
 auto split2=compileOperations(opsBase.state,parse(R"({"events":[{"action":"Build a port","actor":"old-state","blocked_by":"none","causes":[],"description":"Port begun","operations":[]}],"world_events":[{"actor":"neighbor","blocked_by":"none","causes":[],"description":"Harvest festival","operations":[],"effects":[{"actor":"neighbor","economy":"growth","stability":"none"}]},{"actor":"far","causes":[],"description":"Markets steady"}]})"),{});
 auto invented=compileOperations(opsBase.state,parse(R"({"events":[],"world_events":[{"actor":"old-state","causes":[],"description":"Old builds a fortress"},{"actor":"neighbor","causes":[],"description":"Neighbor takes West","operations":[{"action":"occupy","region":"west","by":"neighbor"}]}]})"),{});
 check(invented.events.empty()&&invented.changes.empty(),"Invented player decisions and map changes are dropped");
 check(split2.events.size()==2&&!split2.events[0]["background"].asBool()&&split2.events[1]["background"].asBool(),"Outcomes then world events");
 auto compile=[&](const char *json){return compileOperations(opsBase.state,parse(json),adjacency);};
 auto ops=parse(R"({"events":[{"description":"West Secedes!","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[
  {"action":"create_actor","actor_id":"west-rep","name":"West Republic","color":"#00aa00"},{"action":"transfer_territory","region":"west","to":"west-rep"},
  {"action":"build","kind":"port","name":"Harbor","actor":"old-state","region":"east"},{"action":"build","kind":"port","name":"Harbor","actor":"old-state","region":"east"},
  {"action":"open_conflict","actors":["old-state","west-rep"],"text":"Dispute"}]},{"description":"West secedes","actor":"neighbor","blocked_by":"none","background":true,"effects":[{"actor":"neighbor","economy":"none","stability":"decline"}],"causes":[],"operations":[{"action":"occupy","region":"west","by":"neighbor"}]}]})");
 auto compiled=compileOperations(opsBase.state,ops,adjacency);check(compiled.events[0]["id"]=="t1-west-secedes"&&compiled.events[1]["id"]=="t1-west-secedes-2","Event IDs");
 auto seceded=applyDelta(opsBase,compiled,geo);
 check(seceded.state.territories["west"]["owner"]=="west-republic"&&seceded.state.territories["west"]["controller"]=="neighbor","Secession and bordering occupation");
 check(seceded.state.entities["e1-harbor"]["origin_event"]=="t1-west-secedes"&&seceded.state.entities.size()==1&&seceded.state.conflicts.size()==1,"Deduplicated construction");
 check(seceded.state.entities["e1-harbor"]["status"]=="under_construction"&&seceded.state.entities["e1-harbor"]["completes_turn"]==1+constructionMonths("port"),"Construction takes months");
 auto later=resolve(seceded.state);for(int n=1;n<constructionMonths("port");++n)later=resolve(later.state);check(later.state.entities["e1-harbor"]["status"]=="active"&&later.events.size()==1,"Construction completes");
 // An impossible operation is dropped with its reason kept for the audit; the event text never mentions it.
 auto infeasible=[&](const WorldDelta &x,Json::ArrayIndex index=0){return !x.events[index]["dropped_operations"].empty()&&x.events[index]["text"].asString().find("Could not happen")==std::string::npos;};
 auto changed=[](const WorldDelta &x,const char *kind,const char *id){for(auto &c:x.changes)if(c["kind"]==kind&&c["id"]==id)return true;return false;};
 // Out of reach: Heart borders no land the aggressor controls.
 auto far=compile(R"({"events":[{"description":"Deep strike","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"occupy","region":"march","by":"old-state"},{"action":"occupy","region":"island","by":"old-state"}]}]})");
 check(infeasible(far)&&far.events[0]["text"]=="Deep strike"&&!far.events[0].isMember("blocked_by")&&changed(far,"territory","march")&&!changed(far,"territory","island"),"Unreachable region dropped, the rest of its event holds");
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
 auto seized=compileOperations(armed.state,parse(R"({"events":[{"description":"Neighbor mobilizes","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"neighbor","economy":"decline","stability":"decline"}],"operations":[{"action":"open_conflict","actors":["neighbor","old-state"],"kind":"war","name":"West War","text":"War"},{"action":"move_entity","entity":"army-1","region":"march"}]}]})"),adjacency);
 check(infeasible(seized)&&seized.events[0]["text"]=="Neighbor mobilizes"&&seized.events[0]["dropped_operations"][0]["action"]=="move_entity"&&changed(seized,"conflict","c1-1")&&!changed(seized,"entity","army-1")&&seized.events[0]["effects"].size()==2,"Valid operations survive a foreign unit order");
 auto effectOf=[](const WorldDelta &x,const char *actor,Json::ArrayIndex index=0){for(auto &f:x.events[index]["effects"])if(f["actor"]==actor)return f;return Json::Value();};
 check(effectOf(seized,"old-state")["economy"]=="decline"&&effectOf(seized,"old-state")["stability"]=="decline","The attacked side suffers even when the model left it out");
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
 check(infeasible(orphan)&&orphan.events[0]["blocked_by"]=="infeasible"&&orphan.changes.empty()&&applyDelta(opsBase,orphan,geo).events.empty(),"New state needs land; the rolled-back event stays out of the journal");
 auto reform=compile(R"({"events":[{"description":"Education reform begins","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[]}]})");
 check(reform.changes.empty()&&reform.events.size()==1&&!reform.events[0].isMember("blocked_by")&&applyDelta(opsBase,reform,geo).events.size()==1,"Narrative-only event");
 auto treaty=opsBase;treaty.state.commitments["treaty-9"]=parse(R"({"actors":["old-state","neighbor"],"status":"open","text":"No new states"})");
 auto refused=ops;refused["events"][0]["blocked_by"]="treaty-9";refused["events"][0]["operations"]=Json::arrayValue;auto denied=compileOperations(treaty.state,refused,adjacency);check(denied.events[0]["blocked_by"]=="treaty-9"&&denied.changes.size()==1,"Commitment block");
 auto broken=ops;broken["events"][0]["blocked_by"]="treaty-9";auto breach=compileOperations(treaty.state,broken,adjacency);
 check(!breach.events[0].isMember("blocked_by")&&breach.events[0]["breaches"]=="treaty-9"&&breach.events[0]["text"].asString().find("No new states")!=std::string::npos&&breach.changes.size()==compiled.changes.size(),"Treaty breach recorded");
 auto outsider=compileOperations(treaty.state,parse(R"({"events":[{"description":"Far builds","actor":"far","blocked_by":"treaty-9","background":false,"causes":[],"operations":[{"action":"build","kind":"port","name":"Far port","actor":"far","region":"island"}]}]})"),adjacency);
 check(!outsider.events[0].isMember("blocked_by")&&!outsider.events[0].isMember("breaches")&&outsider.events[0]["text"]=="Far builds","A treaty binds only its parties");
 auto futile=compileOperations(treaty.state,parse(R"({"events":[{"description":"Futile grab","actor":"neighbor","blocked_by":"treaty-9","background":false,"causes":[],"operations":[{"action":"transfer_territory","region":"west","to":"neighbor"}]}]})"),adjacency);
 check(!futile.events[0].isMember("breaches")&&futile.events[0]["text"]=="Futile grab","No breach when every operation was dropped");
 auto binding=operationSchema(treaty.state,encode(treaty.state),{{"far","Wait"},{"neighbor","Wait"}})["properties"]["events"]["prefixItems"];
 check(binding[0]["properties"]["blocked_by"]["enum"].size()==1&&binding[1]["properties"]["blocked_by"]["enum"][1]=="treaty-9","Only an actor's own commitments can block it");
 rejects([&]{compile(R"({"events":[{"description":"Ghost","actor":"nobody","blocked_by":"none","background":false,"causes":[],"operations":[]}]})");});
 auto noisy=parse(R"({"events":[]})");for(int n=0;n<5;++n)noisy["events"].append(parse(R"({"description":"Rumor","actor":"neighbor","blocked_by":"none","background":true,"causes":[],"operations":[],"effects":[{"actor":"neighbor","economy":"none","stability":"severe_decline"}]})"));
 check(compileOperations(opsBase.state,noisy,adjacency).events.size()==3,"Background events capped");
 // Wars and treaties get historians' names, kinds and intensity; effects are validated and recorded on events.
 check(outcomes["prefixItems"][0]["required"].size()==6&&!outcomes["prefixItems"][0]["properties"].isMember("significance")&&developments["items"]["properties"]["effects"]["minItems"]==1&&outcomes["prefixItems"][0]["properties"]["effects"]["maxItems"]==3,"Events carry effects");
 auto named=compile(R"({"events":[{"description":"War is declared","actor":"old-state","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"old-state","economy":"decline","stability":"growth"},{"actor":"ghost","economy":"decline","stability":"decline"},{"actor":"neighbor","economy":"none","stability":"none"}],"operations":[{"action":"occupy","region":"march","by":"old-state"},{"action":"open_conflict","actors":["old-state","neighbor"],"kind":"war","intensity":"high","name":"March War","text":"War over the March"},{"action":"add_commitment","actors":["neighbor","far"],"kind":"defence_pact","name":"Defence Pact","text":"Mutual defence"}]},
  {"description":"Another war","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"open_conflict","actors":["neighbor","old-state"],"kind":"skirmish","name":"March War","text":"Again"}]}]})");
 auto namedWorld=applyDelta(opsBase,named,geo);
 check(namedWorld.state.conflicts["c1-1"]["name"]=="March War"&&namedWorld.state.conflicts["c1-1"]["set_intensity"]=="high"&&namedWorld.state.conflicts["c1-1"]["started_turn"]==1,("Named war: "+dump(namedWorld.state.conflicts)).c_str());
 check(namedWorld.state.commitments["k1-2"]["name"]=="Neighbor–Far Defence Pact"&&namedWorld.state.commitments["k1-2"]["kind"]=="defence_pact","Generic treaty name replaced");
 check(namedWorld.state.conflicts["c1-3"]["name"]=="Second March War"&&namedWorld.state.conflicts["c1-3"]["kind"]=="war","Repeated name numbered, invalid kind defaulted");
 check(named.events[0]["effects"].size()==2&&effectOf(named,"ghost").isNull()&&effectOf(named,"old-state")["economy"]=="decline"&&effectOf(named,"old-state")["stability"]=="growth"&&effectOf(named,"neighbor")["economy"]=="decline"&&effectOf(named,"neighbor")["stability"]=="decline",("Only valid effects recorded; wars and seizures force the sign: "+dump(named.events[0]["effects"])).c_str());
 check(named.events[1]["effects"].size()==2&&effectOf(named,"neighbor",1)["economy"]=="decline"&&effectOf(named,"neighbor",1)["stability"]=="none"&&effectOf(named,"old-state",1)["stability"]=="decline","Opening a war costs both sides even without model effects");
 auto trade=compile(R"({"events":[{"description":"Trade accord","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"neighbor","economy":"decline","stability":"none"}],"operations":[{"action":"add_commitment","actors":["neighbor","far"],"kind":"trade_agreement","name":"Trade Accord","text":"Tariffs cut"}]},
  {"description":"Non-aggression","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"add_commitment","actors":["neighbor","far"],"kind":"non_aggression_pact","name":"Peace Pact","text":"No war"}]}]})");
 check(trade.events[0]["effects"].size()==2&&effectOf(trade,"neighbor")["economy"]=="growth"&&effectOf(trade,"far")["economy"]=="growth"&&effectOf(trade,"far")["stability"]=="none"&&!trade.events[1].isMember("effects"),("Trade agreements lift both economies; other treaties are left to the model: "+dump(trade.events[0]["effects"])).c_str());
 auto crowded=compile(R"({"events":[{"description":"Crowded war","actor":"old-state","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"far","economy":"growth","stability":"none"},{"actor":"old-state","economy":"strong_growth","stability":"growth"},{"actor":"neighbor","economy":"growth","stability":"growth"}],"operations":[{"action":"open_conflict","actors":["old-state","neighbor"],"kind":"war","intensity":"low","name":"Crowded War","text":"War"}]}]})");
 check(crowded.events[0]["effects"].size()==3&&effectOf(crowded,"old-state")["economy"]=="decline"&&effectOf(crowded,"old-state")["stability"]=="growth"&&effectOf(crowded,"neighbor")["economy"]=="decline"&&effectOf(crowded,"neighbor")["stability"]=="decline"&&effectOf(crowded,"far")["economy"]=="growth",("Forced effects cap the model's optimism: "+dump(crowded.events[0]["effects"])).c_str());
 auto escalated=compileOperations(namedWorld.state,parse(R"({"events":[{"description":"Offensive","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"neighbor","economy":"growth","stability":"growth"}],"operations":[{"action":"set_conflict_intensity","conflict":"c1-1","intensity":"high"}]}]})"),adjacency);
 auto skirmish=compile(R"({"events":[{"description":"Border raid","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"neighbor","economy":"growth","stability":"none"}],"operations":[{"action":"occupy","region":"west","by":"neighbor"},{"action":"open_conflict","actors":["neighbor","old-state"],"kind":"border_conflict","intensity":"high","name":"West Raid","text":"Raid"}]},
  {"description":"Quiet grab","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"effects":[{"actor":"neighbor","economy":"strong_growth","stability":"growth"}],"operations":[{"action":"occupy","region":"east","by":"neighbor"}]}]})");
 check(effectOf(skirmish,"neighbor")["economy"]=="decline"&&effectOf(skirmish,"neighbor")["stability"]=="none"&&effectOf(skirmish,"old-state")["economy"]=="decline"&&effectOf(skirmish,"old-state")["stability"]=="decline"&&effectOf(skirmish,"neighbor",1)["economy"]=="decline"&&effectOf(skirmish,"neighbor",1)["stability"]=="growth"&&effectOf(skirmish,"old-state",1)["stability"]=="decline",("A high-intensity conflict of any kind, or taking land by force, is never a windfall: "+dump(skirmish.events)).c_str());
 check(escalated.events[0]["effects"].size()==2&&effectOf(escalated,"neighbor")["economy"]=="decline"&&effectOf(escalated,"neighbor")["stability"]=="growth"&&effectOf(escalated,"old-state")["stability"]=="decline","Escalating to high costs every party");
 auto intensity=[&](const char *actor){return compileOperations(namedWorld.state,parse(std::string(R"({"events":[{"description":"Offensive","actor":")")+actor+R"(","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"set_conflict_intensity","conflict":"c1-1","intensity":"low"}]}]})"),adjacency);};
 check(!infeasible(intensity("neighbor"))&&infeasible(intensity("far")),"Only parties change a war's intensity");
 auto ended=compileOperations(namedWorld.state,parse(R"({"events":[{"description":"Peace","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"resolve_conflict","conflict":"c1-1"}]}]})"),adjacency);
 auto peaceWorld=applyDelta(namedWorld,ended,geo);check(peaceWorld.state.conflicts["c1-1"]["ended_turn"]==1&&peaceWorld.state.conflicts["c1-1"]["intensity"]=="none","Ended wars record when");
 auto unnamed=d;unnamed.changes=Json::arrayValue;Json::Value bare;bare["kind"]="conflict";bare["id"]="c9";bare["before"]=Json::nullValue;bare["after"]=parse(R"({"actors":["old-state","neighbor"],"status":"open","text":"War"})");bare["event"]="t1-change";bare["rationale"]="x";unnamed.changes.append(bare);
 rejects([&]{applyDelta(base,unnamed,geo);});
 auto legacy=base;legacy.state.conflicts["c9"]=bare["after"];applyDelta(legacy,WorldDelta{},geo); // Entries from before names stay valid.
 // Event sites appear at once and fade after a few months unless a later event involves them or cites their incident.
 auto site=compile(R"({"events":[{"description":"Flood strikes West","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"build","kind":"event_site","name":"West flood","actor":"old-state","region":"west"}]}]})");
 auto siteWorld=applyDelta(opsBase,site,geo);auto siteId=site.changes[0]["id"].asString();
 check(siteWorld.state.entities[siteId]["status"]=="active"&&siteWorld.state.entities[siteId]["expires_turn"]==1+eventMapMonths,"Event sites start active with an expiry");
 auto noExpiry=site;noExpiry.changes[0]["after"].removeMember("expires_turn");rejects([&]{applyDelta(opsBase,noExpiry,geo);});
 auto portExpiry=site;portExpiry.changes[0]["after"]["kind"]="port";rejects([&]{applyDelta(opsBase,portExpiry,geo);});
 auto aged=siteWorld.state;for(int n=0;n<eventMapMonths-1;++n)aged=resolve(aged).state;
 check(aged.entities[siteId]["status"]=="active","Event site lasts its months");
 auto faded=resolve(aged);check(faded.state.entities[siteId]["status"]=="inactive"&&!faded.state.entities[siteId].isMember("expires_turn")&&faded.events.empty(),"Event site fades quietly");
 auto cited=compileOperations(aged,parse(std::string(R"({"events":[{"description":"Flood relief","actor":"neighbor","blocked_by":"none","background":false,"causes":[")")+site.events[0]["id"].asString()+R"("],"operations":[]}]})"),adjacency);
 check(cited.changes.size()==1&&cited.changes[0]["after"]["expires_turn"]==aged.turn+eventMapMonths&&cited.events[0]["entities"][0]==siteId,"Citing an incident renews its site");
 auto touched=compileOperations(aged,parse(std::string(R"({"events":[{"description":"Memorial closed","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"set_entity_status","entity":")")+siteId+R"(","status":"inactive"}]}]})"),adjacency);
 check(touched.changes.size()==1&&touched.changes[0]["after"]["status"]=="inactive"&&!touched.changes[0]["after"].isMember("expires_turn"),"An inactive site loses its expiry");
 // Significance: map and war changes are major whatever the model says; a severe or strong effect declares a headline, capped at three a month; world events without an effect are dropped.
 check(split2.events.size()==2,"World developments without an effect are dropped");
 auto headlines=parse(R"({"events":[]})");
 for(auto text:{"Talks","Summit","Protest","Visit","Election"})headlines["events"].append(parse(std::string(R"({"description":")")+text+R"(","actor":"neighbor","blocked_by":"none","background":false,"causes":[],"operations":[],"effects":[{"actor":"neighbor","economy":"strong_growth","stability":"none"}]})"));
 headlines["events"][3]["effects"]=parse(R"([{"actor":"neighbor","economy":"decline","stability":"none"}])");
 headlines["events"][4]["effects"]=parse(R"([{"actor":"neighbor","economy":"severe_decline","stability":"severe_decline"}])");
 auto capped=compileOperations(opsBase.state,headlines,adjacency);int majors=0;for(auto &e:capped.events)majors+=e["major"].asBool();
 check(majors==3&&capped.events[4]["major"].asBool()&&capped.events[0]["major"].asBool()&&!capped.events[2]["major"].asBool()&&!capped.events[3]["major"].asBool(),"Strong effects declare headlines, capped weightiest first; mild ones stay routine");
 check(named.events[0]["major"].asBool()&&!reform.events[0]["major"].asBool()&&site.events[0]["major"].asBool()&&!orphan.events[0]["major"].asBool(),"Map and war changes are major");
 // Events link the wars and treaties they shape.
 check(named.events[0]["conflicts"]==parse(R"(["c1-1"])")&&named.events[0]["commitments"]==parse(R"(["k1-2"])")&&ended.events[0]["conflicts"]==parse(R"(["c1-1"])")&&reform.events[0]["conflicts"].empty(),"Events record their wars and treaties");
 {
  // The event log: categories, places, scope and what is still on the map.
  Store log(":memory:");auto start=opsBase.state;start.turn=0;log.create(start,false);
  auto world=parse(R"({"events":[{"description":"Flood strikes West","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"build","kind":"event_site","name":"West flood","actor":"old-state","region":"west"}]},{"description":"Island reform","actor":"far","blocked_by":"none","background":false,"causes":[],"operations":[]},{"description":"Orphan","actor":"old-state","blocked_by":"none","background":false,"causes":[],"operations":[{"action":"create_actor","actor_id":"z","name":"Zed","color":"#00aa00"}]}],"world_events":[{"actor":"neighbor","causes":[],"description":"Harvest festival","effects":[{"actor":"neighbor","economy":"severe_decline","stability":"none"}]}]})");
  auto first=resolve(start);auto delta=compileOperations(first.state,world,adjacency);auto month=applyDelta(first,delta,geo);
  Json::Value r;r["delta"]=encode(delta);log.commit(log.submit(0,"log-1","{}")["id"].asString(),month,{},r);
  auto current=month.state;
  auto all=log.eventLog(current,parse(R"({"scope":"all"})"))["events"];
  check(all.size()==3&&all[0]["category"]=="player"&&all[1]["category"]=="nation"&&all[2]["category"]=="world"&&all[0]["on_map"].asBool()&&all[0]["location"].size()==1,("Event log: "+dump(all)).c_str());
  check(!all[1]["location"].empty(),"Events without places sit in the actor's heartland");
  check(!all[1]["major"].asBool()&&!all[1]["on_map"].asBool()&&all[2]["major"].asBool()&&log.eventLog(current,parse(R"({"major":true})"))["events"].size()==2&&log.eventLog(current,parse(R"({"on_map":true})"))["events"].size()==2,"Only major events reach the map");
  check(log.eventLog(current,parse(R"({"scope":"actor","actor":"far"})"))["events"].size()==1&&log.eventLog(current,parse(R"({"categories":["world"]})"))["events"].size()==1&&log.eventLog(current,parse(R"({"q":"harvest"})"))["events"].size()==1,"Event log filters");
  for(int n=0;n<eventMapMonths;++n){auto next=resolve(current);log.commit(log.submit(current.turn,"log-quiet-"+std::to_string(n),"{}")["id"].asString(),next,{},{});current=next.state;}
  check(log.eventLog(current,parse(R"({"on_map":true})"))["events"].empty()&&log.eventLog(current,parse(R"({})"))["events"].size()==3,"Old events leave the map but stay in the log");
  check(log.campaignContext(current,"old-state").facts["related_history"].size()==1,"Rolled-back events stay out of history");
  auto paged=log.eventLog(current,parse(R"({"limit":1,"before_turn":1})"));check(paged["events"].empty()&&!paged["more"].asBool(),"Paging before the first month is empty");
 }
 std::cout<<"World deltas: occupation, secession, entity placement, holes, construction schedule, reach, pacing, annexation, conflicts, reload, replay, old history and atomic rollback passed\n";
}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
