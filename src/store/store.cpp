#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <algorithm>
#include <memory>
namespace lh {
Store::Store(const std::string &p, bool readOnly) {
    if (sqlite3_open_v2(p.c_str(), &db,
                        readOnly ? SQLITE_OPEN_READONLY
                                 : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                        nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        db = nullptr;
        throw std::runtime_error("Cannot open database");
    }
    if (readOnly)
        return;
    sqlite3_busy_timeout(db, 3000);
    // Model-written national histories, rewritten at most yearly; a cache outside turn
    // transactions.
    sql("CREATE TABLE IF NOT EXISTS chronicles(actor TEXT PRIMARY KEY,document TEXT);");
    sql("CREATE TABLE IF NOT EXISTS monthly_allocations(campaign TEXT,month INTEGER,slots "
        "TEXT,PRIMARY KEY(campaign,month));");
    // The nations asked to react each month: kept so a retried month asks the same ones, and read
    // back for how long each has waited.
    sql("CREATE TABLE IF NOT EXISTS monthly_reactions(campaign TEXT,month INTEGER,actors "
        "TEXT,PRIMARY KEY(campaign,month));");
    sql("CREATE TABLE IF NOT EXISTS event_links(event TEXT,kind TEXT,target TEXT,PRIMARY "
        "KEY(event,kind,target)); CREATE INDEX IF NOT EXISTS event_target ON "
        "event_links(kind,target); CREATE TABLE IF NOT EXISTS events(id TEXT PRIMARY KEY,turn "
        "INTEGER,document TEXT); CREATE INDEX IF NOT EXISTS event_turn ON events(turn); CREATE "
        "TABLE IF NOT EXISTS inference(id INTEGER PRIMARY KEY,job TEXT,record TEXT);");
    sql("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; CREATE TABLE IF NOT EXISTS campaign(id "
        "INTEGER PRIMARY KEY CHECK(id=1),state TEXT NOT NULL); CREATE TABLE IF NOT EXISTS jobs(id "
        "INTEGER PRIMARY KEY AUTOINCREMENT,key TEXT UNIQUE,payload TEXT,status TEXT NOT NULL,error "
        "TEXT DEFAULT ''); CREATE TABLE IF NOT EXISTS turns(turn "
        "INTEGER PRIMARY KEY,job INTEGER UNIQUE,state TEXT,intentions TEXT,events TEXT,reference "
        "TEXT); CREATE TABLE IF NOT EXISTS narration(job INTEGER PRIMARY KEY,text TEXT,fallback "
        "INTEGER); CREATE TABLE IF NOT EXISTS background(code TEXT PRIMARY KEY,document TEXT); "
        "CREATE TABLE IF NOT EXISTS initial_world(id INTEGER PRIMARY KEY CHECK(id=1),state TEXT "
        "NOT NULL); CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY,value TEXT);");
    migrate();
}
void Store::migrate() {
    switch (query("PRAGMA user_version")[0]["user_version"].asInt()) {
    case 0:
        // Version 1: the tables above, all created if missing.
        sql("PRAGMA user_version = 1");
        [[fallthrough]];
    case 1:
        break;
    default:
        throw std::runtime_error("Save was written by a newer version");
    }
}
Store::~Store() { sqlite3_close(db); }
void Store::sql(const std::string &s) {
    char *e = nullptr;
    if (sqlite3_exec(db, s.c_str(), nullptr, nullptr, &e) != SQLITE_OK) {
        std::string m = e ? e : "SQLite error";
        sqlite3_free(e);
        throw std::runtime_error(m);
    }
}
Json::Value Store::query(const std::string &s, const std::vector<std::string> &args) {
    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(db, s.c_str(), -1, &raw, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> stmt(raw, sqlite3_finalize);
    for (size_t i = 0; i < args.size(); i++)
        sqlite3_bind_text(raw, int(i + 1), args[i].c_str(), -1, SQLITE_TRANSIENT);
    Json::Value rows(Json::arrayValue);
    int rc;
    while ((rc = sqlite3_step(raw)) == SQLITE_ROW) {
        Json::Value row;
        for (int i = 0; i < sqlite3_column_count(raw); i++) {
            auto name = sqlite3_column_name(raw, i);
            switch (sqlite3_column_type(raw, i)) {
            case SQLITE_INTEGER:
                row[name] = Json::Int64(sqlite3_column_int64(raw, i));
                break;
            case SQLITE_FLOAT:
                row[name] = sqlite3_column_double(raw, i);
                break;
            case SQLITE_NULL:
                row[name] = Json::nullValue;
                break;
            default:
                row[name] = reinterpret_cast<const char *>(sqlite3_column_text(raw, i));
            }
        }
        rows.append(row);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error(sqlite3_errmsg(db));
    return rows;
}
void Store::execute(const std::string &s, const std::vector<std::string> &a) { query(s, a); }
Json::Value Store::campaign() {
    auto j = query("SELECT state FROM campaign WHERE id=1");
    return j.empty() ? Json::Value() : parse(j[0]["state"].asString());
}
void Store::create(const WorldState &w, bool replace) {
    if (!campaign().isNull() && !replace)
        throw ValidationError("Confirm replacement of the existing campaign");
    if (!query("SELECT id FROM jobs WHERE status='running'").empty())
        throw ValidationError("A turn is running");
    sql("BEGIN IMMEDIATE");
    try {
        eventLogCache.reset();
        sql("DELETE FROM narration; DELETE FROM turns; DELETE FROM jobs; DELETE FROM campaign; "
            "DELETE FROM initial_world; DELETE FROM monthly_allocations; DELETE FROM "
            "monthly_reactions; DELETE FROM event_links; "
            "DELETE FROM events; DELETE FROM inference; DELETE FROM chronicles;");
        execute("INSERT INTO campaign VALUES(1,?)", {dump(encode(w))});
        execute("INSERT INTO initial_world VALUES(1,?)", {dump(encode(w))});
        sql("COMMIT");
    } catch (...) {
        sql("ROLLBACK");
        throw;
    }
}
void Store::recover() {
    sql("UPDATE jobs SET status='interrupted',error='Interrupted before commit; submit again with "
        "a new key' WHERE status='running';");
}
Json::Value Store::submit(int turn, const std::string &key, const std::string &payload) {
    auto old = query("SELECT * FROM jobs WHERE key=?", {key});
    if (!old.empty()) {
        if (old[0]["payload"].asString() != payload)
            throw ValidationError("Idempotency key reused with different payload");
        return old[0];
    }
    auto c = campaign();
    if (c.isNull() || c["turn"].asInt() != turn)
        throw ValidationError("Stale turn");
    if (!query("SELECT id FROM jobs WHERE status='running'").empty())
        throw ValidationError("A turn is already running");
    execute("INSERT INTO jobs(key,payload,status) VALUES(?,?,'running')", {key, payload});
    return query("SELECT * FROM jobs WHERE key=?", {key})[0];
}
void Store::commit(const std::string &id, const Resolution &r, const Json::Value &i,
                   const Json::Value &refs, bool fail) {
    sql("BEGIN IMMEDIATE");
    try {
        auto j = job(id);
        auto c = campaign();
        if (j["status"] != "running" || c["turn"].asInt() + 1 != r.state.turn)
            throw ValidationError("Stale job");
        execute("UPDATE campaign SET state=? WHERE id=1", {dump(encode(r.state))});
        if (fail)
            throw std::runtime_error("Injected failure");
        execute("INSERT INTO turns VALUES(?,?,?,?,?,?)",
                {std::to_string(r.state.turn), id, dump(encode(r.state)), dump(i),
                 dump(encode(r.events)), dump(refs)});
        for (auto &event : refs["delta"]["events"]) {
            auto eid = event["id"].asString();
            for (auto &cause : event["causes"]) {
                if (!cause.isString() ||
                    query("SELECT id FROM events WHERE id=?", {cause.asString()}).empty())
                    throw ValidationError("Unknown or forward event cause");
            }
            execute("INSERT INTO events VALUES(?,?,?)",
                    {eid, std::to_string(r.state.turn), dump(event)});
            execute("INSERT OR IGNORE INTO event_links VALUES(?,'actors',?)",
                    {eid, event["actor"].asString()});
            for (auto kind : {"actors", "regions", "entities", "causes"})
                for (auto &target : event[kind])
                    execute("INSERT OR IGNORE INTO event_links VALUES(?,?,?)",
                            {eid, kind, target.asString()});
        }
        std::string summary;
        for (auto &e : r.events)
            summary += e.text + "\n";
        execute("INSERT INTO narration VALUES(?,?,1)", {id, summary});
        execute("UPDATE jobs SET status='complete' WHERE id=?", {id});
        sql("COMMIT");
    } catch (...) {
        sql("ROLLBACK");
        throw;
    }
}
void Store::narrate(const std::string &id, const std::string &t, bool f) {
    execute("UPDATE narration SET text=?,fallback=? WHERE job=?", {t, f ? "1" : "0", id});
}
Json::Value Store::job(const std::string &id) {
    auto j = query("SELECT id,status,error FROM jobs WHERE id=?", {id});
    if (j.empty())
        throw ValidationError("Unknown job");
    return j[0];
}
Json::Value Store::journal() {
    auto j =
        query("SELECT t.turn,t.events,n.text AS narration,n.fallback AS narration_fallback FROM "
              "turns t LEFT JOIN narration n ON t.job=n.job ORDER BY t.turn DESC LIMIT 24");
    std::map<int, Json::Value> structured;
    for (auto &row : query("SELECT turn,document FROM events WHERE turn IN (SELECT turn FROM turns "
                           "ORDER BY turn DESC LIMIT 24) ORDER BY rowid"))
        structured[row["turn"].asInt()].append(parse(row["document"].asString()));
    for (auto &month : j) {
        month["events"] = parse(month["events"].asString());
        auto found = structured.find(month["turn"].asInt());
        month["structured_events"] =
            found == structured.end() ? Json::Value(Json::arrayValue) : found->second;
    }
    // Prompts read what actually happened; the journal shows the model's text with a badge.
    for (auto &month : j)
        for (auto &event : month["events"])
            for (auto &d : month["structured_events"])
                if (d["id"] == event["target"]) {
                    auto clause = droppedClause(d);
                    if (!clause.empty())
                        event["prompt_text"] = event["text"].asString() + clause;
                    break;
                }
    return j;
}
Json::Value Store::monthChanges(int turn) {
    auto rows =
        query("SELECT json_extract(reference,'$.delta.changes') AS c FROM turns WHERE turn=?",
              {std::to_string(turn)});
    if (rows.empty() || !rows[0]["c"].isString())
        return Json::arrayValue;
    return parse(rows[0]["c"].asString());
}
CountryContext Store::campaignContext(const WorldState &w, const std::string &actor) {
    auto c = context(w, actor, journal(), background(actor));
    c.facts["regional_politics"] = Json::objectValue;
    for (auto &r : w.regionalPolitics.getMemberNames())
        if (w.territories[r]["controller"] == actor)
            c.facts["regional_politics"][r] = w.regionalPolitics[r];
    Json::Value regions(Json::arrayValue), entities(Json::arrayValue);
    for (auto &id : c.facts["territories"].getMemberNames())
        regions.append(id);
    for (auto &id : c.facts["entities"].getMemberNames())
        entities.append(id);
    auto older =
        query("WITH RECURSIVE related(id) AS (SELECT event FROM event_links WHERE (kind='actors' "
              "AND target=?) OR (kind='regions' AND target IN (SELECT value FROM json_each(?))) OR "
              "(kind='entities' AND target IN (SELECT value FROM json_each(?))) UNION SELECT "
              "l.target FROM event_links l JOIN related r ON l.event=r.id WHERE l.kind='causes') "
              "SELECT e.document FROM events e JOIN related r ON e.id=r.id WHERE " +
                  happened + " ORDER BY e.turn DESC LIMIT 20",
              {actor, dump(regions), dump(entities)});
    c.facts["related_history"] = Json::arrayValue;
    for (auto &row : older) {
        auto d = parse(row["document"].asString());
        d["text"] = d["text"].asString() + droppedClause(d);
        c.facts["related_history"].append(d);
    }
    return c;
}
void Store::recordInference(const std::string &job, const Json::Value &records) {
    for (auto &r : records)
        execute("INSERT INTO inference(job,record) VALUES(?,?)", {job, dump(r)});
}
Json::Value Store::background(const std::string &c) {
    auto j = query("SELECT document FROM background WHERE code=?", {c});
    return j.empty() ? Json::Value() : parse(j[0]["document"].asString());
}
Json::Value Store::chronicle(const std::string &actor) {
    auto j = query("SELECT document FROM chronicles WHERE actor=?", {actor});
    return j.empty() ? Json::Value() : parse(j[0]["document"].asString());
}
void Store::saveChronicle(const std::string &actor, const Json::Value &document) {
    execute("INSERT OR REPLACE INTO chronicles VALUES(?,?)", {actor, dump(document)});
}
void Store::cache(const std::string &c, const Json::Value &j) {
    execute("INSERT OR REPLACE INTO background VALUES(?,?)", {c, dump(j)});
}
std::string Store::setting(const std::string &key, const std::string &fallbackDefault) {
    auto j = query("SELECT value FROM settings WHERE key=?", {key});
    return j.empty() ? fallbackDefault : j[0]["value"].asString();
}
void Store::setSetting(const std::string &key, const std::string &value) {
    execute("INSERT INTO settings VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
            {key, value});
}
} // namespace lh
