#include "localhistoria/providers.h"
#include <fstream>
#include <iostream>
using namespace lh;
int main(int argc,char **argv){if(argc!=2){std::cerr<<"Usage: world_probe output.json\n";return 1;}Json::Value report(Json::arrayValue);LocalProvider provider("http://127.0.0.1:8081",60);
 for(auto scenario:{"construction","conflict","secession","long_history"}){
  WorldState w;w.turn=80;w.player="PT";w.countries["PT"]={};w.countries["ES"]={};w.actors=parse(R"({"PT":{"name":"Portugal","color":"#338855"},"ES":{"name":"Spain","color":"#ddcc44"}})");
  w.territories=parse(R"({"region-west":{"name":"West","owner":"PT","controller":"PT","initial_affiliation":"PT","longitude":1,"latitude":1},"region-east":{"name":"East","owner":"ES","controller":"ES","initial_affiliation":"ES","longitude":11,"latitude":1}})");
  Json::Value geo;geo["region-west"]=parse(R"({"type":"Polygon","coordinates":[[[0,0],[5,0],[5,5],[0,5],[0,0]]]})");geo["region-east"]=parse(R"({"type":"Polygon","coordinates":[[[10,0],[15,0],[15,5],[10,5],[10,0]]]})");
  if(std::string(scenario)=="long_history")w.commitments=parse(R"({"treaty-1":{"actors":["PT","ES"],"status":"open","text":"Treaty signed in turn 1 forbids construction of military bases in region-west until turn 120."}})");
  auto base=resolve(w,{});Json::Value input;input["facts"]=encode(base.state);input["reference_only"]["available"]=false;
  std::string name=scenario;
  if(name=="construction")input["intentions"]["PT"]="Construct a port in region-west, funded from the PT treasury.";
  if(name=="conflict")input["intentions"]["ES"]="Send the army to occupy region-west. A limited military occupation; no annexation.";
  if(name=="secession")input["intentions"]["PT"]="Grant independence to region-west as a new state named West Republic (actor id west-republic), giving it 30 treasury and 3 income.";
  if(name=="long_history")input["intentions"]["PT"]="Construct a military base in region-west.";
  Json::Value row;row["scenario"]=scenario;
  try{
   auto delta=provider.adjudicate(base.state,input);auto result=applyDelta(base,delta,geo);auto &s=result.state;row["valid"]=true;row["delta"]=encode(delta);row["state"]=encode(s);
   // Validity only proves the output is legal; these check the outcome matches the intention.
   bool expected=false;
   if(name=="construction")for(auto &e:s.entities)expected=expected||(e["kind"]=="port"&&e["region"]=="region-west"&&s.countries.at("PT").treasury<base.state.countries.at("PT").treasury);
   if(name=="conflict")expected=s.territories["region-west"]["controller"]=="ES"&&s.territories["region-west"]["owner"]=="PT";
   if(name=="secession")expected=s.countries.contains("west-republic")&&s.territories["region-west"]["owner"]=="west-republic"&&s.countries.at("PT").treasury+s.countries.at("west-republic").treasury==base.state.countries.at("PT").treasury;
   if(name=="long_history"){expected=true;for(auto &e:s.entities)expected=expected&&e["kind"]!="base";}
   row["expected_outcome"]=expected;
  }catch(const std::exception&e){row["valid"]=false;row["expected_outcome"]=false;row["error"]=e.what();}
  auto records=provider.takeAudit();for(auto &r:records){r.removeMember("chat_template");r.removeMember("schema");}row["inference"]=records;report.append(row);std::ofstream(argv[1])<<dump(report);std::cout<<scenario<<": valid="<<row["valid"].asBool()<<" expected="<<row["expected_outcome"].asBool()<<" "<<row.get("error","").asString()<<std::endl;
 }return 0;}
