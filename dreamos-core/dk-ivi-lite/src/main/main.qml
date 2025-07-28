import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import AppManager 1.0

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: "App Manager Example"
    
    // Use the framework's main component
    AppManagerView {
        anchors.fill: parent
        
        // You can customize the app manager if needed
        Component.onCompleted: {
            // Auto-search on startup
            appManager.searchApps("", "vehicle")
        }
    }
}
