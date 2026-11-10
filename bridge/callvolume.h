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

// The volume keys during a call and a voice message at the ear (ADR 0034). The system ties them
// to a call's volume only while its own call state is active, which the modem or App Support
// set; during ours they changed the ringtone's volume (no media plays as far as the system
// knows), and our streams, in the policy's group "call", follow neither that nor the media
// volume. So while the call class is held and the app is in front, the app asks the policy for
// the volume keys, as a camera or a player does, and turns them into a gain on our own
// playback pipelines. The level is kept for the next call. A gain element of our own, not the
// stream's volume: that one is stored under the role the system's phone calls share. Full only.
class CallVolume : public QObject
{
    Q_OBJECT
public:
    // First called at the start (main.cpp), on the main thread.
    static CallVolume *instance();

    // A pipeline's `volume` element follows from now on, until the call class is let go of. From
    // any thread (the engines link their playback on a streaming thread).
    void follow(GstElement *volume);

    // The top level plays as it comes; the lowest is quiet, not silent, like a phone call's.
    static const int Levels = 9;
    static double gainFor(int level);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onHeld(bool held);
    void updateKeys();

private:
    explicit CallVolume(QObject *parent = nullptr);
    void setLevel(int level);

    std::mutex m_lock;
    QVector<GstElement *> m_elements;
    double m_gain = 1.0;
    int m_level = Levels - 1;
    bool m_held = false;
    bool m_keysAsked = false;
    ResourcePolicy::ResourceSet *m_keys = nullptr;
};

#endif // CALLVOLUME_H
