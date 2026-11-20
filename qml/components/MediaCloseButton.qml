// SPDX-License-Identifier: AGPL-3.0-only
import QtQuick 2.6
import Sailfish.Silica 1.0

Rectangle {
    anchors.top: parent.top
    width: parent.width
    height: dismiss.y + dismiss.height + Theme.paddingLarge
    visible: opacity > 0
    opacity: enabled ? 1.0 : 0.0
    Behavior on opacity { FadeAnimation { } }
    gradient: Gradient {
        GradientStop { position: 0.0; color: Theme.rgba("black", Theme.opacityOverlay) }
        GradientStop { position: 1.0; color: "transparent" }
    }

    CutoutInset { id: cutout }

    IconButton {
        id: dismiss
        anchors {
            top: parent.top
            topMargin: cutout.inset + Theme.paddingLarge
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
        }
        palette.colorScheme: Theme.LightOnDark
        icon.source: "image://theme/icon-m-dismiss"
        onClicked: pageStack.pop()
    }
}
