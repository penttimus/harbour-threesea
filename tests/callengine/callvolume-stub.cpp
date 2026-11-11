// SPDX-License-Identifier: AGPL-3.0-only
// The loopback test runs without the audio policy and the system's volume: what plays keeps the
// level it comes with.
#include "callvolume.h"

CallVolume *CallVolume::instance()
{
    static CallVolume *self = new CallVolume;
    return self;
}

CallVolume::CallVolume(QObject *parent)
    : QObject(parent)
{
}

void CallVolume::follow(GstElement *)
{
}

double CallVolume::gainFor(uint, uint)
{
    return 1.0;
}

bool CallVolume::eventFilter(QObject *, QEvent *)
{
    return false;
}

void CallVolume::onHeld(bool)
{
}

void CallVolume::updateKeys()
{
}

void CallVolume::onSteps(uint, uint)
{
}
