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
#include <QGuiApplication>
#include <QKeyEvent>
#include <QStandardPaths>

#include <policy/resource-set.h>

#include <cmath>

namespace {

// PulseAudio's own bus (no daemon: a socket in its runtime folder, which Sailjail's Audio
// permission shows us), as the system's volume bar talks to it.
const char *const Connection = "threesea-pulse";
const char *const CorePath = "/org/pulseaudio/core1";
const char *const CoreInterface = "org.PulseAudio.Core1";
const char *const VolumePath = "/com/meego/mainvolume2";
const char *const VolumeInterface = "com.Meego.MainVolume2";
const char *const Properties = "org.freedesktop.DBus.Properties";
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
    // Never deleted, like CallClass: pipelines register from their own threads.
    static CallVolume *self = new CallVolume;
    return self;
}

CallVolume::CallVolume(QObject *parent)
    : QObject(parent)
{
    connect(CallClass::instance(), &CallClass::held, this, &CallVolume::onHeld);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, &CallVolume::updateKeys);
    qGuiApp->installEventFilter(this);
}

double CallVolume::gainFor(uint stepCount, uint currentStep)
{
    if (stepCount < 2) {
        return 1.0;
    }
    if (currentStep == 0) {
        return 0.0;
    }
    if (stepCount == 2) {
        return 1.0;
    }
    const double position = (qMin(currentStep, stepCount - 1) - 1) / double(stepCount - 2);
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

void CallVolume::apply()
{
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_gain = gainFor(m_stepCount, m_step);
        for (GstElement *element : m_elements) {
            g_object_set(element, "volume", m_gain, nullptr);
        }
    }
    const bool silent = m_held && m_stepCount >= 2 && m_step == 0;
    if (silent != m_silent) {
        m_silent = silent;
        emit silentChanged();
    }
}

void CallVolume::onHeld(bool held)
{
    m_held = held;
    listen(held);
    updateKeys();
    if (!held) {
        {
            std::lock_guard<std::mutex> guard(m_lock);
            for (GstElement *element : m_elements) {
                gst_object_unref(element);
            }
            m_elements.clear();
        }
        if (m_silent) {
            m_silent = false;
            emit silentChanged();
        }
    }
}

void CallVolume::listen(bool on)
{
    if (on == m_listening) {
        return;
    }
    if (!on) {
        QDBusConnection::disconnectFromPeer(QLatin1String(Connection));
        m_listening = false;
        return;
    }
    QDBusConnection bus = QDBusConnection::connectToPeer(busAddress(), QLatin1String(Connection));
    if (!bus.isConnected()) {
        qWarning("threesea: no connection to the system's volume, the call plays as it comes");
        QDBusConnection::disconnectFromPeer(QLatin1String(Connection));
        m_stepCount = 0;
        apply();
        return;
    }
    m_listening = true;
    // PulseAudio sends a signal only to those who asked for it.
    QDBusMessage ask = QDBusMessage::createMethodCall(QString(), QLatin1String(CorePath),
                                                      QLatin1String(CoreInterface),
                                                      QStringLiteral("ListenForSignal"));
    ask << QStringLiteral("com.Meego.MainVolume2.StepsUpdated") << QVariant::fromValue(QList<QDBusObjectPath>());
    bus.asyncCall(ask);
    bus.connect(QString(), QLatin1String(VolumePath), QLatin1String(VolumeInterface),
                QStringLiteral("StepsUpdated"), this, SLOT(onSteps(uint,uint)));

    QDBusMessage all = QDBusMessage::createMethodCall(QString(), QLatin1String(VolumePath),
                                                      QLatin1String(Properties), QStringLiteral("GetAll"));
    all << QLatin1String(VolumeInterface);
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(all), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QVariantMap> reply = *call;
        call->deleteLater();
        if (!m_listening) {
            return;
        }
        if (!reply.isValid()) {
            qWarning("threesea: the system's volume gave no answer");
            return;
        }
        const QVariantMap values = reply.value();
        m_safeStep = values.value(QStringLiteral("HighVolumeStep")).toUInt();
        onSteps(values.value(QStringLiteral("StepCount")).toUInt(),
                values.value(QStringLiteral("CurrentStep")).toUInt());
    });
}

void CallVolume::onSteps(uint stepCount, uint currentStep)
{
    if (m_listening) {
        m_stepCount = stepCount;
        m_step = currentStep;
        apply();
    }
}

// One step of the media volume, heard at once; the system's signal confirms it.
void CallVolume::step(int by)
{
    if (!m_listening || m_stepCount < 2) {
        return;
    }
    uint top = m_stepCount - 1;
    if (m_safeStep > 0 && m_safeStep < top) {
        top = m_safeStep;
    }
    const uint wanted = by > 0 ? qMin(m_step + 1, qMax(top, m_step)) : (m_step > 0 ? m_step - 1 : 0);
    if (wanted == m_step) {
        return;
    }
    m_step = wanted;
    apply();
    QDBusMessage set = QDBusMessage::createMethodCall(QString(), QLatin1String(VolumePath),
                                                      QLatin1String(Properties), QStringLiteral("Set"));
    set << QLatin1String(VolumeInterface) << QStringLiteral("CurrentStep")
        << QVariant::fromValue(QDBusVariant(QVariant::fromValue(wanted)));
    QDBusConnection(QLatin1String(Connection)).asyncCall(set);
}

// The keys are ours only while the call or the message is in front: behind another app they
// stay the system's.
void CallVolume::updateKeys()
{
    const bool want = m_held && QGuiApplication::applicationState() == Qt::ApplicationActive;
    if (want == m_keysAsked) {
        return;
    }
    m_keysAsked = want;
    if (!m_keys) {
        m_keys = new ResourcePolicy::ResourceSet(QStringLiteral("call"), this, false, false);
        m_keys->addResource(ResourcePolicy::ScaleButtonType);
        connect(m_keys, &ResourcePolicy::ResourceSet::resourcesDenied, this, []() {
            qWarning("threesea: the volume keys are not ours, the call keeps its volume");
        });
    }
    if (want) {
        m_keys->acquire();
    } else {
        m_keys->release();
    }
}

bool CallVolume::eventFilter(QObject *, QEvent *event)
{
    if (!m_keysAsked || (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease)) {
        return false;
    }
    const int key = static_cast<QKeyEvent *>(event)->key();
    if (key != Qt::Key_VolumeUp && key != Qt::Key_VolumeDown) {
        return false;
    }
    if (event->type() == QEvent::KeyPress) {
        step(key == Qt::Key_VolumeUp ? 1 : -1);
    }
    return true;
}
