// SPDX-License-Identifier: AGPL-3.0-only
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../components/Contributors.js" as Contributors

// Settings → About: version, the European Commission's "AI generated" label, the disclaimer,
// contributors, credits, the license and where the source code is (the same as the README); the
// license texts in the pulley.
Page {
    allowedOrientations: Orientation.All

    // By the local calendar: new Date("2026-11-08") is midnight UTC, the day before in America.
    function sinceDate(day) {
        var parts = day.split("-")
        return Format.formatDate(new Date(parts[0], parts[1] - 1, parts[2]), Formatter.DateLong)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                //% "Third-party licenses"
                text: qsTrId("threesea-me-license-notices")
                onClicked: pageStack.push(Qt.resolvedUrl("LicenseTextPage.qml"),
                                          { title: text, file: "THIRD-PARTY-NOTICES.txt" })
            }
            MenuItem {
                //% "License text"
                text: qsTrId("threesea-me-license-agpl")
                onClicked: pageStack.push(Qt.resolvedUrl("LicenseTextPage.qml"),
                                          { title: "GNU Affero General Public License", file: "LICENSE" })
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                //% "About"
                title: qsTrId("threesea-he-about")
            }

            DetailItem {
                //% "Version"
                label: qsTrId("threesea-la-version")
                value: backend.coreVersion
            }

            DetailItem {
                //% "Flavour"
                label: qsTrId("threesea-la-flavour")
                //% "Full"
                value: backend.full ? qsTrId("threesea-la-flavour-full")
                                      //% "Lite"
                                    : qsTrId("threesea-la-flavour-lite")
            }

            // The EU icon in the colour that stands out on the ambience.
            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                height: Theme.itemSizeExtraSmall
                fillMode: Image.PreserveAspectFit
                sourceSize.height: height
                source: Theme.colorScheme === Theme.LightOnDark ? "../icons/ai-generated-white.png"
                                                                : "../icons/ai-generated-black.png"
            }

            SectionHeader {
                //% "Disclaimer"
                text: qsTrId("threesea-se-disclaimer")
            }

            Repeater {
                //% "Made with Claude Code (Anthropic) and audited with OpenAI Codex, directed and tested by a human maintainer. A research and experimentation project."
                model: [qsTrId("threesea-la-disclaimer-ai"),
                        //% "AI makes mistakes, also in security-relevant code. There is no warranty: you use Threesea at your own risk."
                        qsTrId("threesea-la-disclaimer-warranty"),
                        //% "Unofficial: not affiliated with, endorsed or supported by Threema AG. Threema doesn't actively block third-party apps that play fair, as Threesea aims to, but that isn't a permission. Keep a backup of your ID."
                        qsTrId("threesea-la-disclaimer-threema")]
                Label {
                    x: Theme.horizontalPageMargin
                    width: column.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                    text: modelData
                }
            }

            SectionHeader {
                //% "Contributors"
                text: qsTrId("threesea-se-contributors")
            }

            Repeater {
                model: Contributors.list
                Column {
                    // Until the next release, which ends it by itself.
                    readonly property bool isNew: modelData.release === "next"
                                                  || modelData.release === backend.coreVersion
                    x: Theme.horizontalPageMargin
                    width: column.width - 2 * Theme.horizontalPageMargin
                    Row {
                        spacing: Theme.paddingSmall
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: isNew
                            source: "image://theme/icon-s-new?" + Theme.highlightColor
                        }
                        Label {
                            font.pixelSize: Theme.fontSizeSmall
                            color: isNew ? Theme.highlightColor : Theme.secondaryHighlightColor
                            text: modelData.login
                        }
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                        //% "since %1"
                        text: qsTrId("threesea-la-contributor-since").arg(sinceDate(modelData.since))
                    }
                    Label {
                        width: parent.width
                        wrapMode: Text.Wrap
                        visible: isNew
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.highlightColor
                        //% "New in this version"
                        text: qsTrId("threesea-la-contributor-new")
                    }
                }
            }

            SectionHeader {
                //% "Credits"
                text: qsTrId("threesea-se-credits")
            }

            Repeater {
                model: {
                    //% "Threema AG: threema-android and libthreema (AGPL-3.0), the protocols of Threema, Threema for Desktop and Threema Web."
                    var credits = [qsTrId("threesea-la-credit-threema"),
                            // Required attribution (CC-BY 4.0), as in Threema Libre's licence page.
                            //% "Emoji graphics by Twemoji (github.com/jdecked/twemoji), licensed under CC-BY 4.0."
                            qsTrId("threesea-la-twemoji-credit"),
                            //% "Emoji search terms from threema-android (AGPL-3.0)."
                            qsTrId("threesea-la-credit-emoji-search"),
                            //% "Unicode CLDR: those search terms are likely derived from its emoji annotations (Unicode License v3)."
                            qsTrId("threesea-la-credit-cldr"),
                            //% "opus-pure by Stephen Berry, from Rust ports of libopus by the Xiph.Org Foundation and others (BSD-3-Clause): voice messages."
                            qsTrId("threesea-la-credit-opus"),
                            //% "The map: MapLibre Native and its Qt bindings (BSD-2-Clause), mapbox-gl-qml by rinigus (LGPL-3.0) and his Sailfish packaging pkg-maplibre-gl-native (MIT). Their exact sources: github.com/maplibre/maplibre-native-qt 3.0.0, github.com/rinigus/pkg-maplibre-gl-native 3.0.0+git2 and github.com/rinigus/mapbox-gl-qml 3.2.1, with this app's patches in its source under scripts/maplibre."
                            qsTrId("threesea-la-credit-maplibre"),
                            //% "Map data © OpenMapTiles © OpenStreetMap contributors (ODbL), from Threema's map server."
                            qsTrId("threesea-la-credit-map-data"),
                            //% "Jolla and Sailfish OS: Silica and its icons; Jolla's Messages, People, Settings and Gallery as the model for the design."
                            qsTrId("threesea-la-credit-jolla"),
                            //% "Whisperfish, the Signal client for Sailfish OS: a reference for building such an app."
                            qsTrId("threesea-la-credit-whisperfish"),
                            //% "Harpoon by Juhani Lehtimäki (GPL-3.0): how the waves on the cover are coloured."
                            qsTrId("threesea-la-credit-harpoon"),
                            //% "SQLCipher, GStreamer, Qt and many Rust libraries (tokio, rustls, reqwest, rusqlite, RustCrypto and others)."
                            qsTrId("threesea-la-credit-libraries"),
                            //% "The “AI generated” label is the European Commission's icon for AI-generated content."
                            qsTrId("threesea-la-credit-eu-icon")]
                    // Only the Full flavour has calls and links the echo canceller.
                    if (backend.full) {
                        //% "webrtc-audio-processing, from Google's WebRTC (BSD-3-Clause): echo cancellation in calls."
                        credits.splice(4, 0, qsTrId("threesea-la-credit-webrtc"))
                    }
                    return credits
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: column.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: modelData
                }
            }

            SectionHeader {
                //% "License"
                text: qsTrId("threesea-se-license")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                //% "Free software under the GNU AGPL v3."
                text: qsTrId("threesea-la-license-text")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                //% "Copyright © 2026 the Threesea developers. libthreema and the code taken from threema-android: copyright Threema AG."
                text: qsTrId("threesea-la-license-copyright")
            }

            // AGPL section 13 and the GitHub page for problems: where the source code is.
            LinkedLabel {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                linkColor: Theme.primaryColor
                //% "Source code and bug reports: %1"
                plainText: qsTrId("threesea-la-license-source").arg("https://github.com/penttimus/harbour-threesea")
            }
        }

        VerticalScrollDecorator { }
    }
}
