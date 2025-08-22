import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// ───────────────────────────────────────────────────────────────
// Modern Notification Overlay - Global UI Component - FIXED
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
    
    // SOLUTION 1: Store timers separately from the model
    property var activeTimers: ({}) // Object to store timers by notification ID
    
    // FIXED: Better initialization with retry mechanism
    Component.onCompleted: {
        // console.log("[NotificationOverlay] Component completed")
        // console.log("[NotificationOverlay] notificationManagerInstance:", notificationManagerInstance)
        // console.log("[NotificationOverlay] Parent:", parent)
        // console.log("[NotificationOverlay] Visible:", visible)
        // console.log("[NotificationOverlay] Z:", z)
        
        // Connect to the global notification manager
        if (notificationManagerInstance) {
            // console.log("[NotificationOverlay] Connecting to manager immediately...")
            connectToManager()
        } else {
            // console.log("[NotificationOverlay] No notificationManagerInstance, waiting...")
            
            // Try multiple times with increasing delays
            var attempts = 0
            var maxAttempts = 20 // Increased attempts
            
            function tryConnect() {
                attempts++
                // console.log("[NotificationOverlay] Connection attempt", attempts, "of", maxAttempts)
                
                if (notificationManagerInstance) {
                    // console.log("[NotificationOverlay] Found notificationManagerInstance on attempt", attempts)
                    connectToManager()
                    return
                }
                
                if (attempts < maxAttempts) {
                    Qt.callLater(tryConnect, attempts * 100)
                } else {
                    // console.log("[NotificationOverlay] ERROR: Failed to find notificationManagerInstance after", maxAttempts, "attempts")
                }
            }
            
            Qt.callLater(tryConnect, 100)
        }
    }

    // FIXED: Enhanced connection manager
    function connectToManager() {
        if (!notificationManagerInstance) {
            // console.log("[NotificationOverlay] ERROR: notificationManagerInstance is null in connectToManager")
            return
        }
        
        // console.log("[NotificationOverlay] Connecting to notification manager...")
        // console.log("[NotificationOverlay] Manager object:", notificationManagerInstance)
        
        try {
            // Connect to notification signals
            notificationManagerInstance.notificationAdded.connect(handleNotificationAdded)
            notificationManagerInstance.notificationDismissed.connect(handleNotificationDismissed)
            notificationManagerInstance.notificationUpdated.connect(handleNotificationUpdated)
            notificationManagerInstance.allNotificationsDismissed.connect(handleAllDismissed)
            notificationManagerInstance.notificationExtended.connect(handleNotificationExtended)
            
            // console.log("[NotificationOverlay] Successfully connected all signals")
            
            // Test connection by triggering a notification
            // Qt.callLater(function() {
            //     // console.log("[NotificationOverlay] Testing connection with startup notification...")
            //     notificationManagerInstance.success("System Ready", "Notification system connected successfully")
            // }, 500)
            
        } catch (error) {
            // console.log("[NotificationOverlay] ERROR connecting signals:", error)
        }
    }
    // SOLUTION 3: Enhanced auto-dismiss function that stores timers separately
    function setupAutoDismissEnhanced(id, duration) {
        if (duration <= 0) return
        
        // Clean up any existing timer for this ID
        if (activeTimers[id]) {
            activeTimers[id].destroy()
            delete activeTimers[id]
        }
        
        // Add small random offset to prevent all timers firing simultaneously
        var randomOffset = Math.floor(Math.random() * 200) // 0-199ms random offset
        var adjustedDuration = duration + randomOffset
        
        console.log("[NotificationOverlay] Setting up auto-dismiss for", id, "in", adjustedDuration, "ms (original:", duration, ")")
        
        var timer = Qt.createQmlObject('
            import QtQuick
            Timer {
                property string notificationId: ""
                running: true
                repeat: false
                onTriggered: {
                    console.log("[NotificationOverlay] QML Timer auto-dismissing notification:", notificationId)
                    if (notificationManagerInstance) {
                        notificationManagerInstance.dismissNotification(notificationId)
                    } else {
                        // Fallback to local dismissal
                        handleNotificationDismissed(notificationId)
                    }
                    // Clean up timer reference
                    if (activeTimers[notificationId]) {
                        delete activeTimers[notificationId]
                    }
                    destroy()
                }
            }
        ', notificationOverlay)
        
        timer.interval = adjustedDuration
        timer.notificationId = id
        
        // Store timer reference separately from model
        activeTimers[id] = timer
    }

    // FIXED: Enhanced notification handling with better error management
    function handleNotificationAdded(id, title, message, level, duration, category, progress, actionText, actionId) {
        console.log("[NotificationOverlay] *** handleNotificationAdded called ***")
        console.log("[NotificationOverlay] Title:", title)
        console.log("[NotificationOverlay] Message:", message)
        
        var notification = {
            id: id,
            title: title,
            message: message,
            level: level,
            duration: duration,
            category: category,
            progress: progress,
            actionText: actionText || "",
            actionId: actionId || "",
            timestamp: new Date(),
            visible: false
            // REMOVED: dismissTimer - don't store Timer objects in ListModel
        }
        
        try {
            notificationModel.append(notification)
            console.log("[NotificationOverlay] Model count after append:", notificationModel.count)
            
            // Animate in with stagger
            var index = notificationModel.count - 1
            console.log("[NotificationOverlay] Animating notification at index:", index)
            
            Qt.callLater(function() {
                animateNotificationIn(index)
                
                // Set up auto-dismiss timer only for notifications with duration > 0
                if (duration > 0) {
                    setupAutoDismissEnhanced(id, duration)
                }
            }, 50)
            
            // Limit visible notifications
            while (notificationModel.count > maxVisibleNotifications) {
                console.log("[NotificationOverlay] Removing excess notification")
                var removedNotification = notificationModel.get(0)
                if (removedNotification && activeTimers[removedNotification.id]) {
                    // Clean up timer for removed notification
                    activeTimers[removedNotification.id].destroy()
                    delete activeTimers[removedNotification.id]
                }
                notificationModel.remove(0, 1)
            }
            
        } catch (error) {
            console.log("[NotificationOverlay] ERROR in handleNotificationAdded:", error)
        }
    }
    
    // FIXED: Enhanced dismiss handling
    function handleNotificationDismissed(id) {
        console.log("[NotificationOverlay] Handling dismissal for ID:", id)
        
        // Clean up timer first
        if (activeTimers[id]) {
            activeTimers[id].destroy()
            delete activeTimers[id]
            console.log("[NotificationOverlay] Cleaned up timer for notification:", id)
        }
        
        // Find and remove notification from model
        for (var i = 0; i < notificationModel.count; i++) {
            var notification = notificationModel.get(i)
            if (notification.id === id) {
                console.log("[NotificationOverlay] Found notification to dismiss at index:", i)
                animateNotificationOut(i)
                return
            }
        }
        // console.log("[NotificationOverlay] Warning: Notification", id, "not found for dismissal")
    }
    
    function handleNotificationUpdated(id, message, progress) {
        // console.log("[NotificationOverlay] Updating notification:", id, "message:", message, "progress:", progress)
        for (var i = 0; i < notificationModel.count; i++) {
            var item = notificationModel.get(i)
            if (item.id === id) {
                notificationModel.setProperty(i, "message", message)
                notificationModel.setProperty(i, "progress", progress)
                // console.log("[NotificationOverlay] Updated notification at index:", i)
                break
            }
        }
    }
    
    function handleAllDismissed() {
        console.log("[NotificationOverlay] Handling dismiss all notifications")
        
        // Clean up all timers
        for (var timerId in activeTimers) {
            if (activeTimers[timerId]) {
                activeTimers[timerId].destroy()
            }
        }
        activeTimers = {}
        
        // Animate all out with stagger
        for (var i = 0; i < notificationModel.count; i++) {
            Qt.callLater(function(index) {
                return function() {
                    if (index < notificationModel.count) {
                        animateNotificationOut(index)
                    }
                }
            }(i), i * 100) // Stagger the animations
        }
    }
    
    // FIXED: Enhanced notification extension handling
    function handleNotificationExtended(id, additionalMs) {
        console.log("[NotificationOverlay] Extending notification:", id, "by", additionalMs, "ms")
        
        // Cancel existing timer
        if (activeTimers[id]) {
            activeTimers[id].destroy()
            delete activeTimers[id]
        }
        
        // Create new extended timer
        setupAutoDismissEnhanced(id, additionalMs)
    }
    
    // FIXED: Auto-dismiss timer setup
    function setupAutoDismiss(index, id, duration) {
        if (duration <= 0) return
        
        // Add small random offset to prevent all timers firing simultaneously
        var randomOffset = Math.floor(Math.random() * 100) // 0-99ms random offset
        var adjustedDuration = duration + randomOffset
        
        console.log("[NotificationOverlay] Setting up auto-dismiss for", id, "in", adjustedDuration, "ms (original:", duration, ")")
        
        var timer = Qt.createQmlObject('
            import QtQuick
            Timer {
                property string notificationId: ""
                property int modelIndex: -1
                running: true
                repeat: false
                onTriggered: {
                    console.log("[NotificationOverlay] QML Timer auto-dismissing notification:", notificationId)
                    if (notificationManagerInstance) {
                        notificationManagerInstance.dismissNotification(notificationId)
                    } else {
                        // Fallback to local dismissal
                        handleNotificationDismissed(notificationId)
                    }
                    destroy()
                }
            }
        ', notificationOverlay)
        
        timer.interval = adjustedDuration
        timer.notificationId = id
        timer.modelIndex = index
        
        // Store reference to timer in model
        if (index < notificationModel.count) {
            notificationModel.setProperty(index, "dismissTimer", timer)
        }
    }

    function animateNotificationInActual(item, index) {
        console.log("[NotificationOverlay] Animating in notification at index:", index)
        console.log("[NotificationOverlay] Item properties - x:", item.x, "y:", item.y, "width:", item.width, "height:", item.height)
        
        // Set initial state
        item.opacity = 0
        item.scale = 0.8
        
        // Calculate positions
        var targetX = getNotificationX()
        var targetY = getNotificationY(index)
        
        console.log("[NotificationOverlay] Target position - x:", targetX, "y:", targetY)
        
        // Set initial position (slightly offset)
        item.x = targetX + 50
        item.y = targetY
        
        // Make visible immediately
        item.visible = true
        notificationModel.setProperty(index, "visible", true)
        
        console.log("[NotificationOverlay] Starting entrance animation for index:", index)
        
        // Animate in with delay
        Qt.callLater(function() {
            // Create individual animations for this item
            var opacityAnim = Qt.createQmlObject(`
                import QtQuick
                NumberAnimation {
                    target: null
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: ${animationDuration}
                    easing.type: Easing.OutCubic
                }
            `, notificationOverlay)
            
            var scaleAnim = Qt.createQmlObject(`
                import QtQuick
                NumberAnimation {
                    target: null
                    property: "scale"
                    from: 0.8
                    to: 1.0
                    duration: ${animationDuration}
                    easing.type: Easing.OutBack
                    easing.overshoot: 1.2
                }
            `, notificationOverlay)
            
            var xAnim = Qt.createQmlObject(`
                import QtQuick
                NumberAnimation {
                    target: null
                    property: "x"
                    to: ${targetX}
                    duration: ${animationDuration}
                    easing.type: Easing.OutCubic
                }
            `, notificationOverlay)
            
            // Set targets and start
            opacityAnim.target = item
            scaleAnim.target = item
            xAnim.target = item
            
            opacityAnim.start()
            scaleAnim.start()
            xAnim.start()
            
            // Clean up animations when done
            opacityAnim.finished.connect(function() { opacityAnim.destroy() })
            scaleAnim.finished.connect(function() { scaleAnim.destroy() })
            xAnim.finished.connect(function() { xAnim.destroy() })
            
        }, index * staggerDelay)
    }
    
    function animateNotificationIn(index) {
        var item = notificationRepeater.itemAt(index)
        if (!item) {
            console.log("[NotificationOverlay] ERROR: No item found at index", index, "- repeater count:", notificationRepeater.count)
            console.log("[NotificationOverlay] Model count:", notificationModel.count)
            
            // Try to wait a bit and retry
            Qt.callLater(function() {
                var retryItem = notificationRepeater.itemAt(index)
                if (retryItem) {
                    console.log("[NotificationOverlay] Retry successful for index:", index)
                    animateNotificationInActual(retryItem, index)
                } else {
                    console.log("[NotificationOverlay] Retry failed for index:", index)
                }
            }, 100)
            return
        }
        
        animateNotificationInActual(item, index)
    }
    
    function animateNotificationOut(index) {
        // console.log("[NotificationOverlay] Animating out notification at index:", index)
        
        if (index < 0 || index >= notificationModel.count) {
            // console.log("[NotificationOverlay] Invalid index for animation out:", index)
            return
        }
        
        var item = notificationRepeater.itemAt(index)
        if (!item) {
            // console.log("[NotificationOverlay] No item found, removing directly from model")
            notificationModel.remove(index, 1)
            return
        }
        
        // Exit animation
        exitAnimation.target = item
        exitAnimation.targetIndex = index
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
        console.log("[NotificationOverlay] Calculating Y position for index:", index, "position:", position)
        
        switch (position) {
            case "topLeft":
            case "topRight":
                baseY = margin
                var calculatedY = baseY + index * (notificationHeight + spacing)
                console.log("[NotificationOverlay] Top position - baseY:", baseY, "calculatedY:", calculatedY, "index:", index)
                return calculatedY
                
            case "bottomLeft":
            case "bottomRight":
                baseY = parent.height - margin - notificationHeight
                var calculatedY = baseY - index * (notificationHeight + spacing)
                console.log("[NotificationOverlay] Bottom position - baseY:", baseY, "calculatedY:", calculatedY, "index:", index)
                return calculatedY
                
            case "center":
                var totalHeight = maxVisibleNotifications * (notificationHeight + spacing) - spacing
                baseY = (parent.height - totalHeight) / 2
                var calculatedY = baseY + index * (notificationHeight + spacing)
                console.log("[NotificationOverlay] Center position - baseY:", baseY, "calculatedY:", calculatedY, "index:", index)
                return calculatedY
                
            default:
                baseY = margin
                var calculatedY = baseY + index * (notificationHeight + spacing)
                console.log("[NotificationOverlay] Default position - baseY:", baseY, "calculatedY:", calculatedY, "index:", index)
                return calculatedY
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
    
    // FIXED: Enhanced animation definitions with proper cleanup
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
    
    // FIXED: Enhanced exit animation with proper cleanup
    ParallelAnimation {
        id: exitAnimation
        property var target: null
        property int targetIndex: -1
        
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
        
        onFinished: {
            // console.log("[NotificationOverlay] Exit animation finished for index:", targetIndex)
            if (targetIndex >= 0 && targetIndex < notificationModel.count) {
                notificationModel.remove(targetIndex, 1)
            }
            target = null
            targetIndex = -1
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
            
            // FIXED: Explicit positioning instead of relying on functions
            property int notificationIndex: index
            property real targetX: {
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
            
            property real targetY: {
                var baseY
                switch (position) {
                    case "topLeft":
                    case "topRight":
                        baseY = margin
                        return baseY + notificationIndex * (notificationHeight + spacing)
                    case "bottomLeft":
                    case "bottomRight":
                        baseY = parent.height - margin - notificationHeight
                        return baseY - notificationIndex * (notificationHeight + spacing)
                    case "center":
                        var totalHeight = maxVisibleNotifications * (notificationHeight + spacing) - spacing
                        baseY = (parent.height - totalHeight) / 2
                        return baseY + notificationIndex * (notificationHeight + spacing)
                    default:
                        baseY = margin
                        return baseY + notificationIndex * (notificationHeight + spacing)
                }
            }
            
            // Set initial position
            x: targetX
            y: targetY
            visible: false
            
            // Debug output
            Component.onCompleted: {
                console.log("[NotificationOverlay] Delegate created for index:", notificationIndex, 
                        "x:", x, "y:", y, "targetX:", targetX, "targetY:", targetY)
            }
            
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
                            text: model.title || "Notification"
                            font.pixelSize: compactMode ? 14 : 16
                            font.weight: Font.DemiBold
                            color: "#FFFFFF"
                            font.family: "Segoe UI"
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }
                        
                        Text {
                            text: model.message || ""
                            font.pixelSize: compactMode ? 12 : 14
                            color: "#B0B0B0"
                            font.family: "Segoe UI"
                            Layout.fillWidth: true
                            wrapMode: compactMode ? Text.NoWrap : Text.WordWrap
                            elide: compactMode ? Text.ElideRight : Text.ElideNone
                            maximumLineCount: compactMode ? 1 : 4
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
                        text: model.actionText || ""
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
                            if (notificationManagerInstance && model.actionId) {
                                notificationManagerInstance.handleNotificationAction(model.id, model.actionId)
                            }
                        }
                    }
                    
                    // FIXED: Close button that works for ALL notification types including errors
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
                                // console.log("[NotificationOverlay] Close button clicked for notification:", model.id)
                                
                                // Clean up timer using the activeTimers object
                                if (notificationOverlay.activeTimers[model.id]) {
                                    notificationOverlay.activeTimers[model.id].destroy()
                                    delete notificationOverlay.activeTimers[model.id]
                                }
                                
                                // Always allow manual dismissal
                                if (notificationManagerInstance) {
                                    console.log("[NotificationOverlay] Calling dismissNotification via manager")
                                    notificationManagerInstance.dismissNotification(model.id)
                                } else {
                                    console.log("[NotificationOverlay] Fallback to local dismiss")
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
    
    // FIXED: Queue indicator with actual queue count
    Rectangle {
        id: queueIndicator
        visible: notificationManagerInstance ? notificationManagerInstance.queueCount > 0 : false
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
            text: notificationManagerInstance ? (notificationManagerInstance.queueCount + " queued") : "Queued"
            color: "#00D4AA"
            font.family: "Segoe UI"
            font.pixelSize: 12
            font.weight: Font.Medium
        }
        
        MouseArea {
            anchors.fill: parent
            onClicked: {
                // Handle queue expansion or show notification center
                // console.log("[NotificationOverlay] Queue indicator clicked - showing queued notifications")
                if (notificationManagerInstance) {
                    // Could implement a function to show all queued notifications
                }
            }
        }
        
        // Pulsing animation for queue indicator
        SequentialAnimation on opacity {
            running: queueIndicator.visible
            loops: Animation.Infinite
            NumberAnimation { to: 0.6; duration: 1000 }
            NumberAnimation { to: 0.9; duration: 1000 }
        }
    }
    
    // FIXED: Enhanced notification test functions
    function testNotifications() {
        if (!notificationManagerInstance) {
            // console.log("[NotificationOverlay] Cannot test - no notification manager instance")
            return
        }
        
        // console.log("[NotificationOverlay] Starting notification tests...")
        
        // Test consecutive notifications to verify queue works
        notificationManagerInstance.info("Test 1", "First info notification")
        notificationManagerInstance.info("Test 2", "Second info notification")
        notificationManagerInstance.info("Test 3", "Third info notification")
        notificationManagerInstance.info("Test 4", "Fourth info notification")
        notificationManagerInstance.info("Test 5", "Fifth info notification")
        notificationManagerInstance.info("Test 6", "Sixth info notification - should be queued")
        notificationManagerInstance.info("Test 7", "Seventh info notification - should be queued")
        
        Qt.callLater(function() {
            notificationManagerInstance.success("Success!", "Operation completed successfully")
        }, 1000)
        
        Qt.callLater(function() {
            notificationManagerInstance.warning("Warning", "Please check your settings")
        }, 2000)
        
        Qt.callLater(function() {
            // Test error notification that should be dismissible
            notificationManagerInstance.error("Error Test", "This error should be manually dismissible")
        }, 3000)
        
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
                    timer.destroy()
                } else {
                    notificationManagerInstance.updateTask(taskId, Math.floor(progress), "Installing... " + Math.floor(progress) + "%")
                }
            })
            timer.start()
        }, 4000)
    }
    
    function testErrorNotification() {
        if (!notificationManagerInstance) return
        notificationManagerInstance.error("Critical Error", "This is a test error notification that should be dismissible")
    }
    
    function testQueue() {
        if (!notificationManagerInstance) return
        
        // console.log("[NotificationOverlay] Testing queue with rapid notifications...")
        for (var i = 1; i <= 10; i++) {
            notificationManagerInstance.info("Queue Test " + i, "Testing notification queue system - item " + i)
        }
    }
    
    // FIXED: Enhanced shortcuts for testing
    Shortcut {
        sequence: "Ctrl+Shift+N"
        onActivated: testNotifications()
    }
    
    Shortcut {
        sequence: "Ctrl+Shift+E"
        onActivated: testErrorNotification()
    }
    
    Shortcut {
        sequence: "Ctrl+Shift+Q"
        onActivated: testQueue()
    }
    
    Shortcut {
        sequence: "Ctrl+Shift+C"
        onActivated: {
            if (notificationManagerInstance) {
                notificationManagerInstance.dismissAll()
            }
        }
    }
}