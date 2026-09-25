#pragma once
#include "simulation.h"
namespace lh {
constexpr int consequenceVersion = 1, projectVersion = 1, intentVersion = 1;
constexpr unsigned consequenceCapacity = 64, projectCapacity = 32;
constexpr int consequenceExpiry = 24;
bool groundedConsequences(const WorldState &);
bool distinctProjects(const WorldState &);
// Intent version 1 (plan 35): a treaty offer's kind must match its clauses, and a funded project
// written on a policy outcome is kept.
bool groundedIntents(const WorldState &);
std::string consequenceText(const std::string &);
// Bounded, deterministic accounting shared by compilation and monthly replay.
void expireConsequences(Json::Value &, int turn);
Json::Value recordConsequence(Json::Value &, int turn, const std::string &key,
                              const std::string &kind, bool envelope);
std::string domesticProjectId(const std::string &actor, const std::string &category,
                              const std::string &target, const std::string &benefit);
bool supportedProject(const std::string &category, const std::string &benefit);
void migrateDomesticProjects(WorldState &);
} // namespace lh
