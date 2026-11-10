// SPDX-License-Identifier: AGPL-3.0-only
// GStreamer first: GLib headers use `signals` as a member name, which Qt defines as a macro.
#include <gst/gst.h>

#include "callvolume.h"

#include "callclass.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QSettings>

#include <policy/resource-set.h>

#include <cmath>

namespace {

const double StepDb = 3.0;
const char *const LevelKey = "callVolumeLevel";

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
    m_level = qBound(0, QSettings().value(QLatin1String(LevelKey), Levels - 1).toInt(), Levels - 1);
    m_gain = gainFor(m_level);
    connect(CallClass::instance(), &CallClass::held, this, &CallVolume::onHeld);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, &CallVolume::updateKeys);
    qGuiApp->installEventFilter(this);
}

double CallVolume::gainFor(int level)
{
    return std::pow(10.0, -StepDb * (Levels - 1 - qBound(0, level, Levels - 1)) / 20.0);
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

void CallVolume::setLevel(int level)
{
    level = qBound(0, level, Levels - 1);
    if (level == m_level) {
        return;
    }
    m_level = level;
    QSettings().setValue(QLatin1String(LevelKey), level);
    std::lock_guard<std::mutex> guard(m_lock);
    m_gain = gainFor(level);
    for (GstElement *element : m_elements) {
        g_object_set(element, "volume", m_gain, nullptr);
    }
}

void CallVolume::onHeld(bool held)
{
    m_held = held;
    updateKeys();
    if (!held) {
        std::lock_guard<std::mutex> guard(m_lock);
        for (GstElement *element : m_elements) {
            gst_object_unref(element);
        }
        m_elements.clear();
    }
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
        setLevel(m_level + (key == Qt::Key_VolumeUp ? 1 : -1));
    }
    return true;
}
