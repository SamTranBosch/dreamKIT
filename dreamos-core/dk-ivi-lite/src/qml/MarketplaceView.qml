// MarketplaceView.qml - View for browsing available apps
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import AppManager 1.0

Item {
    property ApplicationManager appManager
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 16
        
        // Search and filter controls
        RowLayout {
            Layout.fillWidth: true
            
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "Search applications..."
                font.pixelSize: 14
                
                background: Rectangle {
                    color: "#2A2A2A"
                    radius: 8
                    border.color: parent.activeFocus ? "#00D4AA" : "#404040"
                    border.width: 1
                }
                
                color: "#FFFFFF"
                
                onAccepted: {
                    appManager.searchApps(text, categoryCombo.currentText)
                }
            }
            
            ComboBox {
                id: categoryCombo
                model: ["vehicle", "vehicle-service", "utility", "entertainment"]
                currentIndex: 0
                
                onCurrentTextChanged: {
                    appManager.searchApps(searchField.text, currentText)
                }
                
                background: Rectangle {
                    color: "#2A2A2A"
                    radius: 8
                    border.color: "#404040"
                    border.width: 1
                }
                
                contentItem: Text {
                    text: parent.currentText
                    color: "#FFFFFF"
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 12
                }
            }
            
            Button {
                text: "Search"
                onClicked: appManager.searchApps(searchField.text, categoryCombo.currentText)
                
                background: Rectangle {
                    color: parent.hovered ? "#00E5BB" : "#00D4AA"
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
        
        // Apps grid
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            GridView {
                id: appsGrid
                model: appManager.availableApps
                cellWidth: 280
                cellHeight: 320
                
                delegate: AppCard {
                    width: appsGrid.cellWidth - 10
                    height: appsGrid.cellHeight - 10
                    appManager: root.appManager
                    
                    appId: model.id
                    appName: model.name
                    appAuthor: model.author
                    appVersion: model.version
                    appDescription: model.description
                    appIconUrl: model.iconUrl
                    appRating: model.rating
                    appDownloads: model.downloads
                    appStatus: model.status
                    isInstalled: model.isInstalled
                    isRunning: model.isRunning
                }
            }
        }
    }
}
