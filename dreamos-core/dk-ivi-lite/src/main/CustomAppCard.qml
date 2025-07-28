import QtQuick 2.15
import QtQuick.Controls 2.15
import AppManager 1.0

// Extend the base AppCard with custom styling
AppCard {
    id: customCard
    
    // Custom properties
    property color accentColor: "#FF6B35"
    property bool showBadge: true
    
    // Override the border color
    border.color: mouseArea.containsMouse ? accentColor : "#404040"
    
    // Add custom badge
    Rectangle {
        visible: showBadge && customCard.isInstalled
        width: 60
        height: 20
        radius: 10
        color: accentColor
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        
        Text {
            anchors.centerIn: parent
            text: "NEW"
            color: "white"
            font.pixelSize: 10
            font.bold: true
        }
    }
}