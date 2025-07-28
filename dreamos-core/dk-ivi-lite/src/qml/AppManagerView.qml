// AppManagerView.qml - Main interface component
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AppManager 1.0

ApplicationWindow {
    id: root
    width: 1200
    height: 800
    title: "Application Manager"
    
    property ApplicationManager appManager: ApplicationManager.createManager()
    
    background: Rectangle {
        color: "#1A1A1A"
    }
    
    Component.onCompleted: {
        // Initialize with default search
        appManager.searchApps("", "vehicle")
        appManager.refreshInstalledApps()
    }
    
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 20
        
        // Header
        Text {
            text: "Application Manager"
            font.pixelSize: 32
            font.weight: Font.Bold
            color: "#00D4AA"
            Layout.alignment: Qt.AlignHCenter
        }
        
        // Tab bar for switching between views
        TabBar {
            id: tabBar
            Layout.fillWidth: true
            
            TabButton {
                text: "Marketplace"
                font.pixelSize: 16
            }
            
            TabButton {
                text: "Installed Apps"
                font.pixelSize: 16
            }
            
            background: Rectangle {
                color: "#2A2A2A"
                radius: 8
            }
        }
        
        // Stack layout for different views
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex
            
            // Marketplace view
            MarketplaceView {
                appManager: root.appManager
            }
            
            // Installed apps view
            InstalledAppsView {
                appManager: root.appManager
            }
        }
    }
    
    // Global loading indicator
    BusyIndicator {
        anchors.centerIn: parent
        visible: appManager.isLoading
        running: visible
        
        background: Rectangle {
            color: "#80000000"
            radius: 40
        }
    }
    
    // Error dialog
    Dialog {
        id: errorDialog
        title: "Error"
        modal: true
        anchors.centerIn: parent
        
        contentItem: Text {
            text: appManager.currentError
            color: "#FFFFFF"
            wrapMode: Text.WordWrap
        }
        
        background: Rectangle {
            color: "#2A2A2A"
            radius: 10
            border.color: "#FF4444"
            border.width: 1
        }
        
        Button {
            text: "OK"
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            onClicked: errorDialog.close()
        }
        
        Connections {
            target: appManager
            function onCurrentErrorChanged(error) {
                if (error !== "") {
                    errorDialog.open()
                }
            }
        }
    }
}
