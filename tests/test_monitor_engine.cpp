#include "core/monitor_engine.h"

#include <cassert>
#include <iostream>
#include <thread>

int main() {
    using namespace cos;

    MonitorEngine engine;
    assert(engine.initialize(""));

    int counter = 0;
    engine.on_event([&](const Event&) { ++counter; });

    assert(engine.start_all());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(engine.is_running());
    assert(engine.stop_all());

    std::cout << "test_monitor_engine: OK (handled=" << counter << ")\n";
    return 0;
}
