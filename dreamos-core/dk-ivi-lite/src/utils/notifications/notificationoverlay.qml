import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ───────────────────────────────────────────────────────────────
// Modern Notification Overlay - Global UI Component
// ───────────────────────────────────────────────────────────────
Item {
    id: notificationOverlay
    anchors.fill: parent
    z: 9999 // Ensure it's always on top
    
    property var notificationManagerInstance: null
    property int maxVisibleNotifications: 5
    property bool compactMode: false
    property string position: "topRight" // topRight, topLeft, bottomRight, bottomLeft, center
    property int margin: 24
    property int notificationWidth: 380
    property int notificationHeight: compactMode ? 64 : 96
    property int spacing: 12
    
    // Animation settings
    property int animationDuration: 400
    property int staggerDelay: 80
    
    Component.onCompleted: {
        // Connect to the global notification manager
        if (notificationManagerInstance) {
            connectToManager()
        }
    }
    
    function connectToManager() {
        if (!notificationManagerInstance) return
        
        // Connect to notification signals
        notificationManagerInstance.notificationAdded.connect(handleNotificationAdded)
        notificationManagerInstance.notificationDismissed.connect(handleNotificationDismissed)
        notificationManagerInstance.notificationUpdated.connect(handleNotificationUpdated)
        notificationManagerInstance.allNotificationsDismissed.connect(handleAllDismissed)
    }
    
    function handleNotificationAdded(id, title, message, level, duration, category, progress, actionText, actionId) {
        var notification = {
            id: id,
            title: title,
            message: message,
            level: level,
            duration: duration,
            category: category,
            progress: progress,
            actionText: actionText,
            actionId: actionId,
            timestamp: new Date(),
            visible: false
        }
        
        notificationModel.append(notification)
        
        // Animate in with stagger
        var index = notificationModel.count - 1
        animateNotificationIn(index)
        
        // Limit visible notifications
        while (notificationModel.count > maxVisibleNotifications) {
            notificationModel.remove(0, 1)
        }
    }
    
    function handleNotificationDismissed(id) {
        for (var i = 0; i < notificationModel.count; i++) {
            if (notificationModel.get(i).id === id) {
                animateNotificationOut(i)
                break
            }
        }
    }
    
    function handleNotificationUpdated(id, message, progress) {
        for (var i = 0; i < notificationModel.count; i++) {
            var item = notificationModel.get(i)
            if (item.id === id) {
                notificationModel.setProperty(i, "message", message)
                notificationModel.setProperty(i, "progress", progress)
                break
            }
        }
    }
    
    function handleAllDismissed() {
        // Animate all out
        for (var i = 0; i < notificationModel.count; i++) {
            animateNotificationOut(i)
        }
    }
    
    function animateNotificationIn(index) {
        var item = notificationRepeater.itemAt(index)
        if (!item) return
        
        // Set initial state
        item.opacity = 0
        item.scale = 0.8
        item.x = getNotificationX() + 50
        
        // Animate in with delay
        Qt.callLater(function() {
            item.visible = true
            notificationModel.setProperty(index, "visible", true)
            
            // Entrance animation
            enterAnimation.target = item
            enterAnimation.start()
        }, index * staggerDelay)
    }
    
    function animateNotificationOut(index) {
        var item = notificationRepeater.itemAt(index)
        if (!item) {
            notificationModel.remove(index, 1)
            return
        }
        
        // Exit animation
        exitAnimation.target = item
        exitAnimation.finished.connect(function() {
            notificationModel.remove(index, 1)
            exitAnimation.finished.disconnect(arguments.callee)
        })
        exitAnimation.start()
    }
    
    function getNotificationX() {
        switch (position) {
            case "topLeft":
            case "bottomLeft":
                return margin
            case "topRight":
            case "bottomRight":
                return parent.width - notificationWidth - margin
            case "center":
                return (parent.width - notificationWidth) / 2
            default:
                return parent.width - notificationWidth - margin
        }
    }
    
    function getNotificationY(index) {
        var baseY
        switch (position) {
            case "topLeft":
            case "topRight":
                baseY = margin
                return baseY + index * (notificationHeight + spacing)
            case "bottomLeft":
            case "bottomRight":
                baseY = parent.height - margin - notificationHeight
                return baseY - index * (notificationHeight + spacing)
            case "center":
                var totalHeight = maxVisibleNotifications * (notificationHeight + spacing) - spacing
                baseY = (parent.height - totalHeight) / 2
                return baseY + index * (notificationHeight + spacing)
            default:
                baseY = margin
                return baseY + index * (notificationHeight + spacing)
        }
    }
    
    function getLevelColor(level) {
        switch (level) {
            case 0: return "#4A90E2" // Info - Blue
            case 1: return "#00D4AA" // Success - Green
            case 2: return "#F5A623" // Warning - Orange
            case 3: return "#D0021B" // Error - Red
            case 4: return "#7ED321" // Progress - Light Green
            default: return "#4A90E2"
        }
    }
    
    function getLevelIcon(level) {
        switch (level) {
            case 0: return "ℹ" // Info
            case 1: return "✓" // Success
            case 2: return "⚠" // Warning
            case 3: return "✕" // Error
            case 4: return "⟳" // Progress
            default: return "ℹ"
        }
    }
    
    // Notification model
    ListModel {
        id: notificationModel
    }
    
    // Animation definitions
    ParallelAnimation {
        id: enterAnimation
        property var target: null
        
        NumberAnimation {
            target: enterAnimation.target
            property: "opacity"
            from: 0
            to: 1
            duration: animationDuration
            easing.type: Easing.OutCubic
        }
        
        NumberAnimation {
            target: enterAnimation.target
            property: "scale"
            from: 0.8
            to: 1.0
            duration: animationDuration
            easing.type: Easing.OutBack
            easing.overshoot: 1.2
        }
        
        NumberAnimation {
            target: enterAnimation.target
            property: "x"
            to: getNotificationX()
            duration: animationDuration
            easing.type: Easing.OutCubic
        }
    }
    
    ParallelAnimation {
        id: exitAnimation
        property var target: null
        
        NumberAnimation {
            target: exitAnimation.target
            property: "opacity"
            to: 0
            duration: animationDuration * 0.7
            easing.type: Easing.InCubic
        }
        
        NumberAnimation {
            target: exitAnimation.target
            property: "scale"
            to: 0.8
            duration: animationDuration * 0.7
            easing.type: Easing.InBack
        }
        
        NumberAnimation {
            target: exitAnimation.target
            property: "x"
            to: getNotificationX() + 100
            duration: animationDuration * 0.7
            easing.type: Easing.InCubic
        }
    }
    
    // Notification items
    Repeater {
        id: notificationRepeater
        model: notificationModel
        
        delegate: Item {
            id: notificationItem
            width: notificationWidth
            height: notificationHeight
            x: getNotificationX()
            y: getNotificationY(index)
            visible: false
            
            // Smooth position transitions when other notifications are removed
            Behavior on y {
                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutCubic
                }
            }
            
            // Main notification card
            Rectangle {
                id: notificationCard
                anchors.fill: parent
                radius: 16
                color: "#1A1A1A"
                border.color: getLevelColor(model.level)
                border.width: 2
                
                // Enhanced glassmorphism effect
                Rectangle {
                    id: glassEffect
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: parent.radius - 2
                    color: "#FFFFFF"
                    opacity: 0.05
                }
                
                // Animated accent bar
                Rectangle {
                    id: accentBar
                    width: 4
                    height: parent.height - 16
                    x: 8
                    y: 8
                    radius: 2
                    color: getLevelColor(model.level)
                    
                    // Breathing animation
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.6; duration: 2000 }
                        NumberAnimation { to: 1.0; duration: 2000 }
                    }
                }
                
                // Progress bar (for progress notifications)
                Rectangle {
                    id: progressBackground
                    anchors.bottom: parent.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.margins: 2
                    height: 4
                    radius: 2
                    color: "#2A2A2A"
                    visible: model.progress >= 0
                    
                    Rectangle {
                        id: progressBar
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.max(0, Math.min(100, model.progress)) / 100
                        radius: parent.radius
                        color: getLevelColor(model.level)
                        
                        // Smooth progress transitions
                        Behavior on width {
                            NumberAnimation {
                                duration: 300
                                easing.type: Easing.OutCubic
                            }
                        }
                        
                        // Shimmer effect for progress
                        Rectangle {
                            width: 20
                            height: parent.height
                            color: "#FFFFFF"
                            opacity: 0.3
                            radius: parent.radius
                            
                            SequentialAnimation on x {
                                loops: model.progress < 100 ? Animation.Infinite : 0
                                NumberAnimation {
                                    from: -20
                                    to: progressBar.width + 20
                                    duration: 1500
                                    easing.type: Easing.InOutCubic
                                }
                                PauseAnimation { duration: 500 }
                            }
                        }
                    }
                }
                
                // Content layout
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    anchors.bottomMargin: model.progress >= 0 ? 24 : 16
                    spacing: 12
                    
                    // Icon container
                    Rectangle {
                        width: compactMode ? 32 : 40
                        height: width
                        radius: width / 2
                        color: getLevelColor(model.level)
                        opacity: 0.2
                        border.color: getLevelColor(model.level)
                        border.width: 1
                        Layout.alignment: Qt.AlignTop
                        
                        Text {
                            anchors.centerIn: parent
                            text: getLevelIcon(model.level)
                            font.pixelSize: compactMode ? 14 : 18
                            color: getLevelColor(model.level)
                            font.family: "Segoe UI"
                            font.weight: Font.Bold
                        }
                        
                        // Rotating animation for progress notifications
                        RotationAnimation on rotation {
                            running: model.level === 4 && model.progress < 100
                            loops: Animation.Infinite
                            from: 0
                            to: 360
                            duration: 2000
                        }
                    }
                    
                    // Text content
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: compactMode ? 2 : 4
                        
                        Text {
                            text: model.title
                            font.pixelSize: compactMode ? 14 : 16
                            font.weight: Font.DemiBold
                            color: "#FFFFFF"
                            font.family: "Segoe UI"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        
                        Text {
                            text: model.message
                            font.pixelSize: compactMode ? 12 : 14
                            color: "#B0B0B0"
                            font.family: "Segoe UI"
                            Layout.fillWidth: true
                            wrapMode: compactMode ? Text.NoWrap : Text.WordWrap
                            elide: compactMode ? Text.ElideRight : Text.ElideNone
                            maximumLineCount: compactMode ? 1 : 2
                            lineHeight: 1.2
                        }
                        
                        // Progress text for progress notifications
                        Text {
                            text: model.progress >= 0 ? model.progress + "%" : ""
                            font.pixelSize: 11
                            color: getLevelColor(model.level)
                            font.family: "Segoe UI"
                            font.weight: Font.Medium
                            visible: model.progress >= 0
                        }
                    }
                    
                    // Action button (if available)
                    Button {
                        visible: model.actionText && model.actionText.length > 0
                        text: model.actionText
                        Layout.preferredWidth: 80
                        Layout.preferredHeight: 28
                        
                        background: Rectangle {
                            radius: 14
                            color: parent.hovered ? getLevelColor(model.level) : "transparent"
                            border.color: getLevelColor(model.level)
                            border.width: 1
                            
                            Behavior on color {
                                ColorAnimation { duration: 200 }
                            }
                        }
                        
                        contentItem: Text {
                            text: parent.text
                            color: parent.hovered ? "#FFFFFF" : getLevelColor(model.level)
                            font.family: "Segoe UI"
                            font.pixelSize: 11
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            
                            Behavior on color {
                                ColorAnimation { duration: 200 }
                            }
                        }
                        
                        onClicked: {
                            if (notificationManagerInstance) {
                                notificationManagerInstance.handleNotificationAction(model.id, model.actionId)
                            }
                        }
                    }
                    
                    // Close button
                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        color: closeArea.containsMouse ? "#FF4444" : "#2A2A2A"
                        border.color: closeArea.containsMouse ? "#FF4444" : "#404040"
                        border.width: 1
                        Layout.alignment: Qt.AlignTop
                        
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Behavior on border.color { ColorAnimation { duration: 200 } }
                        
                        Text {
                            anchors.centerIn: parent
                            text: "×"
                            color: closeArea.containsMouse ? "#FFFFFF" : "#B0B0B0"
                            font.pixelSize: 14
                            font.weight: Font.Bold
                            font.family: "Segoe UI"
                            
                            Behavior on color { ColorAnimation { duration: 200 } }
                        }
                        
                        MouseArea {
                            id: closeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            
                            onClicked: {
                                if (notificationManagerInstance) {
                                    notificationManagerInstance.dismissNotification(model.id)
                                } else {
                                    handleNotificationDismissed(model.id)
                                }
                            }
                        }
                    }
                }
                
                // Click handler for entire notification
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    onClicked: {
                        if (notificationManagerInstance) {
                            notificationManagerInstance.handleNotificationClick(model.id)
                        }
                    }
                }
                
                // Hover effects
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    
                    onEntered: {
                        notificationCard.scale = 1.02
                        glassEffect.opacity = 0.08
                    }
                    
                    onExited: {
                        notificationCard.scale = 1.0
                        glassEffect.opacity = 0.05
                    }
                }
                
                Behavior on scale {
                    NumberAnimation {
                        duration: 200
                        easing.type: Easing.OutCubic
                    }
                }
            }
            
            // Drop shadow effect
            Rectangle {
                anchors.fill: notificationCard
                anchors.margins: -2
                radius: notificationCard.radius + 2
                color: "#000000"
                opacity: 0.2
                z: -1
                
                // Shadow blur simulation with multiple layers
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -1
                    radius: parent.radius + 1
                    color: "#000000"
                    opacity: 0.1
                    z: -1
                }
                
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    radius: parent.radius + 2
                    color: "#000000"
                    opacity: 0.05
                    z: -1
                }
            }
        }
    }
    
    // Notification queue indicator (when queue is not empty)
    Rectangle {
        id: queueIndicator
        visible: false // Connect this to queue status from C++
        anchors.right: position.includes("Right") ? parent.right : undefined
        anchors.left: position.includes("Left") ? parent.left : undefined
        anchors.bottom: position.includes("bottom") ? parent.bottom : undefined
        anchors.top: position.includes("top") ? parent.top : undefined
        anchors.margins: margin
        
        width: 120
        height: 32
        radius: 16
        color: "#2A2A2A"
        border.color: "#00D4AA"
        border.width: 1
        opacity: 0.9
        
        Text {
            anchors.centerIn: parent
            text: "3+ more..." // Connect this to actual queue count
            color: "#00D4AA"
            font.family: "Segoe UI"
            font.pixelSize: 12
            font.weight: Font.Medium
                        }
                        
                        MouseArea {
            anchors.fill: parent
            onClicked: {
                // Handle queue expansion or show notification center
            }
        }
    }
    
    // Global notification test functions (for development)
    function testNotifications() {
        if (!notificationManagerInstance) return
        
        notificationManagerInstance.info("Test Info", "This is an info notification")
        
        Qt.callLater(function() {
            notificationManagerInstance.success("Success!", "Operation completed successfully")
        }, 1000)
        
        Qt.callLater(function() {
            notificationManagerInstance.warning("Warning", "Please check your settings")
        }, 2000)
        
        Qt.callLater(function() {
            var taskId = notificationManagerInstance.startTask("Installing App", "Downloading packages...")
            
            // Simulate progress updates
            var progress = 0
            var timer = Qt.createQmlObject('import QtQuick; Timer {}', notificationOverlay)
            timer.interval = 200
            timer.repeat = true
            timer.triggered.connect(function() {
                progress += Math.random() * 15
                if (progress >= 100) {
                    progress = 100
                    timer.stop()
                    notificationManagerInstance.completeTask(taskId, "App installed successfully!")
                } else {
                    notificationManagerInstance.updateTask(taskId, Math.floor(progress), "Installing... " + Math.floor(progress) + "%")
                }
            })
            timer.start()
        }, 3000)
    }
    
    // Shortcut for testing (Ctrl+Shift+N)
    Shortcut {
        sequence: "Ctrl+Shift+N"
        onActivated: testNotifications()
    }
}