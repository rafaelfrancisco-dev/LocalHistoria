#include "localhistoria/store.h"
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
    sql("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; CREATE TABLE IF NOT EXISTS campaign(id "
        "INTEGER PRIMARY KEY CHECK(id=1),state TEXT NOT NULL); CREATE TABLE IF NOT EXISTS jobs(id "
        "INTEGER PRIMARY KEY AUTOINCREMENT,key TEXT UNIQUE,payload TEXT,status TEXT NOT NULL,error "
        "TEXT DEFAULT '',fallback INTEGER DEFAULT 0); CREATE TABLE IF NOT EXISTS turns(turn "
        "INTEGER PRIMARY KEY,job INTEGER UNIQUE,state TEXT,intentions TEXT,events TEXT,reference "
        "TEXT); CREATE TABLE IF NOT EXISTS narration(job INTEGER PRIMARY KEY,text TEXT,fallback "
        "INTEGER); CREATE TABLE IF NOT EXISTS background(code TEXT PRIMARY KEY,document TEXT); "
        "CREATE TABLE IF NOT EXISTS initial_world(id INTEGER PRIMARY KEY CHECK(id=1),state TEXT "
        "NOT NULL);");
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
        sql("DELETE FROM narration; DELETE FROM turns; DELETE FROM jobs; DELETE FROM campaign; "
            "DELETE FROM initial_world;");
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
                   const Json::Value &refs, bool fail, bool fallback) {
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
        std::string summary;
        for (auto &e : r.events)
            summary += e.text + "\n";
        execute("INSERT INTO narration VALUES(?,?,1)", {id, summary});
        execute("UPDATE jobs SET status='complete',fallback=? WHERE id=?",
                {fallback ? "1" : "0", id});
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
    auto j = query("SELECT id,status,error,fallback FROM jobs WHERE id=?", {id});
    if (j.empty())
        throw ValidationError("Unknown job");
    return j[0];
}
Json::Value Store::journal() {
    auto j =
        query("SELECT t.turn,t.events,n.text AS narration,n.fallback AS narration_fallback FROM "
              "turns t LEFT JOIN narration n ON t.job=n.job ORDER BY t.turn DESC LIMIT 24");
    for (auto &x : j)
        x["events"] = parse(x["events"].asString());
    return j;
}
Json::Value Store::background(const std::string &c) {
    auto j = query("SELECT document FROM background WHERE code=?", {c});
    return j.empty() ? Json::Value() : parse(j[0]["document"].asString());
}
void Store::cache(const std::string &c, const Json::Value &j) {
    execute("INSERT OR REPLACE INTO background VALUES(?,?)", {c, dump(j)});
}
} // namespace lh
