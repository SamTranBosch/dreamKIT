// InstalledAppCard.qml - Card for installed apps with controls
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
    property string appIconUrl
    property bool isRunning
    property int appStatus
    
    height: 100
    color: "#2A2A2A"
    radius: 12
    border.color: "#404040"
    border.width: 1
    
    RowLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 16
        
        // App icon
        Rectangle {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            
            radius: 12
            color: "#404040"
            
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
                    font.pixelSize: 20
                    font.bold: true
                    color: "#00D4AA"
                }
            }
        }
        
        // App info
        Column {
            Layout.fillWidth: true
            spacing: 4
            
            Text {
                text: appName
                font.pixelSize: 18
                font.bold: true
                color: "#FFFFFF"
            }
            
            Text {
                text: appAuthor
                font.pixelSize: 14
                color: "#B0B0B0"
            }
            
            Text {
                text: "Version " + appVersion
                font.pixelSize: 12
                color: "#808080"
                visible: appVersion !== ""
            }
        }
        
        // Status indicator
        Rectangle {
            Layout.preferredWidth: 80
            Layout.preferredHeight: 28
            
            radius: 14
            color: isRunning ? "#00D4AA20" : "#404040"
            border.color: isRunning ? "#00D4AA" : "#606060"
            border.width: 1
            
            Text {
                anchors.centerIn: parent
                text: isRunning ? "Running" : "Stopped"
                color: isRunning ? "#00D4AA" : "#B0B0B0"
                font.pixelSize: 12
                font.weight: Font.Medium
            }
        }
        
        // Control buttons
        Row {
            spacing: 8
            
            // Start/Stop button
            Button {
                width: 80
                height: 36
                
                text: isRunning ? "Stop" : "Start"
                
                onClicked: {
                    if (isRunning) {
                        appManager.stopApp(appId)
                    } else {
                        appManager.startApp(appId)
                    }
                }
                
                background: Rectangle {
                    color: {
                        if (isRunning) {
                            return parent.hovered ? "#FF6666" : "#FF4444"
                        } else {
                            return parent.hovered ? "#00E5BB" : "#00D4AA"
                        }
                    }
                    radius: 18
                    
                    Behavior on color { ColorAnimation { duration: 200 } }
                }
                
                contentItem: Text {
                    text: parent.text
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                    font.weight: Font.Medium
                }
            }
            
            // Uninstall button
            Button {
                width: 36
                height: 36
                
                onClicked: uninstallDialog.open()
                
                background: Rectangle {
                    color: parent.hovered ? "#FF444440" : "#FF444420"
                    radius: 18
                    border.color: "#FF4444"
                    border.width: 1
                    
                    Behavior on color { ColorAnimation { duration: 200 } }
                }
                
                contentItem: Text {
                    text: "🗑"
                    color: "#FF4444"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 14
                }
            }
        }
    }
    
    // Uninstall confirmation dialog
    Dialog {
        id: uninstallDialog
        title: "Confirm Uninstall"
        modal: true
        anchors.centerIn: parent
        
        contentItem: Column {
            spacing: 16
            
            Text {
                text: "Are you sure you want to uninstall " + appName + "?"
                color: "#FFFFFF"
                wrapMode: Text.WordWrap
            }
            
            Text {
                text: "This action cannot be undone."
                color: "#FF4444"
                font.pixelSize: 12
            }
        }
        
        background: Rectangle {
            color: "#2A2A2A"
            radius: 10
            border.color: "#FF4444"
            border.width: 1
        }
        
        Row {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 16
            
            Button {
                text: "Cancel"
                onClicked: uninstallDialog.close()
                
                background: Rectangle {
                    color: parent.hovered ? "#404040" : "#2A2A2A"
                    radius: 8
                    border.color: "#606060"
                    border.width: 1
                }
                
                contentItem: Text {
                    text: parent.text
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            
            Button {
                text: "Uninstall"
                onClicked: {
                    appManager.uninstallApp(appId)
                    uninstallDialog.close()
                }
                
                background: Rectangle {
                    color: parent.hovered ? "#FF6666" : "#FF4444"
                    radius: 8
                }
                
                contentItem: Text {
                    text: parent.text
                    color: "#FFFFFF"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}