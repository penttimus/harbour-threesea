// SPDX-License-Identifier: AGPL-3.0-only
#include "mediaroute.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

namespace {

const char *const Service = "org.nemomobile.Route.Manager";
const char *const Path = "/org/nemomobile/Route/Manager";
const char *const Interface = "org.nemomobile.Route.Manager";
// ohm's route plugin (ohm-ext/route.h): the signal comes for inputs and for routes left, too.
const uint OutputType = 1u << 0;
const uint ActiveType = 1u << 27;

} // namespace

MediaRoute::MediaRoute(QObject *parent)
    : QObject(parent)
{
}

void MediaRoute::start()
{
    busListen();
    busQuery();
}

void MediaRoute::busListen()
{
    QDBusConnection::systemBus().connect(QLatin1String(Service), QLatin1String(Path), QLatin1String(Interface),
                                         QStringLiteral("AudioRouteChanged"), this,
                                         SLOT(onRouteChanged(QString,uint)));
}

void MediaRoute::busQuery()
{
    const QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(Service), QLatin1String(Path), QLatin1String(Interface), QStringLiteral("ActiveRoutes"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QString, uint, QString, uint> reply = *call;
        call->deleteLater();
        if (reply.isValid()) {
            answeredRoute(reply.argumentAt<0>());
        } else {
            qWarning("threesea: audio route manager gave no answer");
        }
    });
}

void MediaRoute::answeredRoute(const QString &device)
{
    setOutput(device);
}

void MediaRoute::onRouteChanged(const QString &device, uint type)
{
    if ((type & OutputType) && (type & ActiveType)) {
        setOutput(device);
    }
}

void MediaRoute::setOutput(const QString &device)
{
    if (device.isEmpty()) {
        return;
    }
    const bool headphones = device != QLatin1String("speaker") && device != QLatin1String("earpiece");
    if (headphones != m_headphones) {
        m_headphones = headphones;
        emit headphonesChanged();
    }
}
