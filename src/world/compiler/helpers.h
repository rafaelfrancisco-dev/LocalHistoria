#pragma once
#include "world/world_internal.h"
namespace lh::detail {
// A physically unreachable operation this month: it is dropped and the rest of its event and the
// turn continue.
struct Infeasible : ValidationError {
    using ValidationError::ValidationError;
};
std::string slug(const std::string &text, size_t limit = 40);
double kilometres(const Json::Value &a, const Json::Value &b);
// Effects ordered from worst to best, so the compiler can force a ceiling or floor on what the
// model wrote.
int rank(const std::string &effect);
std::string weaker(const std::string &a, const std::string &b);
std::string stronger(const std::string &a, const std::string &b);
// Treaty terms a small model may attach: normalized to known clause fields, at most four.
Json::Value treatyTerms(const Json::Value &terms);
bool securityTreaty(const std::string &kind);
// How much an event's effects weigh: 2 per severe or strong one, 1 per mild one, ignoring malformed
// entries.
int effectWeight(const Json::Value &effects);
std::string alphanumeric(const std::string &v);
// A treaty keeps its eight latest terms.
void appendTerms(Json::Value &treaty, const Json::Value &done);
} // namespace lh::detail
