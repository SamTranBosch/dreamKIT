// InstalledAppsView.qml - View for managing installed apps
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AppManager 1.0

Item {
    property ApplicationManager appManager
    
    Component.onCompleted: {
        appManager.refreshInstalledApps()
    }
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        
        // Header
        RowLayout {
            Layout.fillWidth: true
            
            Text {
                text: "Installed Applications"
                font.pixelSize: 24
                font.weight: Font.Medium
                color: "#FFFFFF"
            }
            
            Item { Layout.fillWidth: true }
            
            Button {
                text: "Refresh"
                onClicked: appManager.refreshInstalledApps()
                
                background: Rectangle {
                    color: parent.hovered ? "#404040" : "#2A2A2A"
                    radius: 8
                    border.color: "#00D4AA"
                    border.width: 1
                }
                
                contentItem: Text {
                    text: parent.text
                    color: "#00D4AA"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
        
        // Installed apps list
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            ListView {
                id: installedList
                model: appManager.installedApps
                spacing: 12
                
                delegate: InstalledAppCard {
                    width: installedList.width
                    appManager: root.appManager
                    
                    appId: model.id
                    appName: model.name
                    appAuthor: model.author
                    appVersion: model.version
                    appIconUrl: model.iconUrl
                    isRunning: model.isRunning
                    appStatus: model.status
                }
            }
        }
        
        // Empty state
        Column {
            Layout.alignment: Qt.AlignHCenter
            visible: installedList.count === 0
            spacing: 16
            
            Rectangle {
                width: 80
                height: 80
                radius: 40
                color: "#2A2A2A"
                anchors.horizontalCenter: parent.horizontalCenter
                
                Text {
                    anchors.centerIn: parent
                    text: "📱"
                    font.pixelSize: 32
                }
            }
            
            Text {
                text: "No Apps Installed"
                font.pixelSize: 18
                color: "#B0B0B0"
                anchors.horizontalCenter: parent.horizontalCenter
            }
            
            Text {
                text: "Visit the Marketplace to install apps"
                font.pixelSize: 14
                color: "#707070"
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }
}
