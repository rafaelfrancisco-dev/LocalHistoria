#pragma once
#include "localhistoria/history.h"
#include "localhistoria/providers.h"
#include "localhistoria/status.h"
#include "localhistoria/store.h"
#include "localhistoria/world.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
using namespace lh;
extern int checks;
void check(bool b, const char *s);
template <class F> void rejects(F f) {
    bool thrown = false;
    try {
        f();
    } catch (...) {
        thrown = true;
    }
    check(thrown, "Expected rejection");
}
// Wikipedia history extraction and the reference briefs built from it.
void historyTests();
