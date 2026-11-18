// SPDX-License-Identifier: AGPL-3.0-only
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../components"
import "../components/Format.js" as Fmt

CoverBackground {
    id: cover

    // The cover shows: the app was left, not only its focus lost (bridge/voicerecorder.h).
    onStatusChanged: if (status === Cover.Active) voiceRecorder.appLeft()

    // A call: who and its state or running time, with answer/decline or hang up (Jolla's phone
    // app's cover). A 1:1 call that rings or runs comes first: it takes the microphone from a
    // group call when answered.
    readonly property bool inGroupCall: calls.state === "idle" && groupCalls.state !== "idle"
                                        && groupCalls.state !== "ended"
    readonly property bool inCall: calls.state !== "idle" || inGroupCall
    property int seconds: 0

    function callStart() {
        if (!inGroupCall) return calls.activeSince
        // Since the SFU started it, else since we joined, like the group call page.
        var startedAt = groupCalls.call(groupCalls.conversation).startedAt
        return startedAt > 0 ? Math.min(startedAt, groupCalls.activeSince) : groupCalls.activeSince
    }

    Timer {
        running: (calls.state === "active" || (cover.inGroupCall && groupCalls.state === "connected"))
                 && cover.status === Cover.Active
        interval: 1000
        repeat: true
        triggeredOnStart: true
        onTriggered: cover.seconds = Math.max(0, Math.floor((Date.now() - cover.callStart()) / 1000))
    }

    // The cover as a sea (the launcher icon's picture): the count up left, who waits on the wave
    // under it, the actions on the last wave. The water rises a row for every chat listed.
    readonly property bool listsChats: !cover.inCall && backend.unreadCount > 0 && !window.locked
                                       && window.appSettings.disableScreenshots !== true
    // "Show messages on the cover" (Privacy): each chat's last message under its name.
    readonly property bool previews: backend.coverPreviews
    readonly property real rowHeight: nameMetrics.height + (previews ? previewMetrics.height : 0)
    readonly property real waveHeight: Theme.paddingSmall
    // The actions' wave, and the list's last row just above it.
    readonly property real actionsY: height - Theme.itemSizeSmall
    readonly property real listBottom: actionsY - waveHeight
    // Under the count: both upper waves, then the rows.
    readonly property real seaGap: Theme.paddingLarge + Theme.paddingMedium + waveHeight
    // The first wave may come up to the count's digits: the huge font's line is much taller.
    readonly property int rowsMax: Math.max(1, Math.min(previews ? 3 : 5, Math.floor(
        (listBottom - (head.y + count.baselineOffset + waveHeight) - seaGap)
        / Math.max(1, rowHeight))))
    readonly property var waiting: listsChats ? backend.unreadChats.slice(0, rowsMax) : []
    // The water's level in rows: it glides while the cover is seen and is in place when it is not.
    property real tide: waiting.length
    Behavior on tide {
        enabled: cover.status === Cover.Active
        NumberAnimation { duration: 400; easing.type: Easing.InOutQuad }
    }

    FontMetrics {
        id: nameMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSmall
    }
    FontMetrics {
        id: previewMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeExtraSmall
    }

    CoverSea {
        id: sea
        anchors.fill: parent
        level: cover.tide
        emulator: backend.emulator
        amplitude: cover.waveHeight
        thirdY: cover.actionsY
        secondY: cover.listBottom - cover.tide * cover.rowHeight - Theme.paddingMedium - cover.waveHeight
        firstY: secondY - Theme.paddingLarge
    }

    Column {
        visible: cover.inCall
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            // "Hide content on the cover" (Privacy): only the call's state.
            visible: window.appSettings.disableScreenshots !== true
                     && !(cover.inGroupCall && window.locked)
            text: cover.inGroupCall ? window.groupCallName : window.callName
            color: Theme.highlightColor
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            text: cover.inGroupCall && groupCalls.state !== "connected"
                  ? qsTrId("threesea-la-group-call-joining")
                  : cover.inGroupCall && groupCalls.participants.length === 0
                  ? qsTrId("threesea-la-group-call-waiting")
                  : calls.state === "active" || cover.inGroupCall
                  ? Math.floor(cover.seconds / 60) + ":" + ("0" + (cover.seconds % 60)).slice(-2)
                  : calls.state === "incoming"
                    ? qsTrId("threesea-la-call-incoming")
                    : calls.state === "ringing"
                      ? qsTrId("threesea-la-call-ringing")
                      : calls.state === "connecting"
                        ? qsTrId("threesea-la-call-connecting")
                        : calls.state === "ended"
                          ? qsTrId("threesea-la-call-ended")
                          : qsTrId("threesea-la-call-calling")
        }
    }

    CoverActionList {
        enabled: calls.state === "incoming"
        CoverAction {
            iconSource: "image://theme/icon-cover-reject"
            onTriggered: calls.reject()
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-answer"
            onTriggered: {
                calls.accept()
                window.activate()
            }
        }
    }

    CoverActionList {
        enabled: calls.state !== "idle" && calls.state !== "incoming" && calls.state !== "ended"
        CoverAction {
            iconSource: calls.muted ? "image://theme/icon-cover-unmute" : "image://theme/icon-cover-mute"
            onTriggered: calls.muted = !calls.muted
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-hangup"
            onTriggered: calls.hangUp()
        }
    }

    CoverActionList {
        enabled: cover.inGroupCall
        CoverAction {
            iconSource: groupCalls.muted ? "image://theme/icon-cover-unmute" : "image://theme/icon-cover-mute"
            onTriggered: groupCalls.muted = !groupCalls.muted
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-hangup"
            onTriggered: groupCalls.leave()
        }
    }

    // The count and what it counts, or with nothing unread the state. Where the bubble floats
    // (right of it) stays free.
    Row {
        id: head
        visible: !cover.inCall
        x: Theme.paddingLarge
        y: Theme.paddingMedium
        spacing: Theme.paddingMedium

        Label {
            id: count
            visible: backend.unreadCount > 0
            text: backend.unreadCount
            font.pixelSize: Theme.fontSizeHuge
            color: Theme.primaryColor
        }

        Label {
            anchors.verticalCenter: count.visible ? count.verticalCenter : undefined
            // The huge count's line is the height of the row also without the count.
            height: count.visible ? implicitHeight : Math.max(implicitHeight, count.height)
            verticalAlignment: Text.AlignVCenter
            width: cover.width - 2 * Theme.paddingLarge - sea.bubbleWidth
                   - (count.visible ? count.width + head.spacing : 0)
            wrapMode: Text.Wrap
            maximumLineCount: 2
            lineHeight: 0.9
            font.pixelSize: count.visible ? Theme.fontSizeExtraSmall : Theme.fontSizeSmall
            color: Theme.secondaryColor
            text: backend.unreadCount === 1
                  //% "unread message"
                  ? qsTrId("threesea-la-cover-unread-one")
                  : backend.unreadCount > 1
                    //% "unread messages"
                    ? qsTrId("threesea-la-cover-unread-many")
                    : backend.state === "locked"
                      ? qsTrId("threesea-la-locked-notification")
                      // Starting, failed, not set up: nothing to say about messages.
                      : backend.state !== "ready" ? ""
                      // Like the chat list's header: "no new" is known only while connected.
                      : backend.connectionState === "connected"
                        //% "No new messages"
                        ? qsTrId("threesea-la-cover-no-new")
                        : backend.connectionState === "connecting"
                          ? qsTrId("threesea-la-connecting")
                          : qsTrId("threesea-la-offline")
        }
    }

    // Who is waiting, newest first: the chats that still have something unread, each with its
    // own count and, if asked for, its last message. Never a private or an archived chat (the
    // core leaves them out); "Hide content on the cover" (Privacy) and the app lock show only
    // the count.
    Column {
        id: names
        visible: !cover.inCall
        x: Theme.paddingLarge
        y: cover.listBottom - height
        width: cover.width - 2 * Theme.paddingLarge

        Repeater {
            model: cover.waiting

            Column {
                width: names.width

                Item {
                    width: parent.width
                    height: nameMetrics.height

                    Label {
                        anchors {
                            left: parent.left
                            right: unread.left
                            rightMargin: Theme.paddingSmall
                            verticalCenter: parent.verticalCenter
                        }
                        truncationMode: TruncationMode.Fade
                        text: modelData.displayName
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.primaryColor
                    }

                    Label {
                        id: unread
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: modelData.unread
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                    }
                }

                Label {
                    visible: cover.previews
                    width: parent.width
                    height: previewMetrics.height
                    verticalAlignment: Text.AlignVCenter
                    truncationMode: TruncationMode.Fade
                    maximumLineCount: 1
                    // A group's with who wrote it, like the chat list.
                    text: cover.previews
                          ? (modelData.senderName && !modelData.lastMessage.outgoing
                             && modelData.lastMessage.kind !== "status"
                             ? modelData.senderName + ": " : "") + Fmt.preview(modelData.lastMessage)
                          : ""
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
            }
        }
    }

    // From here: inside a CoverActionList `window` is its own property, not the app's window.
    readonly property bool appLocked: window.locked
    readonly property bool actionsOn: backend.state === "ready" && !cover.inCall && !cover.appLocked
    readonly property bool namesChat: backend.unreadCount > 0 && backend.unreadFrom.length > 0
                                      && backend.unreadChat.length > 0 && !window.locked
                                      && window.appSettings.disableScreenshots !== true

    // New message: the contacts, attached to the chat list (swipe left there).
    function newMessage() {
        window.activate()
        var chats = pageStack.find(function(page) { return page.objectName === "conversations" })
        if (!chats) return
        pageStack.pop(chats, PageStackAction.Immediate)
        if (pageStack.nextPage(chats) === null) {
            pageStack.pushAttached(Qt.resolvedUrl("../pages/ContactsPage.qml"))
        }
        pageStack.navigateForward(PageStackAction.Immediate)
    }

    function openWaitingChat() {
        window.showChat(backend.unreadChat)
    }

    CoverActionList {
        enabled: cover.actionsOn && !cover.namesChat
        CoverAction {
            iconSource: "image://theme/icon-cover-new"
            onTriggered: cover.newMessage()
        }
    }

    // With the name on the cover, also the way to that chat.
    CoverActionList {
        enabled: cover.actionsOn && cover.namesChat
        CoverAction {
            iconSource: "image://theme/icon-cover-new"
            onTriggered: cover.newMessage()
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-message"
            onTriggered: cover.openWaitingChat()
        }
    }
}
