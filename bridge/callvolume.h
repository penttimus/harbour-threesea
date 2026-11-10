// SPDX-License-Identifier: AGPL-3.0-only
#ifndef CALLVOLUME_H
#define CALLVOLUME_H

#include <QObject>
#include <QVector>

#include <mutex>

typedef struct _GstElement GstElement;
namespace ResourcePolicy {
class ResourceSet;
}

// The volume keys during a call and a voice message at the ear (ADR 0034): they change the media
// volume, and our playback follows it (user decision 2026-11-10: one volume, the media's).
//
// The system does neither by itself. It ties the keys to a call's volume only while its own call
// state is active, which the modem or App Support set; during ours they changed the ringtone's
// volume (to the system no media plays). And our streams, in the policy's group "call", don't
// follow the media volume. So while the call class is held:
//  - with the app in front, it asks the policy for the volume keys, as a camera or a player
//    does, and moves the media volume's step itself (PulseAudio's MainVolume2 interface, the one
//    the system's volume bar uses);
//  - the step, whoever moved it, becomes a gain on our own playback pipelines. A gain element of
//    our own, not the stream's volume: that one is stored under the role the system's phone
//    calls share.
// Full only.
class CallVolume : public QObject
{
    Q_OBJECT
public:
    // First called at the start (main.cpp), on the main thread.
    static CallVolume *instance();

    // A pipeline's `volume` element follows from now on, until the call class is let go of. From
    // any thread (the engines link their playback on a streaming thread). `silentAtZero`: a voice
    // message is silent at media volume zero, as media is. A call is not (the default): it stays
    // quiet there like a phone call's lowest step, or whoever keeps the media volume at zero
    // would take every call for a broken one.
    void follow(GstElement *volume, bool silentAtZero = false);

    // The top step plays as it comes, the lowest 24 dB below it.
    static double gainFor(uint stepCount, uint currentStep);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onHeld(bool held);
    void updateKeys();
    void onSteps(uint stepCount, uint currentStep);

private:
    explicit CallVolume(QObject *parent = nullptr);
    void listen(bool on);
    void step(int by);
    void apply();

    std::mutex m_lock;
    struct Follower {
        GstElement *element;
        bool silentAtZero;
    };
    double gainOf(const Follower &follower) const;
    QVector<Follower> m_elements;
    double m_gain = 1.0;
    // The media volume as last heard of. Without the system's answer: the top, as the sound comes.
    uint m_stepCount = 0;
    uint m_step = 0;
    // On a headset the system warns above this step; the keys stop there. 0: no such step.
    uint m_safeStep = 0;
    bool m_held = false;
    bool m_listening = false;
    bool m_keysAsked = false;
    ResourcePolicy::ResourceSet *m_keys = nullptr;
};

#endif // CALLVOLUME_H
