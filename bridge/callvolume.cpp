// SPDX-License-Identifier: AGPL-3.0-only
// GStreamer first: GLib headers use `signals` as a member name, which Qt defines as a macro.
#include <gst/gst.h>

#include "callvolume.h"

#include "callclass.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QStandardPaths>

#include <cmath>

namespace {

// PulseAudio's own bus (no daemon: a socket in its runtime folder, which Sailjail's Audio
// permission shows us), as the system's volume bar talks to it.
const char *const Connection = "threesea-pulse";
const char *const CorePath = "/org/pulseaudio/core1";
const char *const CoreInterface = "org.PulseAudio.Core1";
const char *const VolumePath = "/com/meego/mainvolume2";
const char *const VolumeInterface = "com.Meego.MainVolume2";
// From the top step down to the lowest.
const double RangeDb = 24.0;

QString busAddress()
{
    const QByteArray given = qgetenv("PULSE_DBUS_SERVER");
    if (!given.isEmpty()) {
        return QString::fromLocal8Bit(given);
    }
    return QStringLiteral("unix:path=%1/pulse/dbus-socket")
        .arg(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation));
}

} // namespace

CallVolume *CallVolume::instance()
{
    // Never deleted, like CallClass: pipelines register from their own threads. First called at
    // the start (main.cpp), so that it is there when the class is first held.
    static CallVolume *self = new CallVolume;
    return self;
}

CallVolume::CallVolume(QObject *parent)
    : QObject(parent)
{
    connect(CallClass::instance(), &CallClass::held, this, &CallVolume::onHeld);
}

double CallVolume::gainFor(uint stepCount, uint currentStep)
{
    if (stepCount < 2) {
        return 1.0;
    }
    const double position = qMin(currentStep, stepCount - 1) / double(stepCount - 1);
    return std::pow(10.0, -RangeDb * (1.0 - position) / 20.0);
}

void CallVolume::follow(GstElement *volume)
{
    if (!volume) {
        return;
    }
    std::lock_guard<std::mutex> guard(m_lock);
    // Those of pipelines that are gone (they have no parent any more).
    for (int i = m_elements.size() - 1; i >= 0; --i) {
        if (!GST_OBJECT_PARENT(m_elements.at(i))) {
            gst_object_unref(m_elements.at(i));
            m_elements.remove(i);
        }
    }
    g_object_set(volume, "volume", m_gain, nullptr);
    m_elements.append(GST_ELEMENT(gst_object_ref(volume)));
}

void CallVolume::apply(double gain)
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_gain = gain;
    for (GstElement *element : m_elements) {
        g_object_set(element, "volume", gain, nullptr);
    }
}

void CallVolume::onHeld(bool held)
{
    if (!held) {
        if (m_listening) {
            QDBusConnection::disconnectFromPeer(QLatin1String(Connection));
            m_listening = false;
        }
        std::lock_guard<std::mutex> guard(m_lock);
        for (GstElement *element : m_elements) {
            gst_object_unref(element);
        }
        m_elements.clear();
        return;
    }
    if (m_listening) {
        return;
    }
    QDBusConnection bus = QDBusConnection::connectToPeer(busAddress(), QLatin1String(Connection));
    if (!bus.isConnected()) {
        // The keys change nothing then, as before.
        qWarning("threesea: no connection to the system's volume, the call keeps its own");
        QDBusConnection::disconnectFromPeer(QLatin1String(Connection));
        return;
    }
    m_listening = true;
    // PulseAudio sends a signal only to those who asked for it.
    QDBusMessage listen = QDBusMessage::createMethodCall(QString(), QLatin1String(CorePath),
                                                         QLatin1String(CoreInterface),
                                                         QStringLiteral("ListenForSignal"));
    listen << QStringLiteral("com.Meego.MainVolume2.StepsUpdated")
           << QVariant::fromValue(QList<QDBusObjectPath>());
    bus.asyncCall(listen);
    bus.connect(QString(), QLatin1String(VolumePath), QLatin1String(VolumeInterface),
                QStringLiteral("StepsUpdated"), this, SLOT(onSteps(uint,uint)));

    QDBusMessage all = QDBusMessage::createMethodCall(QString(), QLatin1String(VolumePath),
                                                      QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("GetAll"));
    all << QLatin1String(VolumeInterface);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(all), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QVariantMap> reply = *call;
        call->deleteLater();
        if (reply.isValid() && m_listening) {
            const QVariantMap values = reply.value();
            onSteps(values.value(QStringLiteral("StepCount")).toUInt(),
                    values.value(QStringLiteral("CurrentStep")).toUInt());
        } else if (!reply.isValid()) {
            qWarning("threesea: the system's volume gave no answer");
        }
    });
}

void CallVolume::onSteps(uint stepCount, uint currentStep)
{
    if (m_listening) {
        apply(gainFor(stepCount, currentStep));
    }
}
