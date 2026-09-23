#include "status/monthly/monthly_status.h"
#include <algorithm>
#include <cmath>
namespace lh::detail {
namespace {
bool tradeKind(const Json::Value &k) {
    return k["kind"] == "trade_agreement" || k["kind"] == "union";
}
// The turn an entity was ordered, from its ID ("e<turn>-…"); seeded entities count as turn 0.
int orderedTurn(const std::string &id) {
    if (id.size() < 2 || id[0] != 'e' || !std::isdigit(static_cast<unsigned char>(id[1])))
        return 0;
    return std::atoi(id.c_str() + 1);
}
double costOf(const std::string &kind) {
    static const std::map<std::string, int> index{
        {"army", 0}, {"fleet", 1}, {"base", 2}, {"port", 3}, {"city", 4}};
    auto at = index.find(kind);
    return at == index.end() ? 0 : rate::projectCost[at->second];
}
struct Order {
    std::string kind, units;
    const char *field;
    double unitCost;
    int months;
    bool slot;
};
const Order orders[] = {
    {"personnel", "recruits", "active", rate::recruitCost, rate::recruitMonths, false},
    {"aircraft", "aircraft", "aircraft", rate::aircraftCost, rate::aircraftMonths, true},
    {"ships", "warships", "ships", rate::shipCost, rate::shipMonths, true}};
} // namespace

// Every nation's effective output in one pass over the map: the same shares as effectiveOutput,
// which walks every region per actor.
std::map<std::string, double> MonthlyStatus::outputs() const {
    std::map<std::string, double> out;
    for (auto it = w.territories.begin(); it != w.territories.end(); ++it) {
        const auto &t = *it;
        auto owner = t["owner"].asString(), controller = t["controller"].asString();
        double gdp = number(t, "gdp_usd");
        if (!owner.empty())
            out[owner] += outputShare(w, it.name(), owner) * gdp;
        if (!controller.empty() && controller != owner)
            out[controller] += outputShare(w, it.name(), controller) * gdp;
    }
    return out;
}
// A nation's trade partners: everyone it shares an open trade agreement or union with, once however
// many treaties link them, so a renamed copy of a treaty adds nothing.
std::set<std::string> MonthlyStatus::tradePartners(const std::string &a) const {
    std::set<std::string> partners;
    for (const auto &k : std::as_const(w.commitments))
        if (isOpen(k) && tradeKind(k) && isParty(k, a))
            for (const auto &p : k["actors"])
                if (p != a && cActors[p.asString()]["status"].isObject())
                    partners.insert(p.asString());
    return partners;
}
// Trade partners gained since the first realism month raise growth, and partners lost lower it,
// each weighted by its share of the pair's combined output: a microstate adds almost nothing, a
// large market almost one. Seeded growth already includes the trade of 2026, so an unchanged set of
// partners changes nothing, and expiry, withdrawal or breach all end the same future benefit.
void MonthlyStatus::tradeStep() {
    std::map<std::string, double> output;
    bool measured = false;
    for (const auto &a : w.actors.getMemberNames()) {
        if (!cActors[a]["status"].isObject())
            continue;
        auto &s = w.actors[a]["status"];
        auto partners = tradePartners(a);
        if (!s.isMember("trade_reference")) {
            s["trade_reference"] = Json::arrayValue;
            for (const auto &p : partners)
                s["trade_reference"].append(p);
        }
        std::set<std::string> reference;
        for (const auto &p : std::as_const(s)["trade_reference"])
            reference.insert(p.asString());
        if (partners == reference) {
            s.removeMember("trade_weight");
            continue;
        }
        if (!measured) {
            output = outputs();
            measured = true;
        }
        double own = output[a], gained = 0, lost = 0;
        auto weight = [&](const std::string &p) {
            return own + output[p] > 0 ? output[p] / (own + output[p]) : 0.0;
        };
        for (const auto &p : partners)
            if (!reference.contains(p))
                gained += weight(p);
        for (const auto &p : reference)
            if (!partners.contains(p))
                lost += weight(p);
        double change = std::min(gained, rate::tradeCap) - std::min(lost, rate::tradeCap);
        s["trade_weight"] = std::round(change * 1000) / 1000;
        double yearly = rate::tradeGrowth * s["trade_weight"].asDouble();
        if (yearly == 0)
            continue;
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a && cT[id].isMember("gdp_usd"))
                T[id]["gdp_usd"] =
                    Int(std::llround(double(integer(cT[id], "gdp_usd")) * (1 + yearly / 1200)));
    }
}
void MonthlyStatus::allowances() {
    effective = outputs();
    for (const auto &a : w.actors.getMemberNames()) {
        const auto &s = cActors[a]["status"];
        if (!s.isObject())
            continue;
        double envelope = rate::implementationShare * effective[a];
        double claim = rate::militaryClaim * number(s, "military_spend_usd");
        allowance[a] = budget[a] = std::max(rate::allowanceFloor * envelope, envelope - claim) / 12;
    }
}
void MonthlyStatus::implementation() {
    for (const auto &a : w.actors.getMemberNames())
        if (cActors[a]["status"].isObject()) {
            fund(a);
            recover(a);
        }
}
// Fixed priority: work already under way, oldest first, then this month's new work in recorded
// order, and replacement orders last. What the allowance cannot cover waits a month: a project's
// completion moves back, an effort or order makes no progress. Nothing is borrowed.
void MonthlyStatus::fund(const std::string &a) {
    auto &s = w.actors[a]["status"];
    const auto &cs = cActors[a]["status"];
    bool armed = cs["military"].isObject();
    // The establishment replacement works toward: the inventory at the first realism month.
    if (armed && !cs["military"].isMember("establishment"))
        for (auto field : {"active", "aircraft", "ships"})
            s["military"]["establishment"][field] = integer(cs["military"], field);
    // Efforts the month's events started. Equivalent requests continue the one already open.
    for (const auto &e : delta.events)
        if (e["actor"] == a && e["effort"].isString()) {
            auto category = e["effort"].asString();
            if (cs["efforts"].isMember(category))
                continue;
            bool investment = category == "investment";
            int months = investment ? rate::investmentMonths : rate::reformMonths;
            Json::Value effort;
            effort["started"] = w.turn;
            effort["months"] = months;
            effort["paid"] = 0;
            effort["monthly_usd"] = Int(std::llround(
                (investment ? rate::investmentCost : rate::reformCost) * effective[a] / months));
            s["efforts"][category] = effort;
        }
    // Replacement orders for losses below the establishment, one open order of each kind. Aircraft
    // and ships also need a free construction slot.
    if (armed) {
        int used = 0;
        for (const auto &e : std::as_const(w.entities))
            if (e["actor"] == a && e["status"] == "under_construction" && e["kind"] != "event_site")
                ++used;
        for (const auto &o : orders)
            if (o.slot && cs["procurement"].isMember(o.kind))
                ++used;
        for (const auto &o : orders) {
            const auto &est = cs["military"]["establishment"];
            Int deficit = integer(est, o.field) - integer(cs["military"], o.field);
            if (deficit <= 0 || cs["procurement"].isMember(o.kind) ||
                (o.slot && used >= force::constructionCapacity(w, a)))
                continue;
            Int count =
                std::min(deficit, std::max<Int>(1, Int(std::llround(double(integer(est, o.field)) *
                                                                    rate::replacementBatch))));
            Json::Value order;
            order["count"] = count;
            order["started"] = w.turn;
            order["months"] = o.months;
            order["paid"] = 0;
            order["monthly_usd"] = Int(std::llround(double(count) * o.unitCost / o.months));
            s["procurement"][o.kind] = order;
            used += o.slot;
        }
    }
    // Everything the allowance is asked to pay for, in priority order.
    struct Claim {
        int group, started;
        std::string kind, id;
        double cost;
    };
    std::vector<Claim> claims;
    // Building costs local prices: the same wealth factor that sets force quality, 0.6 to 1.4.
    double prices = 0;
    for (const auto &id : w.entities.getMemberNames()) {
        const auto &e = std::as_const(w.entities)[id];
        if (e["actor"] != a || e["status"] != "under_construction" || e["paused"].isObject() ||
            !e["completes_turn"].isInt())
            continue;
        int started = orderedTurn(id);
        if (prices == 0)
            prices = force::quality(w, a);
        claims.push_back({started < w.turn ? 0 : 1, started, "entity", id,
                          costOf(e["kind"].asString()) * prices});
    }
    for (const auto &category : cs["efforts"].getMemberNames()) {
        const auto &effort = cs["efforts"][category];
        int started = effort["started"].asInt();
        claims.push_back(
            {started < w.turn ? 0 : 1, started, "effort", category, number(effort, "monthly_usd")});
    }
    for (const auto &kind : cs["procurement"].getMemberNames()) {
        const auto &order = cs["procurement"][kind];
        int started = order["started"].asInt();
        claims.push_back(
            {started < w.turn ? 0 : 2, started, "order", kind, number(order, "monthly_usd")});
    }
    std::stable_sort(claims.begin(), claims.end(), [](const Claim &x, const Claim &y) {
        return std::tie(x.group, x.started) < std::tie(y.group, y.started);
    });
    int stalled = 0;
    auto round6 = [](double v) { return std::round(v * 1e6) / 1e6; };
    for (const auto &c : claims) {
        // What is left pays for as much of the month as it covers; the rest of the month's work
        // waits.
        double share = c.cost > 0 ? std::clamp(budget[a] / c.cost, 0.0, 1.0) : 1.0;
        budget[a] -= share * c.cost;
        bool full = share >= 1 - 1e-9;
        stalled += !full;
        if (c.kind == "entity") {
            auto &e = w.entities[c.id];
            const auto &ce = std::as_const(e);
            if (full) {
                e.removeMember("unfunded");
                continue;
            }
            // A project behind by a whole month finishes a month later.
            double arrears = number(ce, "arrears") + (1 - share);
            int late = int(std::floor(arrears + 1e-9));
            e["completes_turn"] = e["completes_turn"].asInt() + late;
            e["arrears"] = round6(arrears - late);
            if (!ce.isMember("unfunded"))
                r.events.push_back({a, "construction_stalled", c.id,
                                    "Work on " + e["name"].asString() + " (" +
                                        e["kind"].asString() + ") in " +
                                        cT[e["region"].asString()]["name"].asString() +
                                        " slowed: the implementation allowance does not cover it.",
                                    e["region"].asString()});
            e["unfunded"] = true;
            continue;
        }
        auto &item = c.kind == "effort" ? s["efforts"][c.id] : s["procurement"][c.id];
        if (full)
            item.removeMember("unfunded");
        else
            item["unfunded"] = true;
        item["paid"] = round6(number(item, "paid") + share);
        if (number(item, "paid") < item["months"].asDouble() - 1e-6)
            continue;
        if (c.kind == "effort") {
            complete(a, c.id);
            continue;
        }
        const auto &o = *std::find_if(std::begin(orders), std::end(orders),
                                      [&](const Order &x) { return x.kind == c.id; });
        Int count = integer(item, "count");
        auto &m = s["military"];
        m[o.field] = integer(m, o.field) + count;
        r.events.push_back({a, "replacement", a,
                            cActors[a]["name"].asString() + " took delivery of " + grouped(count) +
                                " " + o.units + " to replace losses."});
        s["procurement"].removeMember(c.id);
    }
    for (auto key : {"efforts", "procurement"})
        if (std::as_const(s).isMember(key) && s[key].empty())
            s.removeMember(key);
    Json::Value summary;
    summary["allowance_usd"] = Int(std::llround(allowance[a]));
    summary["committed_usd"] = Int(std::llround(allowance[a] - budget[a]));
    summary["stalled"] = stalled;
    s["implementation"] = summary;
}
// A completed effort's benefit, once. An investment raises output; a reform raises the stability
// baseline, less for each reform completed in the previous five years, and gives a month's
// confidence on top.
void MonthlyStatus::complete(const std::string &a, const std::string &category) {
    auto &s = w.actors[a]["status"];
    auto name = cActors[a]["name"].asString();
    if (category == "investment") {
        for (auto &id : regionIds)
            if (cT[id]["owner"] == a && cT[id].isMember("gdp_usd"))
                T[id]["gdp_usd"] = Int(std::llround(double(integer(cT[id], "gdp_usd")) *
                                                    (1 + rate::investmentGain / 100)));
        r.events.push_back({a, "effort_completed", a,
                            name + " completed an investment programme; output rose about " +
                                std::to_string(int(rate::investmentGain)) + "%."});
    } else {
        int recent = 0;
        for (const auto &h : std::as_const(s)["effort_history"])
            if (h["category"] == "reform" &&
                h["completed_turn"].asInt() > w.turn - rate::reformMemoryMonths)
                ++recent;
        double gain = rate::reformGain / (1 + recent);
        s["baseline_stability"] =
            round1(std::clamp(number(s, "baseline_stability", 50) + gain, 0.0, 100.0));
        s["stability"] =
            round1(std::clamp(number(s, "stability", 50) + rate::reformConfidence, 0.0, 100.0));
        r.events.push_back({a, "effort_completed", a, name + " completed a reform programme."});
    }
    Json::Value done;
    done["category"] = category;
    done["completed_turn"] = w.turn;
    s["effort_history"].append(done);
    if (s["effort_history"].size() > 8)
        s["effort_history"].removeIndex(0, nullptr);
    s["efforts"].removeMember(category);
}
// A year without fighting anywhere lets excess active personnel go back to the reserve, gradually
// and never below what the nation's formations hold.
void MonthlyStatus::recover(const std::string &a) {
    const auto &cm = cActors[a]["status"]["military"];
    if (!cm.isObject())
        return;
    auto &m = w.actors[a]["status"]["military"];
    bool fighting = false;
    for (const auto &c : std::as_const(w.conflicts))
        if (isOpen(c) && isParty(c, a))
            for (const auto &p : c["actors"])
                fighting = fighting || (p != a && fightingPair(w, c, a, p.asString()));
    int peace = fighting ? 0 : cm.get("peace_months", 0).asInt() + 1;
    m["peace_months"] = std::min(peace, 1200);
    Int active = integer(cm, "active"), target = integer(cm["establishment"], "active");
    if (peace < rate::demobiliseAfter || active <= target)
        return;
    Int assigned = 0;
    for (const auto &e : std::as_const(w.entities))
        if (e["actor"] == a && e["kind"] == "army" &&
            (e["status"] == "active" || e["status"] == "under_construction"))
            assigned += integer(e, "personnel");
    Int released =
        std::min({active - target, active - std::max(assigned, target),
                  std::max<Int>(1, Int(std::llround(double(target) * rate::demobiliseShare)))});
    if (released <= 0)
        return;
    m["active"] = active - released;
    m["reserve"] = integer(cm, "reserve") + released;
    r.events.push_back({a, "demobilised", a,
                        cActors[a]["name"].asString() + " returned " + grouped(released) +
                            " troops to the reserve."});
}
// Exhaustion rises with the intensity a nation fights at, the share of its active personnel lost
// this month and any army cut off from home, and recovers in proportion to itself: slowly while it
// fights, faster when replacement is fully funded, fastest in peace.
void MonthlyStatus::exhaustionStep() {
    const auto &start = static_cast<const WorldState &>(forceStart);
    for (const auto &a : w.actors.getMemberNames()) {
        const auto &cm = cActors[a]["status"]["military"];
        if (!cm.isObject())
            continue;
        int intensity = -1;
        for (const auto &f : fronts)
            if (f.parties.contains(a) && !f.regions.empty())
                intensity = std::max(intensity, f.intensity);
        double e = std::clamp(number(cm, "exhaustion"), 0.0, 1.0), rise = 0, recovery;
        if (intensity >= 0) {
            double active = std::max(1.0, number(start.actors[a]["status"]["military"], "active"));
            rise =
                rate::exhaustionOps[intensity] + rate::exhaustionLosses * personnelLoss[a] / active;
            for (auto it = start.entities.begin(); it != start.entities.end(); ++it)
                if ((*it)["actor"] == a && (*it)["kind"] == "army" && (*it)["status"] == "active" &&
                    !supplied(start, adjacency, a, (*it)["region"].asString())) {
                    rise += rate::exhaustionIsolated;
                    break;
                }
            bool funded = cActors[a]["status"]["implementation"]["stalled"].asInt() == 0;
            recovery = rate::recoveryFighting + (funded ? 1.0 : 0.5) * rate::recoveryFunded;
        } else
            recovery = rate::recoveryPeace;
        double next = std::clamp(e + rise - recovery * e, 0.0, 1.0);
        next = std::round(next * 1e4) / 1e4;
        if (next > 0 || cm.isMember("exhaustion"))
            w.actors[a]["status"]["military"]["exhaustion"] = next;
    }
}
} // namespace lh::detail
