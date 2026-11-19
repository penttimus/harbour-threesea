// SPDX-License-Identifier: AGPL-3.0-only
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../../components"

// Settings → Cover: what the cover shows of the chats that wait, and how its sea looks.
Page {
    id: page
    allowedOrientations: Orientation.All

    property var settings: ({})

    function setSetting(name, value) {
        backend.call("setSetting", { name: name, value: value }, function(ok, result) {
            if (!ok) return
            page.settings = result
            window.appSettings = result
        })
    }

    function loadSettings() {
        backend.call("settings", {}, function(ok, result) {
            if (ok) page.settings = result
        })
    }

    Component.onCompleted: loadSettings()
    Connections {
        target: backend
        onCoreEvent: if (name === "settingsChanged") page.loadSettings()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                //% "Cover"
                title: qsTrId("threesea-se-cover")
            }

            SectionHeader {
                //% "Content"
                text: qsTrId("threesea-se-cover-content")
            }

            TextSwitch {
                automaticCheck: false
                checked: page.settings.disableScreenshots === true
                //% "Hide content on the cover"
                text: qsTrId("threesea-la-hide-cover")
                //% "The cover only shows the number of new messages, not who wrote."
                description: qsTrId("threesea-la-hide-cover-description")
                onClicked: page.setSetting("disableScreenshots", !checked)
            }

            TextSwitch {
                enabled: page.settings.disableScreenshots !== true
                automaticCheck: false
                checked: backend.coverPreviews
                //% "Show messages on the cover"
                text: qsTrId("threesea-la-cover-previews")
                //% "Under each chat that has something new, the cover also shows its last message. Never for private chats."
                description: qsTrId("threesea-la-cover-previews-description")
                onClicked: backend.coverPreviews = !checked
            }

            SectionHeader {
                //% "Look"
                text: qsTrId("threesea-se-cover-look")
            }

            ChoiceBox {
                options: [
                    //% "Ambient"
                    qsTrId("threesea-me-cover-colour-ambient"),
                    //% "Static"
                    qsTrId("threesea-me-cover-colour-static")
                ]
                //% "Colour of the waves"
                label: qsTrId("threesea-la-cover-colour")
                //% "Ambient takes the colour of your ambience, Static is a pale haze on every ambience."
                description: qsTrId("threesea-la-cover-colour-description")
                choice: backend.coverColour
                onChosen: backend.coverColour = index
            }

            ChoiceBox {
                options: [
                    //% "Leveled"
                    qsTrId("threesea-me-cover-waves-leveled"),
                    //% "Rising"
                    qsTrId("threesea-me-cover-waves-rising"),
                    //% "Always risen"
                    qsTrId("threesea-me-cover-waves-risen")
                ]
                //% "Waves"
                label: qsTrId("threesea-la-cover-waves")
                //% "Rising: the water climbs with every chat that has something new. Leveled keeps it low, Always risen keeps it high."
                description: qsTrId("threesea-la-cover-waves-description")
                choice: backend.coverWaves
                onChosen: backend.coverWaves = index
            }
        }

        VerticalScrollDecorator {}
    }
}
