#pragma once
#include "localhistoria/simulation.h"
#include <map>
#include <string>
#include <vector>
namespace lh::detail {
// This month's changes, one per kind and ID, in the order first touched. open() starts a frame;
// the first touch of an older change inside it saves that change, so rollback() restores only what
// the frame touched and drops what it added.
class PendingStore {
  public:
    struct Pending {
        std::string kind, id, event, rationale;
        Json::Value before, after;
    };
    Json::Value &touch(const std::string &kind, const std::string &id, const Json::Value &before,
                       const std::string &event, const std::string &rationale) {
        auto key = kind + ":" + id;
        auto found = index.find(key);
        if (found == index.end()) {
            index[key] = entries.size();
            entries.push_back({kind, id, event, rationale, before, before});
            return entries.back().after;
        }
        auto &p = entries[found->second];
        if (!frames.empty() && found->second < frames.back().size)
            frames.back().saved.try_emplace(found->second, p.after, p.rationale);
        p.rationale = std::string(p.rationale + "; " + rationale).substr(0, 1000);
        return p.after;
    }
    Json::Value current(const std::string &kind, const Json::Value &collection,
                        const std::string &id) const {
        auto found = index.find(kind + ":" + id);
        return found != index.end() ? entries[found->second].after : collection[id];
    }
    bool contains(const std::string &kind, const std::string &id) const {
        return index.contains(kind + ":" + id);
    }
    std::vector<Pending>::const_iterator begin() const { return entries.begin(); }
    std::vector<Pending>::const_iterator end() const { return entries.end(); }
    void open() { frames.push_back({entries.size(), {}}); }
    void commit() {
        auto top = std::move(frames.back());
        frames.pop_back();
        if (!frames.empty())
            for (auto &[n, saved] : top.saved)
                if (n < frames.back().size)
                    frames.back().saved.try_emplace(n, std::move(saved));
    }
    void rollback() {
        auto &top = frames.back();
        for (auto &[n, saved] : top.saved) {
            entries[n].after = std::move(saved.first);
            entries[n].rationale = std::move(saved.second);
        }
        for (auto n = top.size; n < entries.size(); ++n)
            index.erase(entries[n].kind + ":" + entries[n].id);
        entries.erase(entries.begin() + std::ptrdiff_t(top.size), entries.end());
        frames.pop_back();
    }

  private:
    struct Frame {
        size_t size;
        std::map<size_t, std::pair<Json::Value, std::string>> saved;
    };
    std::vector<Pending> entries;
    std::map<std::string, size_t> index;
    std::vector<Frame> frames;
};
} // namespace lh::detail
