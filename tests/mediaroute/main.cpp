// SPDX-License-Identifier: AGPL-3.0-only
// With headphones or Bluetooth the sound is the user's in them, and a voice message must not go to
// the earpiece (issue 22). Exit 0: passed.
#include "mediaroute.h"

#include <QCoreApplication>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char *what)
{
    if (!ok) {
        std::printf("FAILED: %s\n", what);
        ++failures;
    }
}

// ohm's route plugin: an output that is now the active one, an input, an output left.
const uint ActiveOutput = (1u << 0) | (1u << 27);
const uint ActiveInput = (1u << 1) | (1u << 27);
const uint Output = 1u << 0;

// The manager as the phone's would answer, by hand.
class Route : public MediaRoute
{
public:
    int changes = 0;
    Route()
    {
        connect(this, &MediaRoute::headphonesChanged, [this]() { ++changes; });
    }
    using MediaRoute::answeredRoute;
    void route(const char *name, uint type = ActiveOutput) { onRouteChanged(QLatin1String(name), type); }

protected:
    void busListen() override {}
    void busQuery() override {}
};

void testThePhoneItself()
{
    Route r;
    r.answeredRoute(QStringLiteral("speaker"));
    check(!r.headphones(), "the loudspeaker is the phone");
    r.route("earpiece");
    check(!r.headphones(), "the earpiece (ours, at the ear) is the phone");
    check(r.changes == 0, "nothing to tell");
}

void testHeadphones()
{
    for (const char *device : { "bluetootha2dp", "bluetoothsco", "headset", "headphone" }) {
        Route r;
        r.answeredRoute(QStringLiteral("speaker"));
        r.route(device);
        check(r.headphones(), "headphones or Bluetooth");
        r.route("speaker");
        check(!r.headphones(), "and back on the phone");
        check(r.changes == 2, "each told once");
    }
}

void testAtTheStart()
{
    // Already connected when the app starts: the answer to the first question says so.
    Route r;
    r.answeredRoute(QStringLiteral("bluetootha2dp"));
    check(r.headphones(), "headphones from the start");
}

void testOnlyActiveOutputs()
{
    Route r;
    r.answeredRoute(QStringLiteral("speaker"));
    r.route("headset", ActiveInput);
    check(!r.headphones(), "a headset's microphone is not where the sound goes");
    r.route("bluetootha2dp", Output);
    check(!r.headphones(), "an output left, not the active one");
    r.answeredRoute(QString());
    check(!r.headphones(), "no answer changes nothing");
}

void testUnknownCountsAsHeadphones()
{
    Route r;
    r.answeredRoute(QStringLiteral("usbaudio"));
    check(r.headphones(), "unknown: no earpiece");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testThePhoneItself();
    testHeadphones();
    testAtTheStart();
    testOnlyActiveOutputs();
    testUnknownCountsAsHeadphones();
    std::printf(failures ? "%d failure(s)\n" : "mediaroute: all passed\n", failures);
    return failures ? 1 : 0;
}
