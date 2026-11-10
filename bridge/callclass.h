// SPDX-License-Identifier: AGPL-3.0-only
#ifndef CALLCLASS_H
#define CALLCLASS_H

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

namespace ResourcePolicy {
class ResourceSet;
}

// What opens the earpiece to us (ADR 0034): while a call or a voice message at the ear runs, the
// app holds a resource set of the audio policy's class "call", as Jolla's own call manager does.
// The policy keeps the earpiece shut "unless there is a call or the route is preferred"
// (policy-settings-common, audio_route_rules.pl); the streams' role alone opens nothing. While
// it is held the system takes it for a call: other apps' music pauses. The policy lets go of it
// by itself when the process ends. Full only.
class CallClass : public QObject
{
    Q_OBJECT
public:
    static CallClass *instance();

    // `who` needs the earpiece from now on, or no longer. Letting go waits a moment, so that one
    // voice message after another doesn't swing the sound to the loudspeaker in between.
    void hold(const void *who);
    void drop(const void *who);
    // The policy gave us the class and the sound is off the loudspeaker.
    bool earpiece() const { return m_state == Earpiece; }
    // It was asked for and the loudspeaker has the sound: no `settled` will come for a new holder.
    bool refused() const { return m_state == Loudspeaker; }

signals:
    // The class is asked for (a call or a voice message at the ear begins), or let go of.
    void held(bool held);
    // After hold(): the sound is at the ear (the earpiece, or a headset), or it didn't get there
    // (no earpiece on this device, the policy refused): the loudspeaker has it.
    void settled(bool earpiece);

protected:
    explicit CallClass(QObject *parent = nullptr);
    // The policy and the bus, apart from the logic.
    virtual void policyAcquire();
    virtual void policyRelease();
    virtual void busListen(bool on);
    virtual void busQuery();

    void answeredRoute(const QString &device);
    void timedOut();

protected slots:
    void onGranted();
    void onRefused();
    void onRouteChanged(const QString &device, uint type);

private:
    enum State { Idle, Waiting, Earpiece, Loudspeaker };

    void release();
    void settle();
    void setState(State state);

    QSet<const void *> m_holders;
    State m_state = Idle;
    bool m_granted = false;
    QString m_output;
    ResourcePolicy::ResourceSet *m_set = nullptr;
    QTimer m_letGo;
    QTimer m_wait;
};

#endif // CALLCLASS_H
