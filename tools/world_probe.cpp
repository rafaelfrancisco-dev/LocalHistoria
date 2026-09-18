#include "localhistoria/providers.h"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace lh;
// Real-model scenarios on a four-region map: West and North (PT), East (ES, borders West) and Interior (ES, borders only East).
int main(int argc,char **argv){if(argc!=2){std::cerr<<"Usage: world_probe output.json\n";return 1;}Json::Value report(Json::arrayValue);LocalProvider provider("http://127.0.0.1:8081",600);
 Adjacency adjacency{{"region-west",{"region-east","region-north"}},{"region-north",{"region-west"}},{"region-east",{"region-west","region-interior"}},{"region-interior",{"region-east"}}};
 const std::vector<std::pair<std::string,std::pair<std::string,std::string>>> scenarios{
  {"construction",{"PT","Build a port in West."}},
  {"conflict",{"ES","Send the army across the border to occupy West."}},
  {"secession",{"PT","Grant West independence as a new state named West Republic."}},
  {"long_history",{"PT","Construct a military base in West."}},
  {"reform",{"PT","Launch a national education reform."}},
  {"overreach",{"PT","Conquer all of Spain this month."}},
  {"capital",{"PT","Build a new city in North to serve as the future capital."}},
  {"invasion",{"PT","Invade Spain."}},
  {"treaty_consent",{"ES","Sign a defence pact with Portugal."}}};
 for(auto &[name,intention]:scenarios){
  WorldState w;w.turn=80;w.player="PT";w.actors=parse(R"({"PT":{"name":"Portugal","color":"#338855"},"ES":{"name":"Spain","color":"#ddcc44"}})");
  w.territories=parse(R"({"region-west":{"name":"West","owner":"PT","controller":"PT","initial_affiliation":"PT","longitude":1,"latitude":1},"region-north":{"name":"North","owner":"PT","controller":"PT","initial_affiliation":"PT","longitude":1,"latitude":11},"region-east":{"name":"East","owner":"ES","controller":"ES","initial_affiliation":"ES","longitude":11,"latitude":1},"region-interior":{"name":"Interior","owner":"ES","controller":"ES","initial_affiliation":"ES","longitude":21,"latitude":1}})");
  Json::Value geo;
  geo["region-west"]=parse(R"({"type":"Polygon","coordinates":[[[0,0],[5,0],[5,5],[0,5],[0,0]]]})");geo["region-north"]=parse(R"({"type":"Polygon","coordinates":[[[0,10],[5,10],[5,15],[0,15],[0,10]]]})");
  geo["region-east"]=parse(R"({"type":"Polygon","coordinates":[[[10,0],[15,0],[15,5],[10,5],[10,0]]]})");geo["region-interior"]=parse(R"({"type":"Polygon","coordinates":[[[20,0],[25,0],[25,5],[20,5],[20,0]]]})");
  if(name=="long_history")w.commitments=parse(R"({"treaty-1":{"actors":["PT","ES"],"status":"open","text":"Treaty signed in turn 1 forbids construction of military bases in West until turn 120."}})");
  auto base=resolve(w);Json::Value input;input["facts"]["turn"]=base.state.turn;
  for(auto a:{"PT","ES"}){auto facts=context(base.state,a,{},{}).facts;facts.removeMember("territories");input["facts"]["actors"][a]=facts;}
  for(auto &id:base.state.territories.getMemberNames()){auto t=base.state.territories[id];for(auto k:{"longitude","latitude","initial_affiliation"})t.removeMember(k);t["within_reach_of"]=Json::arrayValue;for(auto a:{"PT","ES"})if(reaches(base.state,adjacency,a,id))t["within_reach_of"].append(a);input["facts"]["territories"][id]=t;}
  input["facts"]["entities"]=Json::objectValue;input["reference_only"]["available"]=false;
  Json::Value item;item["actor"]=intention.first;item["action"]=intention.second+attackGuidance(intention.first,intention.second,input["facts"]["territories"]);input["intentions"].append(item);
  Json::Value row;row["scenario"]=name;auto started=std::chrono::steady_clock::now();
  try{
   auto delta=provider.adjudicate(base.state,input,adjacency);auto result=applyDelta(base,delta,geo);auto &s=result.state;row["valid"]=true;row["delta"]=encode(delta);row["state"]=encode(s);
   // Validity only proves the output is legal; these check the outcome matches the intention and stays realistic.
   bool expected=false;
   auto has=[&](const char *kind,const char *region,const char *status){for(auto &e:s.entities)if(e["kind"]==kind&&e["region"]==region&&e["status"]==status)return true;return false;};
   if(name=="construction")expected=has("port","region-west","under_construction");
   if(name=="conflict")expected=s.territories["region-west"]["controller"]=="ES"&&s.territories["region-west"]["owner"]=="PT";
   if(name=="secession")expected=s.territories["region-west"]["owner"]!="PT"&&s.actors.size()==3;
   if(name=="long_history"){expected=true;for(auto &e:s.entities)expected=expected&&e["kind"]!="base";}
   if(name=="reform")expected=delta.changes.empty()&&!delta.events.empty();
   if(name=="overreach")expected=s.territories["region-interior"]["controller"]=="ES"&&s.territories["region-east"]["owner"]=="ES";
   if(name=="capital")expected=has("city","region-north","under_construction");
   if(name=="invasion"){int open=0;for(auto &id:s.conflicts.getMemberNames())open+=s.conflicts[id]["status"]=="open";expected=s.territories["region-east"]["controller"]=="PT"&&s.territories["region-east"]["owner"]=="ES"&&open==1;}
   // Until authority checks (plan 01) require Portugal's own outcome to mention Spain, a one-sided pact is accepted.
   if(name=="treaty_consent")expected=s.commitments.size()==1;
   row["expected_outcome"]=expected;
  }catch(const std::exception&e){row["valid"]=false;row["expected_outcome"]=false;row["error"]=e.what();}
  row["seconds"]=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
  auto records=provider.takeAudit();for(auto &r:records){r.removeMember("chat_template");r.removeMember("schema");}row["inference"]=records;report.append(row);std::ofstream(argv[1])<<dump(report);
  std::string events;for(auto &e:row["delta"]["events"])events+="\n    - "+e["text"].asString();
  std::cout<<name<<": valid="<<row["valid"].asBool()<<" expected="<<row["expected_outcome"].asBool()<<" seconds="<<int(row["seconds"].asDouble())<<" "<<row.get("error","").asString()<<events<<std::endl;
 }return 0;}
