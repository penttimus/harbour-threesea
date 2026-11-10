// SPDX-License-Identifier: AGPL-3.0-only
#include "callroute.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QSettings>

namespace {

// Sailjail's Audio permission (part of Microphone) lets us talk to it. The names are the
// policy's (policy-settings-common), seen on a phone.
const char *const Service = "org.nemomobile.Route.Manager";
const char *const Path = "/org/nemomobile/Route/Manager";
const char *const Interface = "org.nemomobile.Route.Manager";
const char *const Speaker = "speaker";
const char *const Earpiece = "earpiece";
// ohm's route plugin (ohm-ext/route.h). The change signal comes for inputs and for the routes
// left as well; Prefer wants the output bit.
const uint OutputType = 1u << 0;
const uint ActiveType = 1u << 27;
// The manager's signals come at once if it does what it was asked.
const int ConfirmMs = 3000;
// Set while the loudspeaker is preferred on our word (see recover).
const char *const PreferredKey = "callSpeakerPreferred";

QDBusMessage routeCall(const char *method)
{
    return QDBusMessage::createMethodCall(QLatin1String(Service), QLatin1String(Path), QLatin1String(Interface),
                                          QLatin1String(method));
}

QDBusMessage preferSpeaker(bool on)
{
    QDBusMessage message = routeCall("Prefer");
    message << QLatin1String(Speaker) << OutputType << uint(on ? 1 : 0);
    return message;
}

} // namespace

CallRoute::CallRoute(QObject *parent)
    : QObject(parent)
{
    m_confirm.setSingleShot(true);
    m_confirm.setInterval(ConfirmMs);
    connect(&m_confirm, &QTimer::timeout, this, &CallRoute::confirmTimedOut);
}

CallRoute::~CallRoute()
{
    // Waits: the process is ending, and the loudspeaker must not stay preferred after it.
    if (m_ours || (m_pending && m_wanted)) {
        QDBusConnection::systemBus().call(preferSpeaker(false), QDBus::Block, 1000);
        QSettings().remove(QLatin1String(PreferredKey));
    }
}

void CallRoute::recover()
{
    if (QSettings().contains(QLatin1String(PreferredKey))) {
        qWarning("threesea: taking back the loudspeaker preference of a call that did not end");
        busSend(false);
    }
}

void CallRoute::busListen(bool on)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    const QString service = QLatin1String(Service), path = QLatin1String(Path), iface = QLatin1String(Interface);
    if (on) {
        bus.connect(service, path, iface, QStringLiteral("AudioRouteChanged"), this,
                    SLOT(onRouteChanged(QString,uint)));
    } else {
        bus.disconnect(service, path, iface, QStringLiteral("AudioRouteChanged"), this,
                       SLOT(onRouteChanged(QString,uint)));
    }
}

void CallRoute::busQuery()
{
    auto *routes = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(routeCall("ActiveRoutes")),
                                               this);
    connect(routes, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QString, uint, QString, uint> reply = *call;
        call->deleteLater();
        if (reply.isValid()) {
            answeredRoute(reply.argumentAt<0>());
        } else {
            qWarning("threesea: audio route manager gave no answer");
        }
    });
}

void CallRoute::busSend(bool on)
{
    // Written before asking and removed after taking back: a crash in between leaves the mark.
    if (on) {
        QSettings().setValue(QLatin1String(PreferredKey), true);
    }
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(preferSpeaker(on)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, on](QDBusPendingCallWatcher *call) {
        const bool ok = !call->isError();
        call->deleteLater();
        if (!on && ok) {
            QSettings().remove(QLatin1String(PreferredKey));
        }
        answeredSend(ok, on);
    });
}

// The button is there while the sound is on the earpiece, or on the loudspeaker by our doing.
void CallRoute::show()
{
    const bool available = m_running && (m_ours || m_pending || m_output == QLatin1String(Earpiece));
    const bool speaker = m_running && m_ours;
    if (available != m_available || speaker != m_speaker) {
        m_available = available;
        m_speaker = speaker;
        emit changed();
    }
}

void CallRoute::setCall(bool running, bool preferSpeaker)
{
    if (running != m_running) {
        m_running = running;
        if (!running) {
            busListen(false);
            if (m_ours || (m_pending && m_wanted)) {
                busSend(false);
            }
            m_confirm.stop();
            m_pending = false;
            m_ours = false;
            m_output.clear();
            m_lastPrefer = false;
            m_preferSpeaker = false;
            show();
            return;
        }
        m_chosen = false;
        busListen(true);
        busQuery();
    }
    if (!running) {
        return;
    }
    if (preferSpeaker != m_lastPrefer) {
        m_lastPrefer = preferSpeaker;
        m_preferSpeaker = preferSpeaker;
        applyPreference();
    }
}

void CallRoute::answeredRoute(const QString &device)
{
    if (m_running) {
        m_output = device;
        settle();
        show();
        applyPreference();
    }
}

// Only from the earpiece: a headset stays as it is.
void CallRoute::applyPreference()
{
    if (m_preferSpeaker && !m_chosen && !m_ours && !m_pending && m_output == QLatin1String(Earpiece)) {
        m_preferSpeaker = false;
        request(true, false);
    }
}

void CallRoute::setSpeaker(bool on)
{
    if (!m_running || !m_available || m_pending || on == m_speaker) {
        return;
    }
    m_chosen = true;
    request(on, true);
}

void CallRoute::request(bool on, bool byUser)
{
    m_pending = true;
    m_wanted = on;
    m_byUser = byUser;
    m_confirm.start();
    busSend(on);
}

// The route itself has to say it: off the earpiece when asked on, off the loudspeaker when
// asked off (to the earpiece, or to a headset that came in the meantime).
void CallRoute::settle()
{
    if (!m_pending || m_output.isEmpty()) {
        return;
    }
    const bool done = m_wanted ? m_output != QLatin1String(Earpiece) : m_output != QLatin1String(Speaker);
    if (!done) {
        return;
    }
    m_confirm.stop();
    m_pending = false;
    m_ours = m_wanted;
}

void CallRoute::answeredSend(bool ok, bool on)
{
    if (!ok && m_pending && m_wanted == on) {
        qWarning("threesea: audio route manager refused the loudspeaker change");
        giveUp();
    }
}

void CallRoute::confirmTimedOut()
{
    if (m_pending) {
        qWarning("threesea: audio route manager did not confirm the loudspeaker change");
        giveUp();
    }
}

// Back to what it was: no preference of ours stays behind with the sound still on the earpiece.
void CallRoute::giveUp()
{
    m_confirm.stop();
    m_pending = false;
    if (m_wanted) {
        m_ours = false;
        busSend(false);
    }
    show();
    if (m_byUser) {
        emit failed();
    }
}

void CallRoute::onRouteChanged(const QString &device, uint type)
{
    if (m_running && (type & OutputType) && (type & ActiveType)) {
        m_output = device;
        settle();
        show();
        applyPreference();
    }
}
