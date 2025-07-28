// AppCard.qml - Reusable app card component
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AppManager 1.0

Rectangle {
    id: card
    
    property ApplicationManager appManager
    property string appId
    property string appName
    property string appAuthor
    property string appVersion
    property string appDescription
    property string appIconUrl
    property real appRating
    property int appDownloads
    property int appStatus
    property bool isInstalled
    property bool isRunning
    
    color: "#2A2A2A"
    radius: 16
    border.color: mouseArea.containsMouse ? "#00D4AA" : "#404040"
    border.width: 1
    
    Behavior on border.color { ColorAnimation { duration: 200 } }
    
    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12
        
        // App icon and status
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 80
            
            Rectangle {
                id: iconContainer
                width: 64
                height: 64
                radius: 12
                color: "#404040"
                anchors.centerIn: parent
                
                Image {
                    id: appIcon
                    source: appIconUrl
                    width: 48
                    height: 48
                    anchors.centerIn: parent
                    fillMode: Image.PreserveAspectFit
                    visible: status === Image.Ready
                }
                
                // Fallback icon
                Rectangle {
                    anchors.fill: parent
                    visible: !appIcon.visible
                    color: "#00D4AA20"
                    radius: parent.radius
                    
                    Text {
                        anchors.centerIn: parent
                        text: appName.length > 0 ? appName.charAt(0).toUpperCase() : "A"
                        font.pixelSize: 24
                        font.bold: true
                        color: "#00D4AA"
                    }
                }
            }
            
            // Status indicator
            Rectangle {
                width: 20
                height: 20
                radius: 10
                color: {
                    switch(appStatus) {
                        case 4: return "#00D4AA" // Running
                        case 3: return "#FFA500" // Installed
                        case 2: return "#2196F3" // Installing
                        default: return "transparent"
                    }
                }
                visible: isInstalled
                anchors.top: iconContainer.top
                anchors.right: iconContainer.right
                anchors.margins: -8
                
                Text {
                    anchors.centerIn: parent
                    text: appStatus === 4 ? "▶" : "✓"
                    color: "white"
                    font.pixelSize: 10
                    font.bold: true
                }
            }
        }
        
        // App information
        Column {
            Layout.fillWidth: true
            spacing: 4
            
            Text {
                text: appName
                font.pixelSize: 16
                font.bold: true
                color: "#FFFFFF"
                elide: Text.ElideRight
                width: parent.width
            }
            
            Text {
                text: appAuthor
                font.pixelSize: 12
                color: "#B0B0B0"
                elide: Text.ElideRight
                width: parent.width
            }
            
            Text {
                text: "v" + appVersion
                font.pixelSize: 10
                color: "#808080"
                visible: appVersion !== ""
            }
        }
        
        // Rating and downloads
        Row {
            Layout.fillWidth: true
            spacing: 16
            visible: appRating > 0 || appDownloads > 0
            
            Row {
                spacing: 4
                visible: appRating > 0
                
                Text {
                    text: "★"
                    color: "#FFD700"
                    font.pixelSize: 12
                }
                
                Text {
                    text: appRating.toFixed(1)
                    color: "#B0B0B0"
                    font.pixelSize: 12
                }
            }
            
            Text {
                text: appDownloads + " downloads"
                color: "#B0B0B0"
                font.pixelSize: 12
                visible: appDownloads > 0
            }
        }
        
        // Description
        Text {
            Layout.fillWidth: true
            Layout.fillHeight: true
            text: appDescription
            color: "#B0B0B0"
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
            maximumLineCount: 3
        }
        
        // Action button
        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            
            text: {
                if (appStatus === 2) return "Installing..." // Installing
                if (isInstalled) return "Installed"
                return "Install"
            }
            
            enabled: appStatus !== 2 // Not installing
            
            onClicked: {
                if (!isInstalled) {
                    appManager.installApp(appId)
                }
            }
            
            background: Rectangle {
                color: {
                    if (!parent.enabled) return "#404040"
                    if (isInstalled) return "#00D4AA"
                    if (parent.hovered) return "#2196F3AA"
                    return "#2196F3"
                }
                radius: 18
                
                Behavior on color { ColorAnimation { duration: 200 } }
            }
            
            contentItem: Text {
                text: parent.text
                color: parent.enabled ? "#FFFFFF" : "#808080"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
    }
}
