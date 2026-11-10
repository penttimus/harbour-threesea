// SPDX-License-Identifier: AGPL-3.0-only
#ifndef CALLVOLUME_H
#define CALLVOLUME_H

#include <QObject>
#include <QVector>

#include <mutex>

typedef struct _GstElement GstElement;

// The volume keys during a call and a voice message at the ear (ADR 0034). The system ties them
// to the call's volume only while its own call state is active, which the modem or App Support
// set; for us they go on changing the media volume, which our streams (the policy's group
// "call") don't follow. So while the call class is held, this follows the media volume's steps
// (PulseAudio's MainVolume2 interface, the one the system's volume bar uses) and turns them into
// a gain on our playback pipelines: the bar the keys bring up is what one hears. A gain element
// of our own, not the stream's volume: that one is stored under the role the system's phone
// calls share. Full only.
class CallVolume : public QObject
{
    Q_OBJECT
public:
    static CallVolume *instance();

    // A pipeline's `volume` element follows from now on, until the call class is let go of. From
    // any thread (the engines link their playback on a streaming thread).
    void follow(GstElement *volume);

    // The top step plays as it comes; the lowest is quiet, not silent, like a phone call's.
    static double gainFor(uint stepCount, uint currentStep);

private slots:
    void onHeld(bool held);
    void onSteps(uint stepCount, uint currentStep);

private:
    explicit CallVolume(QObject *parent = nullptr);
    void apply(double gain);

    std::mutex m_lock;
    QVector<GstElement *> m_elements;
    double m_gain = 1.0;
    bool m_listening = false;
};

#endif // CALLVOLUME_H
