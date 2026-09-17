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
    require(j["changes"].isArray()&&j["changes"].size()<=32&&j["events"].isArray()&&j["events"].size()<=12,"Delta exceeds bounds");
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
        if(kind=="resources") {
            require(w.countries.contains(id),"Unknown resource actor");
            auto old=encode(w)["countries"][id];require(c["before"]==old,"Resource precondition failed");
            auto replacement=c["after"];
            require(replacement.isObject()&&replacement.getMemberNames()==old.getMemberNames(),"Invalid resource shape");
            for(auto k:{"treasury","income","stability","army","navy"})require(replacement[k].isInt()&&replacement[k].asInt()>=0&&replacement[k].asInt()<=(std::string(k)=="stability"?100:std::string(k)=="treasury"?1000000:10000),"Resource out of bounds");
            require(replacement["posture"]==old["posture"]&&replacement["priority"]==old["priority"],"Use intentions to change priorities");
            auto encoded=encode(w);encoded["countries"][id]=replacement;w=decode(encoded);continue;
        }
        require(collection,"Unknown change kind");
        require((*collection)[id]==c["before"],"Change precondition failed");
        require(c["after"].isObject(),"Deletion is not allowed; update status instead");
        if(kind=="actor") {
            require(c["before"].isNull(),"Actor identity is immutable");
            require(c["after"]["name"].isString()&&!c["after"]["name"].asString().empty()&&c["after"]["name"].asString().size()<=120&&c["after"]["color"].isString()&&std::regex_match(c["after"]["color"].asString(),std::regex("#[0-9a-fA-F]{6}")),"Invalid actor identity");
            require(!w.countries.contains(id),"Actor already exists");
            w.countries[id]={0,0,0,0,0,"defensive","balanced"};
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
        for(auto k:{"owner","controller"})require(t[k].isString()&&(t[k]==""||w.countries.contains(t[k].asString())),"Unknown territory actor");
    }
    for(auto &id:w.entities.getMemberNames()) {
        auto &e=w.entities[id];
        if(e==base.state.entities[id])continue;
        require(e["actor"].isString()&&w.countries.contains(e["actor"].asString())&&e["region"].isString()&&w.territories.isMember(e["region"].asString()),"Invalid entity affiliation");
        require(e["name"].isString()&&e["name"].asString().size()<=120&&e["status"].isString()&&(e["status"]=="active"||e["status"]=="destroyed"||e["status"]=="inactive"),"Invalid entity description");
        require(e["kind"]=="city"||e["kind"]=="port"||e["kind"]=="base"||e["kind"]=="army"||e["kind"]=="event_site","Invalid entity kind");
        require(e["longitude"].isNumeric()&&e["latitude"].isNumeric()&&containsPoint(geo[e["region"].asString()],e["longitude"].asDouble(),e["latitude"].asDouble()),"Entity location outside region");
        require(e["origin_event"].isString()&&(eventIds.contains(e["origin_event"].asString())||base.state.entities[id]["origin_event"]==e["origin_event"]),"Invalid originating event");
    }
    // Secessions conserve every transferable resource across all actors. Normal adjudication may consume resources, never mint them.
    for(auto k:{"treasury","income","army","navy"}) {
        Json::Int64 before=0,after=0;auto a=encode(base.state)["countries"],b=encode(w)["countries"];
        for(auto &id:a.getMemberNames())before+=a[id][k].asInt();for(auto &id:b.getMemberNames())after+=b[id][k].asInt();
        require(after<=before,"Adjudication cannot duplicate or create resources");
    }
    for(auto &id:w.actors.getMemberNames())if(!base.state.actors.isMember(id)) {
        bool land=false;for(auto &t:w.territories)land=land||t["owner"]==id;require(land,"New actor requires territory allocation");
    }
    for(auto &e:d.events) {
        require(w.countries.contains(e["actor"].asString()),"Unknown event actor");
        for(auto &id:e["actors"])require(id.isString()&&w.countries.contains(id.asString()),"Unknown event related actor");
        for(auto &id:e["regions"])require(id.isString()&&w.territories.isMember(id.asString()),"Unknown event region");
        for(auto &id:e["entities"])require(id.isString()&&w.entities.isMember(id.asString()),"Unknown event entity");
        r.events.push_back({e["actor"].asString(),"development",e["id"].asString(),e["text"].asString()});
    }
    for(auto kind:{"conflicts","commitments"}) {
        auto collection=kind==std::string("conflicts")?w.conflicts:w.commitments;
        for(auto &id:collection.getMemberNames()) {
            auto &v=collection[id];require(v["actors"].isArray()&&v["actors"].size()>=2&&v["status"].isString()&&(v["status"]=="open"||v["status"]=="resolved")&&v["text"].isString()&&v["text"].asString().size()<=1000,"Invalid conflict or commitment");
            for(auto &a:v["actors"])require(a.isString()&&w.countries.contains(a.asString()),"Unknown commitment actor");
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
Json::Value operationSchema(const WorldState &w,const Json::Value &facts) {
    std::set<std::string> actors,regions,entities,conflicts,commitments,causes;
    if(facts["actors"].isObject())for(auto &a:facts["actors"].getMemberNames())if(w.countries.contains(a))actors.insert(a);
    if(facts["territories"].isObject())for(auto &id:facts["territories"].getMemberNames())if(w.territories.isMember(id)) {
        regions.insert(id);
        for(auto k:{"owner","controller"}){auto a=w.territories[id][k].asString();if(w.countries.contains(a))actors.insert(a);}
    }
    if(facts["entities"].isObject())for(auto &id:facts["entities"].getMemberNames())if(w.entities.isMember(id)) {
        entities.insert(id);auto a=w.entities[id]["actor"].asString();if(w.countries.contains(a))actors.insert(a);
    }
    if(actors.empty()&&w.countries.contains(w.player))actors.insert(w.player);
    for(auto &id:w.conflicts.getMemberNames())if(w.conflicts[id]["status"]=="open")conflicts.insert(id);
    for(auto &id:w.commitments.getMemberNames())if(w.commitments[id]["status"]=="open")commitments.insert(id);
    collectEventIds(facts,causes);
    auto string=[](int max){Json::Value j;j["type"]="string";j["minLength"]=1;j["maxLength"]=max;return j;};
    auto amount=[](int max){Json::Value j;j["type"]="integer";j["minimum"]=0;j["maximum"]=max;return j;};
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
    p=Json::objectValue;p["actor"]=actor;p["treasury"]=amount(100000);p["army"]=amount(10000);p["navy"]=amount(10000);op("spend",p);
    p=Json::objectValue;p["from"]=actor;p["to"]=actorId;p["treasury"]=amount(100000);p["income"]=amount(10000);p["army"]=amount(10000);p["navy"]=amount(10000);op("transfer_resources",p);
    p=Json::objectValue;p["actor"]=actor;p["change"]=parse(R"({"type":"integer","minimum":-20,"maximum":20})");op("adjust_stability",p);
    p=Json::objectValue;p["actors"]=parties;p["text"]=string(500);op("open_conflict",p);
    p=Json::objectValue;p["actors"]=parties;p["text"]=string(500);op("add_commitment",p);
    if(!regions.empty()) {
        p=Json::objectValue;p["kind"]=parse(R"({"type":"string","enum":["city","port","base","army","event_site"]})");p["name"]=string(120);p["actor"]=actor;p["region"]=region;op("build",p);
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
    for(auto k:{"actor","background","blocked_by","causes","description","operations"})event["required"].append(k);
    // Denials must cite a real reason, so a small model cannot invent prerequisites.
    std::set<std::string> blockers(commitments.begin(),commitments.end());
    event["properties"]["blocked_by"]=enumeration(blockers);event["properties"]["blocked_by"]["enum"].insert(0,"none");
    event["properties"]["actor"]=actorId;event["properties"]["description"]=string(600);event["properties"]["background"]["type"]="boolean";
    event["properties"]["causes"]=parse(R"({"type":"array","maxItems":4})");
    if(causes.empty())event["properties"]["causes"]["maxItems"]=0;else event["properties"]["causes"]["items"]=enumeration(causes);
    event["properties"]["operations"]=parse(R"({"type":"array","maxItems":5})");event["properties"]["operations"]["items"]["anyOf"]=ops;
    Json::Value schema;schema["type"]="object";schema["additionalProperties"]=false;schema["required"].append("events");
    schema["properties"]["events"]=parse(R"({"type":"array","maxItems":6})");schema["properties"]["events"]["items"]=event;
    return schema;
}
struct InsufficientResources : ValidationError { using ValidationError::ValidationError; };
WorldDelta compileOperations(const WorldState &w,const Json::Value &response) {
    require(response.isObject()&&response.getMemberNames()==std::vector<std::string>({"events"})&&response["events"].isArray()&&response["events"].size()<=6,"Invalid operation response");
    auto countries=encode(w)["countries"];
    Json::Value zero;{WorldState z;z.countries["x"]={0,0,0,0,0,"defensive","balanced"};zero=encode(z)["countries"]["x"];}
    struct Pending{std::string kind,id,event,rationale;Json::Value before,after;};
    std::vector<Pending> pending;std::map<std::string,size_t> index;
    auto touch=[&](const std::string &kind,const std::string &id,const Json::Value &before,const std::string &event,const std::string &rationale)->Json::Value& {
        auto key=kind+":"+id;
        if(!index.contains(key)){index[key]=pending.size();pending.push_back({kind,id,event,rationale,before,before});}
        else pending[index[key]].rationale=std::string(pending[index[key]].rationale+"; "+rationale).substr(0,1000);
        return pending[index[key]].after;
    };
    std::vector<std::string> resourceOrder;std::map<std::string,Json::Value> resources;std::map<std::string,std::pair<std::string,std::string>> resourceMeta;
    auto funds=[&](const std::string &a,const std::string &event,const std::string &why)->Json::Value& {
        if(!resources.contains(a)) {
            bool created=index.contains("actor:"+a);
            require(created||countries.isMember(a),"Unknown resource actor");
            resources[a]=created?zero:countries[a];resourceOrder.push_back(a);resourceMeta[a]={event,why};
        }
        return resources[a];
    };
    auto current=[&](const std::string &kind,const Json::Value &collection,const std::string &id)->Json::Value {
        auto key=kind+":"+id;return index.contains(key)?pending[index[key]].after:collection[id];
    };
    Json::Value events(Json::arrayValue);std::set<std::string> eventIds;int created=0;
    // New actors get IDs derived from their names; later references may use the requested ID or the name.
    std::map<std::string,std::string> aliases;
    auto normal=[](const std::string &v){std::string o;for(unsigned char ch:v)if(std::isalnum(ch))o+=char(std::tolower(ch));return o;};
    auto actorRef=[&](const std::string &v){
        if(countries.isMember(v))return v;
        if(aliases.contains(v))return aliases[v];
        if(aliases.contains("~"+normal(v)))return aliases["~"+normal(v)];
        return v;
    };
    for(auto &e:response["events"]) {
        require(e.isObject()&&e["description"].isString()&&e["actor"].isString()&&e["blocked_by"].isString()&&e["background"].isBool()&&e["causes"].isArray()&&e["operations"].isArray()&&e["operations"].size()<=8,"Invalid operation event");
        auto text=e["description"].asString();
        auto base="t"+std::to_string(w.turn)+"-"+slug(text,32);auto id=base;
        for(int n=2;!eventIds.insert(id).second;++n)id=base+"-"+std::to_string(n);
        std::set<std::string> actors,regions,entities;
        std::vector<Json::Value> seenOps;
        std::string blocked=e["blocked_by"].asString();
        // Affordability is checked here, not by the model: an unaffordable event is rolled back and marked blocked.
        auto snapshot=std::make_tuple(pending,index,resourceOrder,resources,resourceMeta,created);
        auto snapshotSets=std::make_tuple(actors,regions,entities);
        try {
        for(auto &o:e["operations"]) {
            if(blocked!="none")break;
            require(o.isObject()&&o["action"].isString(),"Invalid operation");
            // Small models sometimes loop; an exact repeat within one event is degenerate, not a second outcome.
            if(std::find(seenOps.begin(),seenOps.end(),o)!=seenOps.end())continue;
            seenOps.push_back(o);
            auto name=o["action"].asString();auto why=name+": "+text.substr(0,300);
            auto str=[&](const char *k){require(o[k].isString()&&!o[k].asString().empty(),"Invalid operation field");return o[k].asString();};
            auto num=[&](const char *k){return o.isMember(k)?(require(o[k].isInt(),"Invalid operation amount"),o[k].asInt()):0;};
            if(name=="create_actor") {
                auto requested=str("actor_id"),a=slug(str("name"),30);
                for(int n=2;countries.isMember(a)||index.contains("actor:"+a);++n)a=slug(str("name"),30)+"-"+std::to_string(n);
                if(!countries.isMember(requested))aliases[requested]=a;
                aliases["~"+normal(str("name"))]=a;aliases["~"+normal(requested)]=a;
                Json::Value identity;identity["name"]=str("name");identity["color"]=str("color");
                touch("actor",a,Json::nullValue,id,why)=identity;actors.insert(a);
            } else if(name=="spend"||name=="transfer_resources") {
                auto from=actorRef(str(name=="spend"?"actor":"from"));auto &source=funds(from,id,why);actors.insert(from);
                Json::Value *target=nullptr;
                if(name=="transfer_resources"){auto to=actorRef(str("to"));require(to!=from,"Invalid transfer");target=&funds(to,id,why);actors.insert(to);}
                for(auto k:{"treasury","income","army","navy"}) {
                    int n=num(k);require(n>=0,"Invalid operation amount");
                    if(source[k].asInt()<n)throw InsufficientResources("Insufficient resources");
                    source[k]=source[k].asInt()-n;if(target)(*target)[k]=(*target)[k].asInt()+n;
                }
            } else if(name=="adjust_stability") {
                auto a=actorRef(str("actor"));auto &r=funds(a,id,why);actors.insert(a);
                r["stability"]=std::clamp(r["stability"].asInt()+num("change"),0,100);
            } else if(name=="open_conflict"||name=="add_commitment") {
                require(o["actors"].isArray()&&o["actors"].size()>=2,"Invalid parties");
                auto kind=name=="open_conflict"?std::string("conflict"):std::string("commitment");
                auto cid=std::string(kind=="conflict"?"c":"k")+std::to_string(w.turn)+"-"+std::to_string(++created);
                Json::Value v;v["actors"]=Json::arrayValue;v["status"]="open";v["text"]=str("text");
                for(auto &a:o["actors"]){require(a.isString(),"Invalid parties");v["actors"].append(actorRef(a.asString()));actors.insert(actorRef(a.asString()));}
                touch(kind,cid,Json::nullValue,id,why)=v;
            } else if(name=="resolve_conflict"||name=="resolve_commitment") {
                auto kind=name=="resolve_conflict"?std::string("conflict"):std::string("commitment");
                auto cid=str(kind.c_str());auto &collection=kind=="conflict"?w.conflicts:w.commitments;
                require(collection.isMember(cid),"Unknown conflict or commitment");
                touch(kind,cid,collection[cid],id,why)["status"]="resolved";
            } else if(name=="build") {
                auto r=str("region");require(w.territories.isMember(r),"Unknown region");
                auto t=current("territory",w.territories,r);
                require(t["longitude"].isNumeric()&&t["latitude"].isNumeric(),"Region has no interior location");
                auto eid="e"+std::to_string(w.turn)+"-"+slug(str("name"),30);
                for(int n=2;w.entities.isMember(eid)||index.contains("entity:"+eid);++n)eid="e"+std::to_string(w.turn)+"-"+slug(str("name"),30)+"-"+std::to_string(n);
                Json::Value v;v["name"]=str("name");v["kind"]=str("kind");v["actor"]=actorRef(str("actor"));v["region"]=r;
                v["longitude"]=t["longitude"];v["latitude"]=t["latitude"];v["status"]="active";v["origin_event"]=id;
                touch("entity",eid,Json::nullValue,id,why)=v;actors.insert(v["actor"].asString());regions.insert(r);entities.insert(eid);
            } else if(name=="move_entity"||name=="set_entity_status") {
                auto eid=str("entity");require(w.entities.isMember(eid)||index.contains("entity:"+eid),"Unknown entity");
                auto &v=touch("entity",eid,w.entities.get(eid,Json::nullValue),id,why);
                if(name=="move_entity") {
                    auto r=str("region");require(w.territories.isMember(r),"Unknown region");auto t=current("territory",w.territories,r);
                    v["region"]=r;v["longitude"]=t["longitude"];v["latitude"]=t["latitude"];regions.insert(r);
                } else v["status"]=str("status");
                entities.insert(eid);actors.insert(v["actor"].asString());
            } else if(name=="transfer_territory"||name=="occupy"||name=="restore_control") {
                auto r=str("region");require(w.territories.isMember(r),"Unknown region");
                auto &t=touch("territory",r,w.territories[r],id,why);
                if(name=="transfer_territory"){t["owner"]=actorRef(str("to"));t["controller"]=t["owner"];}
                else if(name=="occupy")t["controller"]=actorRef(str("by"));
                else t["controller"]=t["owner"];
                regions.insert(r);for(auto k:{"owner","controller"})if(!t[k].asString().empty())actors.insert(t[k].asString());
            } else throw ValidationError("Unknown operation");
        }
        } catch(const InsufficientResources &) {
            std::tie(pending,index,resourceOrder,resources,resourceMeta,created)=snapshot;
            std::tie(actors,regions,entities)=snapshotSets;
            blocked="cannot_pay";text="Could not be carried out for lack of resources: "+text;
        }
        actors.insert(actorRef(e["actor"].asString()));
        Json::Value event;event["id"]=id;event["actor"]=actorRef(e["actor"].asString());if(blocked!="none")event["blocked_by"]=blocked;event["text"]=text;event["background"]=e["background"];event["causes"]=e["causes"];
        for(auto [key,values]:std::initializer_list<std::pair<const char*,std::set<std::string>*>>{{"actors",&actors},{"regions",&regions},{"entities",&entities}}) {
            event[key]=Json::arrayValue;for(auto &v:*values)event[key].append(v);
        }
        events.append(event);
    }
    WorldDelta d;d.events=events;
    // Actors first so later territory, entity and resource changes may reference them.
    for(auto kind:{"actor","territory","entity","conflict","commitment"})
        for(auto &c:pending)if(c.kind==kind&&c.before!=c.after) {
            Json::Value j;j["kind"]=c.kind;j["id"]=c.id;j["before"]=c.before;j["after"]=c.after;j["event"]=c.event;j["rationale"]=c.rationale;d.changes.append(j);
        }
    for(auto &a:resourceOrder) {
        bool created=index.contains("actor:"+a);auto before=created?zero:countries[a];
        if(before==resources[a])continue;
        Json::Value j;j["kind"]="resources";j["id"]=a;j["before"]=before;j["after"]=resources[a];j["event"]=resourceMeta[a].first;j["rationale"]=resourceMeta[a].second;d.changes.append(j);
    }
    return parseDelta(encode(d));
}
}
