// SPDX-License-Identifier: AGPL-3.0-only
#ifndef CALLROUTE_H
#define CALLROUTE_H

#include <QObject>
#include <QString>
#include <QTimer>

// Earpiece or loudspeaker during a call (`callRoute`). The call is on the earpiece because the
// app holds the audio policy's call class (CallClass, ADR 0034); the loudspeaker is the route
// manager's preferred device for as long as the button is lit, which is what the system's own
// loudspeaker button comes to in the policy. (Its `speaker` feature is only offered during a
// phone call.) A connected headset is routed by the policy itself: no button then. Only talks
// to the bus during a call, so never in Lite.
//
// A preference outlives the process: it is taken back when the call ends, when the app closes,
// and by `recover` at the next start after a crash.
//
// `speaker` says what the manager confirmed, not what was asked for: the button, and whether a
// call at the ear goes dark, depend on it.
class CallRoute : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool speaker READ speaker WRITE setSpeaker NOTIFY changed)
public:
    explicit CallRoute(QObject *parent = nullptr);
    ~CallRoute() override;

    bool available() const { return m_available; }
    bool speaker() const { return m_speaker; }
    void setSpeaker(bool on);
    // A call runs or not. `preferSpeaker`: it is held away from the ear now (a camera is on). Each
    // time that begins the call goes to the loudspeaker if it would be on the earpiece, unless
    // the user chose in this call.
    void setCall(bool running, bool preferSpeaker);
    // At the start: takes back a preference that a crash during a call left behind.
    void recover();

signals:
    void changed();
    // What the user asked for didn't happen: the sound stays where it was.
    void failed();

protected:
    // The bus, apart from the logic.
    virtual void busListen(bool on);
    virtual void busQuery();
    virtual void busSend(bool on);

    void answeredRoute(const QString &device);
    void answeredSend(bool ok, bool on);
    void confirmTimedOut();

protected slots:
    void onRouteChanged(const QString &device, uint type);

private:
    void applyPreference();
    void request(bool on, bool byUser);
    void settle();
    void giveUp();
    void show();

    bool m_running = false;
    bool m_lastPrefer = false;
    bool m_preferSpeaker = false;
    // The user chose: nothing else changes the route of this call any more.
    bool m_chosen = false;
    bool m_available = false;
    bool m_speaker = false;
    // We made the loudspeaker the preferred device, so it is ours to take back.
    bool m_ours = false;
    // A request waits for the manager to confirm it.
    bool m_pending = false;
    bool m_wanted = false;
    bool m_byUser = false;
    QString m_output;
    QTimer m_confirm;
};

#endif // CALLROUTE_H
