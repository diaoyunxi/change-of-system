#include "core/event.h"

#include <cassert>
#include <iostream>

int main() {
    using namespace cos;

    Event e;
    e.category = EventCategory::Filesystem;
    e.type = EventType::FileModified;
    e.source = "filesystem";
    e.target = "/tmp/test";
    e.summary = "unit-test";

    assert(!category_name(e.category).empty());
    assert(!type_name(e.type).empty());
    assert(category_of(e.type) == EventCategory::Filesystem);

    Event e2;
    e2.category = EventCategory::Process;
    e2.type = EventType::ProcessStarted;
    assert(category_of(e2.type) == EventCategory::Process);

    std::cout << "test_event: OK\n";
    return 0;
}
