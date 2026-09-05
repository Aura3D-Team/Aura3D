#include <memory>
#include <stdexcept>
#include "aura/UI/Core/Signal.h"
#include "TestUtils.h"

using namespace aura3d::ui;

int main()
{
    Signal<> signal;
    auto connection = signal.connect([] {});
    auto copy = connection;
    connection.disconnect();
    AURA_CHECK(!connection.connected() && !copy.connected(), "disconnection is visible through every handle");

    int calls = 0;
    signal.connect([&] {
        for (int i = 0; i < 100; ++i)
            signal.connect([] {});
        ++calls;
    });
    signal.emit();
    AURA_CHECK(calls == 1, "a callback can grow the signal and continue using its captures");
    signal.clear();

    Connection self;
    self = signal.connect([&] { self.disconnect(); ++calls; });
    signal.emit();
    AURA_CHECK(calls == 2, "a callback can disconnect itself and continue executing");

    auto doomed = std::make_unique<Signal<>>();
    doomed->connect([&] { doomed.reset(); ++calls; });
    doomed->connect([&] { calls += 100; });
    doomed->emit();
    AURA_CHECK(calls == 3, "destroying the signal during emit cancels remaining callbacks safely");

    Signal<> moved(std::move(signal));
    signal.connect([&] { ++calls; });
    signal.emit();
    AURA_CHECK(calls == 4, "a moved-from signal can be reused");

    signal.clear();
    signal.connect([] { throw std::runtime_error("observer"); });
    AURA_CHECK_THROWS(signal.emit(), std::runtime_error, "observer exceptions propagate");
    signal.clear();
    AURA_CHECK(signal.empty(), "an exception does not strand the signal in emission state");
    AURA_TEST_MAIN_RETURN();
}
