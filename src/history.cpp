#include "localhistoria/history.h"
#include <ctime>
#include <drogon/utils/Utilities.h>
#include <regex>
namespace lh {
namespace {
// Bounded plain-text extraction; cached HTML is never rendered or executed.
std::string plainText(const std::string &html, size_t limit) {
    std::string out;
    bool space = true;
    for (size_t i = 0; i < html.size() && out.size() < limit; ++i) {
        if (html[i] == '<') {
            auto end = html.find('>', i);
            if (end == std::string::npos)
                break;
            i = end;
            if (!space) {
                out += ' ';
                space = true;
            }
            continue;
        }
        unsigned char ch = html[i];
        if (std::isspace(ch)) {
            if (!space)
                out += ' ';
            space = true;
        } else {
            out += char(ch);
            space = false;
        }
    }
    // Drop a partially copied final UTF-8 codepoint.
    if (out.size() >= limit) {
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xc0) == 0x80)
            out.pop_back();
        if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xc0)
            out.pop_back();
    }
    for (auto [from, to] : std::vector<std::pair<std::string, std::string>>{{"&amp;", "&"},
                                                                            {"&nbsp;", " "},
                                                                            {"&#160;", " "},
                                                                            {"&quot;", "\""},
                                                                            {"&#39;", "'"},
                                                                            {"&lt;", "<"},
                                                                            {"&gt;", ">"}}) {
        size_t pos = 0;
        while ((pos = out.find(from, pos)) != std::string::npos) {
            out.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return out;
}
std::string introduction(const std::string &html) {
    size_t pos = 0;
    while ((pos = html.find("<p", pos)) != std::string::npos) {
        if (pos + 2 < html.size() && html[pos + 2] != '>' && html[pos + 2] != ' ') {
            pos += 2;
            continue;
        }
        auto end = html.find("</p>", pos);
        if (end == std::string::npos)
            break;
        auto text = plainText(html.substr(pos, std::min<size_t>(end - pos, 30000)), 3000);
        if (text.size() > 150)
            return text;
        pos = end + 4;
    }
    return "No substantial introduction available.";
}
// The closing part of the History section: recent history grounds present-day decisions better than
// origins.
std::string recentHistory(const std::string &html, size_t limit) {
    auto heading = html.find("id=\"History\"");
    if (heading == std::string::npos || html.find('>', heading) == std::string::npos)
        return "History section unavailable in this article.";
    auto start = html.find('>', heading) + 1;
    auto next = html.find("<h2", start);
    auto end = next == std::string::npos ? html.size() : next;
    // Take the section's tail before extracting text, so huge articles stay bounded.
    auto from = end - start > 600000 ? end - 600000 : start;
    static const std::regex citation(R"( ?\[ ?[0-9a-z]{1,4} ?\])");
    auto text = std::regex_replace(plainText(html.substr(from, end - from), 400000), citation, "");
    while (!text.empty() && text.back() == ' ')
        text.pop_back();
    if (text.size() <= limit)
        return text;
    auto cut = text.find(' ', text.size() - limit);
    if (cut == std::string::npos)
        return text.substr(text.size() - limit);
    return "…" + text.substr(cut + 1);
}
} // namespace
Json::Value fetchRegionalHistory(const std::string &name, const std::string &country,
                                 Transport get) {
    Json::Value unavailable;
    unavailable["available"] = false;
    try {
        if (name.empty() || name.size() > 200 || !isCountryCode(country))
            throw ValidationError("Regional identity unavailable");
        // Exact English label, current country and direct national administrative parent: no fuzzy
        // name guesses.
        auto query = "SELECT DISTINCT ?item WHERE { ?country wdt:P297 " + dump(country) +
                     " . ?item rdfs:label " + dump(name) +
                     "@en; wdt:P17 ?country; wdt:P131 ?country . FILTER NOT EXISTS { ?item "
                     "wdt:P576 ?date } } LIMIT 2";
        auto result = get("https://query.wikidata.org",
                          "/sparql?format=json&query=" + drogon::utils::urlEncode(query), {}, 12);
        auto rows = result["results"]["bindings"];
        if (!rows.isArray() || rows.size() != 1)
            throw ValidationError("Regional mapping missing or ambiguous");
        auto uri = rows[0]["item"]["value"].asString();
        auto qid = uri.substr(uri.find_last_of('/') + 1);
        if (!isEntityId(qid))
            throw ValidationError("Invalid regional entity");
        // Reuse bounded article extraction after a verified regional mapping.
        Transport mapped = [&](const std::string &host, const std::string &path,
                               const Json::Value &body, double timeout) {
            if (host == "https://query.wikidata.org") {
                Json::Value j;
                j["results"]["bindings"] = rows;
                return j;
            }
            return get(host, path, body, timeout);
        };
        auto out = fetchHistory("ZZ", mapped);
        out.removeMember("code");
        out["regional_wikidata"] = qid;
        out.removeMember("country_wikidata");
        out["region_name"] = name;
        out["initial_country"] = country;
        return out;
    } catch (const std::exception &e) {
        unavailable["reason"] = e.what();
        return unavailable;
    }
}
Json::Value fetchHistory(const std::string &code, Transport get) {
    if (!isCountryCode(code))
        throw ValidationError("Invalid country code");
    Json::Value out;
    out["available"] = false;
    out["code"] = code;
    out["language"] = "en";
    out["retrieved_at"] = Json::Int64(std::time(nullptr));
    out["attribution"] = "Wikipedia contributors, CC BY-SA 4.0; Wikidata CC0";
    out["notice"] = "Reference only. Current sources may postdate January 2026.";
    try {
        std::map<std::string, std::string> overrides = {
            {"FR", "Q142"}, {"NO", "Q20"}, {"XK", "Q1246"}, {"TW", "Q865"}};
        std::string qid;
        if (overrides.contains(code))
            qid = overrides.at(code);
        else {
            auto query = "SELECT DISTINCT ?item WHERE { ?item wdt:P297 \"" + code +
                         "\" . FILTER NOT EXISTS { ?item wdt:P576 ?date } } LIMIT 2";
            auto j = get("https://query.wikidata.org",
                         "/sparql?format=json&query=" + drogon::utils::urlEncode(query), {}, 12);
            auto rows = j["results"]["bindings"];
            if (!rows.isArray() || rows.size() != 1)
                throw std::runtime_error("Country mapping missing or ambiguous");
            auto uri = rows[0]["item"]["value"].asString();
            qid = uri.substr(uri.find_last_of('/') + 1);
        }
        if (!isEntityId(qid))
            throw std::runtime_error("Invalid entity");
        auto entity =
            get("https://www.wikidata.org", "/wiki/Special:EntityData/" + qid + ".json", {}, 12);
        auto title = entity["entities"][qid]["sitelinks"]["enwiki"]["title"];
        if (!title.isString() || title.asString().empty())
            throw std::runtime_error("No English Wikipedia sitelink");
        auto article =
            get("https://en.wikipedia.org",
                "/w/rest.php/v1/page/" + drogon::utils::urlEncode(title.asString()) + "/with_html",
                {}, 12);
        if (!article["latest"]["id"].isIntegral() || !article["html"].isString())
            throw std::runtime_error("Article revision unavailable");
        auto html = article["html"].asString();
        out["available"] = true;
        out["country_wikidata"] = qid;
        out["title"] = title;
        out["article_url"] =
            "https://en.wikipedia.org/wiki/" + drogon::utils::urlEncode(title.asString());
        out["revision"] = article["latest"]["id"];
        out["excerpt"] = introduction(html);
        out["history_excerpt"] = recentHistory(html, 3000);
        // A longer tail that the model condenses into a nation's chronicle; never sent to prompts
        // directly.
        out["history_source"] = recentHistory(html, 12000);
        // Marks caches that hold the end of the history section and the longer chronicle source.
        out["history_scope"] = "recent_source";
        out["kind"] = "Bounded article introduction and the most recent part of its history "
                      "section; formatting removed";
    } catch (const std::exception &e) {
        out["available"] = false;
        out["reason"] = e.what();
    }
    return out;
}
} // namespace lh
