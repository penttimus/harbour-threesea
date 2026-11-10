// SPDX-License-Identifier: AGPL-3.0-only
// What the loudspeaker button says has to be what the audio policy did: a wrong "on" takes the
// ear guard away with the sound still at the ear. And a voice call stays off the loudspeaker.
// Exit 0: passed.
#include "callroute.h"

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

// ohm's route plugin: an output that is now the active one.
const uint ActiveOutput = (1u << 0) | (1u << 27);

// The manager as the phone's would answer, by hand.
class Route : public CallRoute
{
public:
    QList<bool> sent;
    int failed = 0;
    Route()
    {
        connect(this, &CallRoute::failed, [this]() { ++failed; });
    }
    using CallRoute::answeredRoute;
    using CallRoute::answeredSend;
    using CallRoute::confirmTimedOut;
    using CallRoute::onRouteChanged;

    void started(bool prefer = false)
    {
        setCall(true, prefer);
        // What it was before the call class was ours, then the policy's move.
        answeredRoute(QStringLiteral("speaker"));
        route("earpiece");
    }
    void route(const char *name) { onRouteChanged(QLatin1String(name), ActiveOutput); }

protected:
    void busListen(bool) override {}
    void busQuery() override {}
    void busSend(bool on) override { sent.append(on); }
};

void testLightsOnlyWhenConfirmed()
{
    Route r;
    r.setCall(true, false);
    r.answeredRoute(QStringLiteral("speaker"));
    check(!r.available(), "no button before the sound is on the earpiece");
    r.route("earpiece");
    check(r.available(), "on the earpiece: the button is there");
    r.setSpeaker(true);
    check(!r.speaker(), "not lit on asking");
    check(r.available(), "the button stays while waiting");
    check(r.sent == QList<bool>{ true }, "asked for");
    r.answeredSend(true, true);
    check(!r.speaker(), "not lit on a reply alone");
    r.route("speaker");
    check(r.speaker(), "lit once the route moved");
    check(r.available(), "and the button stays to go back");

    r.setSpeaker(false);
    check(r.speaker(), "stays lit until the way back is confirmed");
    r.answeredSend(true, false);
    check(r.speaker(), "also on a reply alone");
    r.route("earpiece");
    check(!r.speaker(), "off confirmed");
    check(r.sent == (QList<bool>{ true, false }), "taken back");
}

void testOtherSignalsDoNotCount()
{
    Route r;
    r.started();
    r.setSpeaker(true);
    // The microphone's route, and the earpiece being left, come over the same signal.
    r.onRouteChanged(QStringLiteral("voicecallrecord"), (1u << 1) | (1u << 27));
    check(!r.speaker(), "an input is not the sound's route");
    r.onRouteChanged(QStringLiteral("speaker"), 1u << 0);
    check(!r.speaker(), "a route that is not the active one neither");
    r.route("speaker");
    check(r.speaker(), "the active output is");
}

void testRefused()
{
    Route r;
    r.started();
    r.setSpeaker(true);
    r.answeredSend(false, true);
    check(!r.speaker(), "refused: not lit");
    check(r.failed == 1, "refused: the user hears of it");
    check(r.sent.last() == false, "refused: nothing left preferred");
    check(r.available(), "refused: still on the earpiece, the button stays");
}

void testPolicyDoesNotMoveIt()
{
    Route r;
    r.started();
    r.setSpeaker(true);
    r.answeredSend(true, true);
    r.confirmTimedOut();
    check(!r.speaker(), "still on the earpiece: not lit");
    check(r.failed == 1, "still on the earpiece: the user hears of it");
    check(r.sent == (QList<bool>{ true, false }), "still on the earpiece: the preference is taken back");
}

void testNoEarpiece()
{
    // A device without one, or the policy keeps it shut: the sound is on the loudspeaker already.
    Route r;
    r.setCall(true, true);
    r.answeredRoute(QStringLiteral("speaker"));
    check(!r.available(), "no earpiece: no button");
    check(!r.speaker(), "no earpiece: not lit either, it is not our doing");
    r.setSpeaker(true);
    check(r.sent.isEmpty(), "no earpiece: nothing sent, also not for a camera");
}

void testPreference()
{
    Route voice;
    voice.started(false);
    voice.setCall(true, false);
    check(voice.sent.isEmpty(), "a voice call stays on the earpiece");

    voice.setCall(true, true);
    check(voice.sent == QList<bool>{ true }, "a camera comes on: loudspeaker");
    voice.route("speaker");
    check(voice.speaker(), "confirmed");
    voice.setCall(true, false);
    check(voice.speaker(), "the camera going off changes nothing");
    voice.setSpeaker(false);
    voice.route("earpiece");
    voice.setCall(true, true);
    check(voice.sent.size() == 2, "after the user's choice a camera doesn't move it");

    Route early;
    early.setCall(true, true);
    early.answeredRoute(QStringLiteral("speaker"));
    check(early.sent.isEmpty(), "a camera from the start waits for the earpiece");
    early.route("earpiece");
    check(early.sent == QList<bool>{ true }, "and then moves the call off it");

    Route headset;
    headset.setCall(true, true);
    headset.answeredRoute(QStringLiteral("headset"));
    check(headset.sent.isEmpty(), "a headset stays as it is");
    check(!headset.available(), "and has no button");

    Route failing;
    failing.started(true);
    failing.answeredSend(false, true);
    check(failing.failed == 0, "nobody asked: no notice");
    failing.route("earpiece");
    check(failing.sent.size() == 2, "and no second try by itself");
}

void testEnd()
{
    Route r;
    r.started();
    r.setSpeaker(true);
    r.route("speaker");
    r.setCall(false, false);
    check(r.sent.last() == false, "taken back with the call");
    check(!r.speaker() && !r.available(), "nothing shown after it");

    Route waiting;
    waiting.started();
    waiting.setSpeaker(true);
    waiting.setCall(false, false);
    check(waiting.sent.last() == false, "taken back with the call also while unconfirmed");

    Route untouched;
    untouched.started();
    untouched.setCall(false, false);
    check(untouched.sent.isEmpty(), "nothing to take back from a call left on the earpiece");
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testLightsOnlyWhenConfirmed();
    testRefused();
    testPolicyDoesNotMoveIt();
    testOtherSignalsDoNotCount();
    testNoEarpiece();
    testPreference();
    testEnd();
    if (failures == 0) {
        std::printf("passed\n");
    }
    return failures == 0 ? 0 : 1;
}
