// SPDX-License-Identifier: AGPL-3.0-only
// What the loudspeaker button says has to be what the audio policy did: a wrong "on" takes the
// ear guard away with the sound still at the ear. And only the user moves a call.
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
    QList<bool> headset;
    int failed = 0;
    Route()
    {
        connect(this, &CallRoute::failed, [this]() { ++failed; });
    }
    using CallRoute::answeredRoute;
    using CallRoute::answeredSend;
    using CallRoute::confirmTimedOut;
    using CallRoute::onRouteChanged;

    void started()
    {
        setCall(true);
        // What it was before the call class was ours, then the policy's move.
        answeredRoute(QStringLiteral("speaker"));
        route("earpiece");
    }
    void route(const char *name) { onRouteChanged(QLatin1String(name), ActiveOutput); }

protected:
    void busListen(bool) override {}
    void busQuery() override {}
    void busSend(bool on) override { sent.append(on); }
    void busSendHeadset(bool on) override { headset.append(on); }
};

void testLightsOnlyWhenConfirmed()
{
    Route r;
    r.setCall(true);
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
    r.setCall(true);
    r.answeredRoute(QStringLiteral("speaker"));
    check(!r.available(), "no earpiece: no button");
    check(!r.speaker(), "no earpiece: not lit either, it is not our doing");
    r.setSpeaker(true);
    check(r.sent.isEmpty(), "no earpiece: nothing sent");
}

void testHeadset()
{
    Route headset;
    headset.setCall(true);
    headset.answeredRoute(QStringLiteral("headset"));
    check(!headset.available(), "a headset has no button");
    headset.setSpeaker(true);
    check(headset.sent.isEmpty(), "and stays as it is");
}

void testBluetoothHeadset()
{
    // The policy leaves our call on the music profile: the hands-free one is asked for, once.
    Route r;
    r.setCall(true);
    r.answeredRoute(QStringLiteral("bluetootha2dp"));
    check(r.headset == QList<bool>{ true }, "on A2DP: the hands-free route is asked for");
    check(!r.available(), "a Bluetooth headset has no button");
    r.route("bluetootha2dp");
    check(r.headset.size() == 1, "a headset without hands-free: not asked again");
    r.route("bluetoothsco");
    check(r.headset.size() == 1 && !r.available(), "on hands-free: nothing more");
    check(r.sent.isEmpty(), "the loudspeaker was never touched");
    r.setCall(false);
    check(r.headset == (QList<bool>{ true, false }), "taken back with the call");

    // Connected during the call, gone, and back.
    Route later;
    later.started();
    check(later.headset.isEmpty(), "no headset: nothing asked");
    later.route("bluetootha2dp");
    later.route("bluetoothsco");
    later.route("earpiece");
    check(later.available(), "headset gone: the earpiece's button is back");
    later.route("bluetootha2dp");
    check(later.headset == (QList<bool>{ true, true }), "asked again when it comes back");

    // The loudspeaker is the user's choice: a headset arriving doesn't take the call away.
    Route loud;
    loud.started();
    loud.setSpeaker(true);
    loud.route("speaker");
    loud.route("bluetootha2dp");
    check(loud.headset.isEmpty(), "on the loudspeaker by choice: not asked");

    Route none;
    none.started();
    none.setCall(false);
    check(none.headset.isEmpty(), "no headset: nothing to take back");
}

void testEnd()
{
    Route r;
    r.started();
    r.setSpeaker(true);
    r.route("speaker");
    r.setCall(false);
    check(r.sent.last() == false, "taken back with the call");
    check(!r.speaker() && !r.available(), "nothing shown after it");

    Route waiting;
    waiting.started();
    waiting.setSpeaker(true);
    waiting.setCall(false);
    check(waiting.sent.last() == false, "taken back with the call also while unconfirmed");

    Route untouched;
    untouched.started();
    untouched.setCall(false);
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
    testHeadset();
    testBluetoothHeadset();
    testEnd();
    if (failures == 0) {
        std::printf("passed\n");
    }
    return failures == 0 ? 0 : 1;
}
