// SPDX-License-Identifier: AGPL-3.0-only
#include "callclass.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <policy/resource-set.h>

namespace {

const char *const Service = "org.nemomobile.Route.Manager";
const char *const Path = "/org/nemomobile/Route/Manager";
const char *const Interface = "org.nemomobile.Route.Manager";
const char *const Speaker = "speaker";
// ohm's route plugin (ohm-ext/route.h): the signal comes for inputs and for routes left, too.
const uint OutputType = 1u << 0;
const uint ActiveType = 1u << 27;
const int LetGoMs = 600;
// On the phone the grant and the route came within 10 ms.
const int WaitMs = 1500;

} // namespace

CallClass *CallClass::instance()
{
    // Never deleted: the players let go of it in their destructors, whenever those run.
    static CallClass *self = new CallClass;
    return self;
}

CallClass::CallClass(QObject *parent)
    : QObject(parent)
{
    m_letGo.setSingleShot(true);
    m_letGo.setInterval(LetGoMs);
    connect(&m_letGo, &QTimer::timeout, this, [this]() {
        if (m_holders.isEmpty()) {
            release();
        }
    });
    m_wait.setSingleShot(true);
    m_wait.setInterval(WaitMs);
    connect(&m_wait, &QTimer::timeout, this, &CallClass::timedOut);
}

void CallClass::hold(const void *who)
{
    m_holders.insert(who);
    m_letGo.stop();
    if (m_state != Idle) {
        return;
    }
    m_state = Waiting;
    m_granted = false;
    m_output.clear();
    m_wait.start();
    emit held(true);
    busListen(true);
    policyAcquire();
}

void CallClass::drop(const void *who)
{
    if (m_holders.remove(who) && m_holders.isEmpty() && m_state != Idle) {
        m_letGo.start();
    }
}

void CallClass::release()
{
    if (m_state == Idle) {
        return;
    }
    m_state = Idle;
    m_letGo.stop();
    m_wait.stop();
    busListen(false);
    policyRelease();
    emit held(false);
}

void CallClass::policyAcquire()
{
    if (!m_set) {
        m_set = new ResourcePolicy::ResourceSet(QStringLiteral("call"), this, true, false);
        // Our streams are the call's: the policy corks what it takes for a player's.
        ResourcePolicy::AudioResource *audio = new ResourcePolicy::AudioResource(QStringLiteral("call"));
        audio->setProcessID(static_cast<quint32>(QCoreApplication::applicationPid()));
        audio->setStreamTag(QStringLiteral("media.name"), QStringLiteral("*"));
        m_set->addResourceObject(audio);
        m_set->addResource(ResourcePolicy::AudioRecorderType);
        connect(m_set, &ResourcePolicy::ResourceSet::resourcesGranted, this, &CallClass::onGranted);
        connect(m_set, &ResourcePolicy::ResourceSet::resourcesDenied, this, &CallClass::onRefused);
        connect(m_set, &ResourcePolicy::ResourceSet::lostResources, this, &CallClass::onRefused);
        connect(m_set, &ResourcePolicy::ResourceSet::resourcesReleasedByManager, this, &CallClass::onRefused);
    }
    if (!m_set->acquire()) {
        qWarning("threesea: the audio policy's call class could not be asked for");
        onRefused();
    }
}

void CallClass::policyRelease()
{
    if (m_set) {
        m_set->release();
    }
}

void CallClass::busListen(bool on)
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

void CallClass::busQuery()
{
    const QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(Service), QLatin1String(Path), QLatin1String(Interface), QStringLiteral("ActiveRoutes"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QString, uint, QString, uint> reply = *call;
        call->deleteLater();
        if (reply.isValid()) {
            answeredRoute(reply.argumentAt<0>());
        }
    });
}

void CallClass::onGranted()
{
    if (m_state == Idle) {
        return;
    }
    m_granted = true;
    // The route may have been off the loudspeaker before (a headset): no signal comes then.
    busQuery();
}

// Refused, or taken away (a phone call).
void CallClass::onRefused()
{
    if (m_state == Idle) {
        return;
    }
    qWarning("threesea: the audio policy's call class is not ours");
    m_granted = false;
    m_wait.stop();
    setState(Loudspeaker);
}

void CallClass::answeredRoute(const QString &device)
{
    if (m_state != Idle) {
        m_output = device;
        settle();
    }
}

void CallClass::onRouteChanged(const QString &device, uint type)
{
    if (m_state != Idle && (type & OutputType) && (type & ActiveType)) {
        m_output = device;
        settle();
    }
}

// Once the class is ours the route says where the sound is. While waiting, the loudspeaker is
// only believed at the timeout: the route we asked about may be the one from before the grant.
void CallClass::settle()
{
    if (!m_granted || m_output.isEmpty()) {
        return;
    }
    const bool speaker = m_output == QLatin1String(Speaker);
    if (!speaker) {
        m_wait.stop();
        setState(Earpiece);
    } else if (m_state != Waiting) {
        setState(Loudspeaker);
    }
}

void CallClass::timedOut()
{
    if (m_state == Waiting) {
        qWarning("threesea: the audio policy did not move the sound to the earpiece");
        setState(Loudspeaker);
    }
}

void CallClass::setState(State state)
{
    if (state != m_state) {
        m_state = state;
        emit settled(state == Earpiece);
    }
}
