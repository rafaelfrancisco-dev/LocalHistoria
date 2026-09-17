#include "localhistoria/world.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <regex>
namespace lh {
static void require(bool b, const char *message) { if (!b) throw ValidationError(message); }
static bool ring(const Json::Value &r,double x,double y) {
    bool inside=false;
    if(r.size()<4)return false;
    for(Json::ArrayIndex i=0,j=r.size()-1;i<r.size();j=i++) {
        double ax=r[i][0].asDouble(),ay=r[i][1].asDouble(),bx=r[j][0].asDouble(),by=r[j][1].asDouble();
        double cross=(x-ax)*(by-ay)-(y-ay)*(bx-ax);
        if(std::abs(cross)<1e-10 && x>=std::min(ax,bx) && x<=std::max(ax,bx) && y>=std::min(ay,by) && y<=std::max(ay,by))return true;
        if((ay>y)!=(by>y) && x<(bx-ax)*(y-ay)/(by-ay)+ax)inside=!inside;
    }
    return inside;
}
bool containsPoint(const Json::Value &g,double x,double y) {
    if(!std::isfinite(x)||!std::isfinite(y)||x < -180||x>180||y < -90||y>90)return false;
    auto polygon=[&](const Json::Value &p){if(p.empty()||!ring(p[0],x,y))return false;for(Json::ArrayIndex i=1;i<p.size();++i)if(ring(p[i],x,y))return false;return true;};
    if(g["type"]=="Polygon")return polygon(g["coordinates"]);
    if(g["type"]=="MultiPolygon")for(auto &p:g["coordinates"])if(polygon(p))return true;
    return false;
}
WorldDelta parseDelta(const Json::Value &j) {
    require(j.isObject() && j.getMemberNames()==std::vector<std::string>({"changes","events"}),"Invalid delta shape");
    require(j["changes"].isArray()&&j["changes"].size()<=120&&j["events"].isArray()&&j["events"].size()<=40,"Delta exceeds bounds");
    return {j["changes"],j["events"]};
}
Json::Value encode(const WorldDelta &d) {Json::Value j;j["changes"]=d.changes;j["events"]=d.events;return j;}
Resolution applyDelta(const Resolution &base,const WorldDelta &d,const Json::Value &geo) {
    parseDelta(encode(d));
    Resolution r=base;auto &w=r.state;std::set<std::string> seen,eventIds;int background=0;
    for(auto &e:d.events) {
        require(e.isObject()&&e["id"].isString()&&e["text"].isString()&&e["actor"].isString()&&e["causes"].isArray()&&e["regions"].isArray()&&e["entities"].isArray()&&e["actors"].isArray()&&e["background"].isBool(),"Invalid structured event");
        require(e["id"].asString().rfind("t"+std::to_string(w.turn)+"-",0)==0 && eventIds.insert(e["id"].asString()).second && e["text"].asString().size()<=2000,"Invalid event ID or text");
        if(e["background"].asBool())++background;
    }
    require(background<=3,"At most three background developments");
    for(auto &c:d.changes) {
        require(c.isObject()&&c.getMemberNames()==std::vector<std::string>({"after","before","event","id","kind","rationale"}),"Invalid change shape");
        require(c["kind"].isString()&&c["id"].isString()&&c["event"].isString()&&c["rationale"].isString()&&!c["rationale"].asString().empty()&&c["rationale"].asString().size()<=1000,"Invalid change metadata");
        auto id=c["id"].asString(),kind=c["kind"].asString();
        require(std::regex_match(id,std::regex("[A-Za-z0-9_-]{1,100}")),"Invalid permanent ID");
        require(seen.insert(kind+":"+id).second&&eventIds.contains(c["event"].asString()),"Conflicting change or unknown event");
        Json::Value *collection=nullptr;
        if(kind=="actor")collection=&w.actors;
        if(kind=="territory")collection=&w.territories;
        if(kind=="entity")collection=&w.entities;
        if(kind=="conflict")collection=&w.conflicts;
        if(kind=="commitment")collection=&w.commitments;
        require(collection,"Unknown change kind");
        require((*collection)[id]==c["before"],"Change precondition failed");
        require(c["after"].isObject(),"Deletion is not allowed; update status instead");
        if(kind=="actor") {
            require(c["before"].isNull(),"Actor identity is immutable");
            require(c["after"]["name"].isString()&&!c["after"]["name"].asString().empty()&&c["after"]["name"].asString().size()<=120&&c["after"]["color"].isString()&&std::regex_match(c["after"]["color"].asString(),std::regex("#[0-9a-fA-F]{6}")),"Invalid actor identity");
        }
        if(kind=="territory") {
            require(!c["before"].isNull(),"Territory geometry is fixed");
            auto expected=c["before"];expected["owner"]=c["after"]["owner"];expected["controller"]=c["after"]["controller"];
            require(expected==c["after"],"Only ownership and control can change");
        }
        (*collection)[id]=c["after"];
    }
    for(auto &id:w.territories.getMemberNames()) {
        auto &t=w.territories[id];
        for(auto k:{"owner","controller"})require(t[k].isString()&&(t[k]==""||w.hasActor(t[k].asString())),"Unknown territory actor");
    }
    for(auto &id:w.entities.getMemberNames()) {
        auto &e=w.entities[id];
        if(e==base.state.entities[id])continue;
        require(e["actor"].isString()&&w.hasActor(e["actor"].asString())&&e["region"].isString()&&w.territories.isMember(e["region"].asString()),"Invalid entity affiliation");
        require(e["name"].isString()&&e["name"].asString().size()<=120&&e["status"].isString()&&(e["status"]=="active"||e["status"]=="destroyed"||e["status"]=="inactive"||e["status"]=="under_construction"),"Invalid entity description");
        require(e["status"]=="under_construction"?e["completes_turn"].isInt()&&e["completes_turn"].asInt()>w.turn:!e.isMember("completes_turn"),"Invalid construction schedule");
        require(e["kind"]=="city"||e["kind"]=="port"||e["kind"]=="base"||e["kind"]=="army"||e["kind"]=="fleet"||e["kind"]=="event_site","Invalid entity kind");
        require(e["longitude"].isNumeric()&&e["latitude"].isNumeric()&&containsPoint(geo[e["region"].asString()],e["longitude"].asDouble(),e["latitude"].asDouble()),"Entity location outside region");
        require(e["origin_event"].isString()&&(eventIds.contains(e["origin_event"].asString())||base.state.entities[id]["origin_event"]==e["origin_event"]),"Invalid originating event");
    }
    for(auto &id:w.actors.getMemberNames())if(!base.state.actors.isMember(id)) {
        bool land=false;for(auto &t:w.territories)land=land||t["owner"]==id;require(land,"New actor requires territory allocation");
    }
    for(auto &e:d.events) {
        require(w.hasActor(e["actor"].asString()),"Unknown event actor");
        for(auto &id:e["actors"])require(id.isString()&&w.hasActor(id.asString()),"Unknown event related actor");
        for(auto &id:e["regions"])require(id.isString()&&w.territories.isMember(id.asString()),"Unknown event region");
        for(auto &id:e["entities"])require(id.isString()&&w.entities.isMember(id.asString()),"Unknown event entity");
        r.events.push_back({e["actor"].asString(),"development",e["id"].asString(),e["text"].asString()});
    }
    for(auto kind:{"conflicts","commitments"}) {
        auto collection=kind==std::string("conflicts")?w.conflicts:w.commitments;
        for(auto &id:collection.getMemberNames()) {
            auto &v=collection[id];require(v["actors"].isArray()&&v["actors"].size()>=2&&v["status"].isString()&&(v["status"]=="open"||v["status"]=="resolved")&&v["text"].isString()&&v["text"].asString().size()<=1000,"Invalid conflict or commitment");
            for(auto &a:v["actors"])require(a.isString()&&w.hasActor(a.asString()),"Unknown commitment actor");
        }
    }
    return r;
}
}
namespace lh {
static std::string slug(const std::string &text,size_t limit=40) {
    std::string out;
    for(unsigned char ch:text) {
        if(std::isalnum(ch))out+=char(std::tolower(ch));
        else if(!out.empty()&&out.back()!='-')out+='-';
        if(out.size()>=limit)break;
    }
    while(!out.empty()&&out.back()=='-')out.pop_back();
    return out.empty()?"event":out;
}
static void collectEventIds(const Json::Value &v,std::set<std::string> &ids) {
    if(v.isObject()) {
        if(v["related_history"].isArray())for(auto &e:v["related_history"])if(e["id"].isString())ids.insert(e["id"].asString());
        for(auto &k:v.getMemberNames())collectEventIds(v[k],ids);
    } else if(v.isArray())for(auto &x:v)collectEventIds(x,ids);
}
static Json::Value enumeration(const std::set<std::string> &values) {
    Json::Value j;j["type"]="string";j["enum"]=Json::arrayValue;
    for(auto &v:values)j["enum"].append(v);
    return j;
}
Json::Value operationSchema(const WorldState &w,const Json::Value &facts,const std::vector<std::pair<std::string,std::string>> &actions) {
    std::set<std::string> actors,regions,entities,conflicts,commitments,causes;
    if(facts["actors"].isObject())for(auto &a:facts["actors"].getMemberNames())if(w.hasActor(a))actors.insert(a);
    if(facts["territories"].isObject())for(auto &id:facts["territories"].getMemberNames())if(w.territories.isMember(id)) {
        regions.insert(id);
        for(auto k:{"owner","controller"}){auto a=w.territories[id][k].asString();if(w.hasActor(a))actors.insert(a);}
    }
    if(facts["entities"].isObject())for(auto &id:facts["entities"].getMemberNames())if(w.entities.isMember(id)) {
        entities.insert(id);auto a=w.entities[id]["actor"].asString();if(w.hasActor(a))actors.insert(a);
    }
    if(w.hasActor(w.player))actors.insert(w.player);
    for(auto &id:w.conflicts.getMemberNames())if(w.conflicts[id]["status"]=="open")conflicts.insert(id);
    for(auto &id:w.commitments.getMemberNames())if(w.commitments[id]["status"]=="open")commitments.insert(id);
    collectEventIds(facts,causes);
    auto string=[](int max){Json::Value j;j["type"]="string";j["minLength"]=1;j["maxLength"]=max;return j;};
    Json::Value actorId;actorId["type"]="string";actorId["pattern"]="^[A-Za-z0-9_-]{1,40}$";
    auto actor=enumeration(actors),region=enumeration(regions),entity=enumeration(entities);
    Json::Value ops(Json::arrayValue);
    auto op=[&](const char *name,Json::Value properties){
        Json::Value o;o["type"]="object";o["additionalProperties"]=false;o["required"]=Json::arrayValue;o["required"].append("action");
        for(auto &k:properties.getMemberNames())o["required"].append(k);
        properties["action"]["type"]="string";properties["action"]["enum"]=Json::arrayValue;properties["action"]["enum"].append(name);
        o["properties"]=properties;ops.append(o);
    };
    Json::Value p;
    auto parties=parse(R"({"type":"array","minItems":2,"maxItems":6})");parties["items"]=actorId;
    p=Json::objectValue;p["actor_id"]=actorId;p["name"]=string(120);p["color"]=parse(R"({"type":"string","pattern":"^#[0-9a-fA-F]{6}$"})");op("create_actor",p);
    p=Json::objectValue;p["actors"]=parties;p["text"]=string(500);op("open_conflict",p);
    p=Json::objectValue;p["actors"]=parties;p["text"]=string(500);op("add_commitment",p);
    if(!regions.empty()) {
        p=Json::objectValue;p["kind"]=parse(R"({"type":"string","enum":["city","port","base","army","fleet","event_site"]})");p["name"]=string(120);p["actor"]=actor;p["region"]=region;op("build",p);
        p=Json::objectValue;p["region"]=region;p["to"]=actorId;op("transfer_territory",p);
        p=Json::objectValue;p["region"]=region;p["by"]=actorId;op("occupy",p);
        p=Json::objectValue;p["region"]=region;op("restore_control",p);
    }
    if(!entities.empty()) {
        if(!regions.empty()){p=Json::objectValue;p["entity"]=entity;p["region"]=region;op("move_entity",p);}
        p=Json::objectValue;p["entity"]=entity;p["status"]=parse(R"({"type":"string","enum":["active","inactive","destroyed"]})");op("set_entity_status",p);
    }
    if(!conflicts.empty()){p=Json::objectValue;p["conflict"]=enumeration(conflicts);op("resolve_conflict",p);}
    if(!commitments.empty()){p=Json::objectValue;p["commitment"]=enumeration(commitments);op("resolve_commitment",p);}
    Json::Value event;event["type"]="object";event["additionalProperties"]=false;
    // jsoncpp sorts keys and llama.cpp grammars keep that order: description precedes operations, action leads each operation.
    for(auto k:{"actor","blocked_by","causes","description","operations"})event["required"].append(k);
    // Denials must cite a real commitment, so a small model cannot invent prerequisites; physical limits are checked by C++.
    event["properties"]["blocked_by"]=enumeration(commitments);event["properties"]["blocked_by"]["enum"].insert(0,"none");
    event["properties"]["actor"]=actor;event["properties"]["description"]=string(400);
    event["properties"]["causes"]=parse(R"({"type":"array","maxItems":4})");
    if(causes.empty())event["properties"]["causes"]["maxItems"]=0;else event["properties"]["causes"]["items"]=enumeration(causes);
    event["properties"]["operations"]=parse(R"({"type":"array","maxItems":5})");event["properties"]["operations"]["items"]["anyOf"]=ops;
    Json::Value schema;schema["type"]="object";schema["additionalProperties"]=false;schema["required"].append("events");schema["required"].append("world_events");
    // One outcome per action, in order, each opening with that action's exact text: a small model otherwise skips
    // this month's actions and re-adjudicates history instead.
    auto &outcomes=schema["properties"]["events"];outcomes["type"]="array";outcomes["prefixItems"]=Json::arrayValue;
    for(auto &[a,text]:std::vector(actions.begin(),actions.begin()+std::min<size_t>(actions.size(),37))) {
        auto item=event;item["required"].insert(0,"action");item["properties"]["action"]["const"]=text;item["properties"]["actor"]=Json::objectValue;item["properties"]["actor"]["const"]=a;
        outcomes["prefixItems"].append(item);
    }
    outcomes["minItems"]=outcomes["maxItems"]=Json::UInt(outcomes["prefixItems"].size());
    // World events are unplanned developments: narrative only, never decisions of the player's nation, so they cannot
    // invent construction, treaties or conquests that nobody ordered.
    auto development=event;development["required"]=Json::arrayValue;for(auto k:{"actor","causes","description"})development["required"].append(k);
    development["properties"].removeMember("operations");development["properties"].removeMember("blocked_by");
    std::set<std::string> others(actors);others.erase(w.player);if(others.empty())others=actors;development["properties"]["actor"]=enumeration(others);
    schema["properties"]["world_events"]["type"]="array";schema["properties"]["world_events"]["maxItems"]=2;schema["properties"]["world_events"]["items"]=development;
    return schema;
}
int constructionMonths(const std::string &kind) {
    static const std::map<std::string,int> months{{"army",2},{"fleet",4},{"base",3},{"port",6},{"city",24},{"event_site",0}};
    return months.contains(kind)?months.at(kind):0;
}
bool reaches(const WorldState &w,const Adjacency &adjacency,const std::string &a,const std::string &r) {
    // Reach is judged from the start of the month: controlled land (residual slivers excepted) or active armies and fleets.
    auto near=[&](const std::string &from){return from==r||(adjacency.contains(from)&&adjacency.at(from).contains(r));};
    for(auto &id:w.territories.getMemberNames()){auto &t=w.territories[id];if(t["controller"]==a&&t["kind"]!="residual"&&near(id))return true;}
    for(auto &id:w.entities.getMemberNames()){auto &e=w.entities[id];if(e["actor"]==a&&e["status"]=="active"&&(e["kind"]=="army"||e["kind"]=="fleet")&&near(e["region"].asString()))return true;}
    return false;
}
// A physically unreachable outcome this month: the event is kept as a narrative failure, the turn continues.
struct Infeasible : ValidationError { using ValidationError::ValidationError; };
static double kilometres(const Json::Value &a,const Json::Value &b) {
    const double rad=M_PI/180;double la1=a["latitude"].asDouble()*rad,la2=b["latitude"].asDouble()*rad,dl=(b["longitude"].asDouble()-a["longitude"].asDouble())*rad;
    return 6371*std::acos(std::clamp(std::sin(la1)*std::sin(la2)+std::cos(la1)*std::cos(la2)*std::cos(dl),-1.0,1.0));
}
WorldDelta compileOperations(const WorldState &w,const Json::Value &response,const Adjacency &adjacency) {
    require(response.isObject()&&response["events"].isArray()&&response["events"].size()<=37&&(!response.isMember("world_events")||(response["world_events"].isArray()&&response["world_events"].size()<=3)),"Invalid operation response");
    for(auto &k:response.getMemberNames())require(k=="events"||k=="world_events","Invalid operation response");
    // Outcomes of this month's actions first, then background developments.
    Json::Value all(Json::arrayValue);
    for(auto e:response["events"]){if(e.isObject()){e.removeMember("action");if(!e.isMember("background"))e["background"]=false;}all.append(e);}
    for(auto e:response["world_events"]){if(e.isObject()){if(e["actor"]==w.player||!e["operations"].empty())continue;e["background"]=true;e["blocked_by"]="none";e["operations"]=Json::arrayValue;}all.append(e);}
    struct Pending{std::string kind,id,event,rationale;Json::Value before,after;};
    std::vector<Pending> pending;std::map<std::string,size_t> index;
    auto touch=[&](const std::string &kind,const std::string &id,const Json::Value &before,const std::string &event,const std::string &rationale)->Json::Value& {
        auto key=kind+":"+id;
        if(!index.contains(key)){index[key]=pending.size();pending.push_back({kind,id,event,rationale,before,before});}
        else pending[index[key]].rationale=std::string(pending[index[key]].rationale+"; "+rationale).substr(0,1000);
        return pending[index[key]].after;
    };
    auto current=[&](const std::string &kind,const Json::Value &collection,const std::string &id)->Json::Value {
        auto key=kind+":"+id;return index.contains(key)?pending[index[key]].after:collection[id];
    };
    auto near=[&](const std::string &from,const std::string &to){return from==to||(adjacency.contains(from)&&adjacency.at(from).contains(to));};
    auto reaches=[&](const std::string &a,const std::string &r){return lh::reaches(w,adjacency,a,r);};
    Json::Value events(Json::arrayValue);std::set<std::string> eventIds;int created=0,newActors=0,background=0;
    std::map<std::string,std::set<std::string>> taken;
    // New actors get IDs derived from their names; later references may use the requested ID or the name.
    std::map<std::string,std::string> aliases;
    auto normal=[](const std::string &v){std::string o;for(unsigned char ch:v)if(std::isalnum(ch))o+=char(std::tolower(ch));return o;};
    auto actorRef=[&](const std::string &v){
        if(w.hasActor(v))return v;
        if(aliases.contains(v))return aliases[v];
        if(aliases.contains("~"+normal(v)))return aliases["~"+normal(v)];
        return v;
    };
    auto known=[&](const std::string &a){return w.hasActor(a)||index.contains("actor:"+a);};
    for(auto &e:all) {
        require(e.isObject()&&e["description"].isString()&&e["actor"].isString()&&e["blocked_by"].isString()&&e["background"].isBool()&&e["causes"].isArray()&&e["operations"].isArray()&&e["operations"].size()<=8,"Invalid operation event");
        auto ev=e["actor"].asString();require(w.hasActor(ev),"Unknown event actor");
        if(e["background"].asBool()&&++background>3)continue;
        auto text=e["description"].asString();
        auto base="t"+std::to_string(w.turn)+"-"+slug(text,32);auto id=base;
        for(int n=2;!eventIds.insert(id).second;++n)id=base+"-"+std::to_string(n);
        std::set<std::string> actors,regions,entities,createdHere;
        std::vector<Json::Value> seenOps;
        std::string blocked=e["blocked_by"].asString(),breaches;
        // Treaties can be broken: a cited commitment only blocks an event whose outcome changes nothing; otherwise the breach is recorded.
        if(blocked!="none"&&!e["operations"].empty()&&w.commitments.isMember(blocked)){breaches=blocked;blocked="none";}
        auto snapshot=std::make_tuple(pending,index,created,newActors,taken,aliases);
        try {
        for(auto &o:e["operations"]) {
            if(blocked!="none")break;
            require(o.isObject()&&o["action"].isString(),"Invalid operation");
            // Small models sometimes loop; an exact repeat within one event is degenerate, not a second outcome.
            if(std::find(seenOps.begin(),seenOps.end(),o)!=seenOps.end())continue;
            seenOps.push_back(o);
            auto name=o["action"].asString();auto why=name+": "+text.substr(0,300);
            auto str=[&](const char *k){if(!o[k].isString()||o[k].asString().empty())throw Infeasible("incomplete "+name);return o[k].asString();};
            auto region=[&]{auto r=str("region");if(!w.territories.isMember(r))throw Infeasible("unknown region");return r;};
            auto party=[&](const char *k){
                auto a=actorRef(str(k));
                // A small model often names the new state after its region; the state this event created is the only sensible recipient.
                if(!known(a)&&createdHere.size()==1)a=*createdHere.begin();
                if(!known(a))throw Infeasible("unknown actor "+a);return a;};
            auto take=[&](const std::string &a,const std::string &r){
                if(!reaches(a,r))throw Infeasible("no border, army or fleet within reach of "+w.territories[r]["name"].asString());
                taken[a].insert(r);if(taken[a].size()>2)throw Infeasible("at most two regions can change hands by force in one month");
            };
            if(name=="create_actor") {
                if(++newActors>1)throw Infeasible("only one new state can emerge per month");
                auto requested=str("actor_id"),a=slug(str("name"),30);
                for(int n=2;w.hasActor(a)||index.contains("actor:"+a);++n)a=slug(str("name"),30)+"-"+std::to_string(n);
                if(!w.hasActor(requested))aliases[requested]=a;
                aliases["~"+normal(str("name"))]=a;aliases["~"+normal(requested)]=a;
                Json::Value identity;identity["name"]=str("name");identity["color"]=str("color");
                touch("actor",a,Json::nullValue,id,why)=identity;actors.insert(a);createdHere.insert(a);
            } else if(name=="open_conflict"||name=="add_commitment") {
                if(!o["actors"].isArray()||o["actors"].size()<2)throw Infeasible("needs two parties");
                auto kind=name=="open_conflict"?std::string("conflict"):std::string("commitment");
                auto cid=std::string(kind=="conflict"?"c":"k")+std::to_string(w.turn)+"-"+std::to_string(++created);
                Json::Value v;v["actors"]=Json::arrayValue;v["status"]="open";v["text"]=str("text");
                for(auto &a:o["actors"]){if(!a.isString())throw Infeasible("unknown party");auto ref=actorRef(a.asString());if(!known(ref))throw Infeasible("unknown party "+ref);v["actors"].append(ref);actors.insert(ref);}
                touch(kind,cid,Json::nullValue,id,why)=v;
            } else if(name=="resolve_conflict"||name=="resolve_commitment") {
                auto kind=name=="resolve_conflict"?std::string("conflict"):std::string("commitment");
                auto cid=str(kind.c_str());auto &collection=kind=="conflict"?w.conflicts:w.commitments;
                if(!collection.isMember(cid))throw Infeasible("unknown "+kind);
                touch(kind,cid,collection[cid],id,why)["status"]="resolved";
            } else if(name=="build") {
                auto r=region();auto builder=party("actor");auto kind=str("kind");
                auto t=current("territory",w.territories,r);
                if(kind!="event_site"&&w.territories[r]["controller"]!=builder)throw Infeasible("can only build in regions it controls, not "+w.territories[r]["name"].asString());
                require(t["longitude"].isNumeric()&&t["latitude"].isNumeric(),"Region has no interior location");
                auto eid="e"+std::to_string(w.turn)+"-"+slug(str("name"),30);
                for(int n=2;w.entities.isMember(eid)||index.contains("entity:"+eid);++n)eid="e"+std::to_string(w.turn)+"-"+slug(str("name"),30)+"-"+std::to_string(n);
                Json::Value v;v["name"]=str("name");v["kind"]=kind;v["actor"]=builder;v["region"]=r;
                v["longitude"]=t["longitude"];v["latitude"]=t["latitude"];v["origin_event"]=id;
                // Construction takes months; the finished entity appears when resolve() reaches completes_turn.
                if(int months=constructionMonths(kind)){v["status"]="under_construction";v["completes_turn"]=w.turn+months;}else v["status"]="active";
                touch("entity",eid,Json::nullValue,id,why)=v;actors.insert(builder);regions.insert(r);entities.insert(eid);
            } else if(name=="move_entity"||name=="set_entity_status") {
                auto eid=str("entity");if(!w.entities.isMember(eid))throw Infeasible("unknown or unfinished entity");
                auto &v=touch("entity",eid,w.entities[eid],id,why);
                auto owner=w.entities[eid]["actor"].asString();
                if(name=="move_entity") {
                    auto r=region();auto from=w.entities[eid]["region"].asString();auto kind=w.entities[eid]["kind"].asString();
                    if(ev!=owner)throw Infeasible("only its owner can move "+w.entities[eid]["name"].asString());
                    if(w.entities[eid]["status"]!="active")throw Infeasible(w.entities[eid]["name"].asString()+" is not ready");
                    if(kind=="army"){if(!near(from,r))throw Infeasible("an army advances at most one region per month");}
                    else if(kind=="fleet"){if(kilometres(w.territories[from],w.territories[r])>1500)throw Infeasible("a fleet sails at most 1,500 km per month");}
                    else throw Infeasible("a "+kind+" cannot be relocated");
                    auto t=current("territory",w.territories,r);
                    v["region"]=r;v["longitude"]=t["longitude"];v["latitude"]=t["latitude"];regions.insert(r);
                } else {
                    auto status=str("status");
                    if(w.entities[eid]["status"]=="under_construction"&&status=="active")throw Infeasible("construction is still under way");
                    if(w.entities[eid]["status"]=="destroyed"&&status!="destroyed")throw Infeasible("a destroyed entity cannot be restored");
                    if(ev!=owner&&(status=="active"||!reaches(ev,w.entities[eid]["region"].asString())))throw Infeasible("cannot reach "+w.entities[eid]["name"].asString());
                    v["status"]=status;if(status!="under_construction")v.removeMember("completes_turn");
                }
                entities.insert(eid);actors.insert(owner);
            } else if(name=="transfer_territory"||name=="occupy"||name=="restore_control") {
                auto r=region();auto owner=w.territories[r]["owner"].asString(),controller=w.territories[r]["controller"].asString();
                auto &t=touch("territory",r,w.territories[r],id,why);
                if(name=="transfer_territory") {
                    auto to=party("to");
                    // Owners may cede; others must already have held the region before this month to annex it.
                    bool cession=ev==owner,annexation=controller==ev&&(to==ev||createdHere.contains(to)),returned=controller==ev&&to==owner;
                    if(!cession&&!annexation&&!returned)throw Infeasible(controller==ev?"cannot hand over land it does not own":"must occupy "+w.territories[r]["name"].asString()+" for at least a month before annexing it");
                    t["owner"]=to;t["controller"]=to;
                } else if(name=="occupy") {
                    auto by=party("by");
                    if(by!=ev&&!createdHere.contains(by))throw Infeasible("an occupation must be carried out by the acting nation");
                    if(controller!=by)take(ev,r);
                    t["controller"]=by;
                } else {
                    if(ev==owner&&controller!=owner)take(ev,r);
                    else if(ev!=controller)throw Infeasible("only the owner or the occupier can change control");
                    t["controller"]=t["owner"];
                }
                regions.insert(r);for(auto k:{"owner","controller"})if(!t[k].asString().empty())actors.insert(t[k].asString());
            } else throw Infeasible("unknown operation "+name);
        }
        for(auto &a:createdHere){bool land=false;for(auto &c:pending)land=land||(c.kind=="territory"&&c.after["owner"]==a);if(!land)throw Infeasible("a new state needs territory");}
        } catch(const Infeasible &x) {
            std::tie(pending,index,created,newActors,taken,aliases)=snapshot;
            actors.clear();regions.clear();entities.clear();
            blocked="infeasible";text="Could not happen this month ("+std::string(x.what())+"): "+text;
        }
        actors.insert(ev);
        if(!breaches.empty()&&blocked=="none")text+=" This breached the commitment: "+w.commitments[breaches]["text"].asString();
        Json::Value event;event["id"]=id;event["actor"]=ev;if(blocked!="none")event["blocked_by"]=blocked;if(!breaches.empty()&&blocked=="none")event["breaches"]=breaches;event["text"]=text.substr(0,2000);event["background"]=e["background"];event["causes"]=e["causes"];
        for(auto [key,values]:std::initializer_list<std::pair<const char*,std::set<std::string>*>>{{"actors",&actors},{"regions",&regions},{"entities",&entities}}) {
            event[key]=Json::arrayValue;for(auto &v:*values)event[key].append(v);
        }
        events.append(event);
    }
    WorldDelta d;d.events=events;
    // Actors first so later territory and entity changes may reference them.
    for(auto kind:{"actor","territory","entity","conflict","commitment"})
        for(auto &c:pending)if(c.kind==kind&&c.before!=c.after) {
            Json::Value j;j["kind"]=c.kind;j["id"]=c.id;j["before"]=c.before;j["after"]=c.after;j["event"]=c.event;j["rationale"]=c.rationale;d.changes.append(j);
        }
    return parseDelta(encode(d));
}
}
