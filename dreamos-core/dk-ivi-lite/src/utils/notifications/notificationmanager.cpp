#include "notificationmanager.hpp"
#include <QJsonDocument>
#include <QCoreApplication>
#include <QDebug>

// ───────────────────────────────────────────────────────────────
// Constructor
// ───────────────────────────────────────────────────────────────
NotificationManager::NotificationManager(QObject *parent) 
    : QObject(parent)
{
    // Initialize timers
    m_queueTimer = new QTimer(this);
    m_queueTimer->setSingleShot(false); // Changed to repeating timer
    m_queueTimer->setInterval(300); // Process queue every 300ms for better UX
    connect(m_queueTimer, &QTimer::timeout, this, &NotificationManager::processQueue);

    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(30000); // Cleanup every 30 seconds
    connect(m_cleanupTimer, &QTimer::timeout, this, &NotificationManager::cleanupOldNotifications);
    m_cleanupTimer->start();

    qDebug() << "[NotificationManager] Initialized with max visible:" << m_maxVisible;
}

// ───────────────────────────────────────────────────────────────
// Core notification methods
// ───────────────────────────────────────────────────────────────
QString NotificationManager::showNotification(const QString &title, 
                                             const QString &message,
                                             int level,
                                             int duration,
                                             const QString &category)
{
    if (m_globalMute && level < 3) { // Don't mute errors
        return QString();
    }

    NotificationData data;
    data.id = generateId();
    data.title = title;
    data.message = message;
    data.level = static_cast<NotificationLevel>(level);
    data.duration = duration;
    data.category = category;
    data.timestamp = QDateTime::currentDateTime();

    qDebug() << "[NotificationManager] Creating notification:" << data.id << "Title:" << title << "Level:" << level;

    // Check if we should queue this notification
    if (shouldQueue()) {
        qDebug() << "[NotificationManager] Queueing notification" << data.id << "- active count:" << m_activeNotifications.size();
        m_queue.enqueue(data);
        startQueueProcessing();
        emit queueCountChanged();
        addToHistory(data); // Add to history even when queued
        return data.id;
    }

    // Add directly to active notifications
    m_activeNotifications.append(data);
    addToHistory(data);
    
    qDebug() << "[NotificationManager] Showing notification immediately:" << data.id << "Active count:" << m_activeNotifications.size();
    
    emit notificationAdded(data.id, data.title, data.message, 
                          static_cast<int>(data.level), data.duration, 
                          data.category, data.progress, data.actionText, data.actionId);
    
    // Auto-dismiss timer (only for non-error notifications or those with explicit duration)
    if (data.duration > 0) {
        QTimer::singleShot(data.duration, this, [this, id = data.id](){
            qDebug() << "[NotificationManager] Auto-dismissing notification:" << id;
            dismissNotification(id);
        });
    }

    m_totalCount++;
    m_unreadCount++;
    emit totalNotificationsChanged();
    emit unreadCountChanged();

    return data.id;
}

QString NotificationManager::showProgress(const QString &title,
                                        const QString &message,
                                        int progress,
                                        const QString &category)
{
    NotificationData data;
    data.id = generateId();
    data.title = title;
    data.message = message;
    data.level = NotificationLevel::Progress;
    data.duration = 0; // Progress notifications don't auto-dismiss
    data.progress = qBound(0, progress, 100);
    data.category = category;
    data.timestamp = QDateTime::currentDateTime();

    if (shouldQueue()) {
        m_queue.enqueue(data);
        startQueueProcessing();
        emit queueCountChanged();
        addToHistory(data);
        return data.id;
    }

    m_activeNotifications.append(data);
    addToHistory(data);
    
    emit notificationAdded(data.id, data.title, data.message, 
                          static_cast<int>(data.level), data.duration, 
                          data.category, data.progress, data.actionText, data.actionId);

    m_totalCount++;
    m_unreadCount++;
    emit totalNotificationsChanged();
    emit unreadCountChanged();

    return data.id;
}

void NotificationManager::updateProgress(const QString &id, int progress, const QString &message)
{
    // Update active notification
    for (auto &notification : m_activeNotifications) {
        if (notification.id == id) {
            notification.progress = qBound(0, progress, 100);
            if (!message.isEmpty()) {
                notification.message = message;
            }
            
            emit notificationUpdated(id, notification.message, notification.progress);
            
            // Auto-complete at 100%
            if (progress >= 100) {
                QTimer::singleShot(1500, this, [this, id](){
                    completeTask(id);
                });
            }
            return;
        }
    }

    // Update task if it exists
    if (m_activeTasks.contains(id)) {
        auto &task = m_activeTasks[id];
        task.progress = qBound(0, progress, 100);
        if (!message.isEmpty()) {
            task.message = message;
        }
        emit notificationUpdated(id, task.message, task.progress);
    }
}

QString NotificationManager::smartNotify(const QString &title, 
                                        const QString &message,
                                        int level,
                                        const QString &category,
                                        const QString &groupId)
{
    // Check if we should update existing instead of creating new
    if (!groupId.isEmpty() && m_groupToNotificationMap.contains(groupId)) {
        QString existingId = m_groupToNotificationMap[groupId];
        QString updatedId = updateExisting(existingId, message, level);
        if (!updatedId.isEmpty()) {
            extendDuration(updatedId, 3000);
            return updatedId;
        }
    }
    
    // Check if it's too soon for this category
    if (isTooSoon(category)) {
        // Try to find similar notification to update
        QString similarId = findSimilarNotification(title, category);
        if (!similarId.isEmpty()) {
            return updateExisting(similarId, message, level);
        }
    }
    
    // Check if we should batch notifications
    if (shouldBatch(category)) {
        QString batchId = category + "_batch_" + QString::number(QDateTime::currentMSecsSinceEpoch());
        startBatch(batchId);
        addToBatch(batchId, title, message, level);
        
        // Auto-commit batch after short delay
        QTimer::singleShot(1000, this, [this, batchId]() {
            commitBatch(batchId);
        });
        
        return batchId;
    }
    
    // Create new notification
    QString notificationId = showNotification(title, message, level, 5000, category);
    
    // Update mappings
    if (!groupId.isEmpty()) {
        m_groupToNotificationMap[groupId] = notificationId;
    }
    m_lastNotificationTime[category] = QDateTime::currentDateTime();
    m_categoryToNotificationMap[category] = notificationId;
    
    return notificationId;
}

void NotificationManager::updateOrCreate(const QString &groupId,
                                        const QString &title,
                                        const QString &message,
                                        int level,
                                        const QString &category)
{
    if (groupId.isEmpty()) {
        showNotification(title, message, level, 5000, category);
        return;
    }
    
    if (m_groupToNotificationMap.contains(groupId)) {
        // Update existing
        QString existingId = m_groupToNotificationMap[groupId];
        updateExisting(existingId, message, level);
        extendDuration(existingId, 2000);
    } else {
        // Create new
        QString newId = showNotification(title, message, level, 5000, category);
        m_groupToNotificationMap[groupId] = newId;
    }
}

QString NotificationManager::updateExisting(const QString &notificationId,
                                           const QString &newMessage,
                                           int newLevel)
{
    // Update active notification
    for (auto &notification : m_activeNotifications) {
        if (notification.id == notificationId) {
            notification.message = newMessage;
            if (newLevel >= 0) {
                notification.level = static_cast<NotificationLevel>(newLevel);
            }
            
            emit notificationUpdated(notificationId, newMessage, notification.progress);
            return notificationId;
        }
    }
    
    // Update active task
    if (m_activeTasks.contains(notificationId)) {
        auto &task = m_activeTasks[notificationId];
        task.message = newMessage;
        if (newLevel >= 0) {
            task.level = static_cast<NotificationLevel>(newLevel);
        }
        
        emit notificationUpdated(notificationId, newMessage, task.progress);
        return notificationId;
    }
    
    return QString();
}

void NotificationManager::extendDuration(const QString &notificationId, int additionalMs)
{
    // Emit signal so QML can handle timer extension
    emit notificationExtended(notificationId, additionalMs);
    
    qDebug() << "[NotificationManager] Extended notification" << notificationId << "by" << additionalMs << "ms";
}

QString NotificationManager::categoryNotify(const QString &category,
                                           const QString &title,
                                           const QString &message,
                                           int level,
                                           bool replaceExisting)
{
    if (replaceExisting && m_categoryToNotificationMap.contains(category)) {
        QString existingId = m_categoryToNotificationMap[category];
        QString updatedId = updateExisting(existingId, message, level);
        if (!updatedId.isEmpty()) {
            extendDuration(updatedId, 3000);
            return updatedId;
        }
    }
    
    QString newId = showNotification(title, message, level, 5000, category);
    m_categoryToNotificationMap[category] = newId;
    return newId;
}

// Batch operations
void NotificationManager::startBatch(const QString &batchId)
{
    m_batchedNotifications[batchId] = QList<NotificationData>();
    qDebug() << "[NotificationManager] Started batch" << batchId;
}

void NotificationManager::addToBatch(const QString &batchId, 
                                    const QString &title, 
                                    const QString &message, 
                                    int level)
{
    if (!m_batchedNotifications.contains(batchId)) {
        startBatch(batchId);
    }
    
    NotificationData data;
    data.id = generateId();
    data.title = title;
    data.message = message;
    data.level = static_cast<NotificationLevel>(level);
    data.duration = 5000;
    data.category = "batch";
    data.timestamp = QDateTime::currentDateTime();
    
    m_batchedNotifications[batchId].append(data);
    qDebug() << "[NotificationManager] Added to batch" << batchId << ":" << title;
}

void NotificationManager::commitBatch(const QString &batchId, int maxNotifications)
{
    if (!m_batchedNotifications.contains(batchId)) {
        qDebug() << "[NotificationManager] Batch" << batchId << "not found";
        return;
    }
    
    auto notifications = m_batchedNotifications.take(batchId);
    qDebug() << "[NotificationManager] Committing batch" << batchId << "with" << notifications.size() << "notifications";
    
    if (notifications.size() <= maxNotifications) {
        // Show all notifications individually
        for (const auto &data : notifications) {
            if (shouldQueue()) {
                m_queue.enqueue(data);
            } else {
                m_activeNotifications.append(data);
                emit notificationAdded(data.id, data.title, data.message, 
                                      static_cast<int>(data.level), data.duration, 
                                      data.category, data.progress, data.actionText, data.actionId);
                                      
                // Auto-dismiss timer
                if (data.duration > 0) {
                    QTimer::singleShot(data.duration, this, [this, id = data.id](){
                        dismissNotification(id);
                    });
                }
            }
            addToHistory(data);
        }
        
        if (!m_queue.isEmpty()) {
            startQueueProcessing();
            emit queueCountChanged();
        }
    } else {
        // Create summary notification
        QString summaryTitle = QString("Multiple Updates (%1)").arg(notifications.size());
        QString summaryMessage;
        
        if (notifications.size() > 0) {
            summaryMessage = QString("Latest: %1").arg(notifications.last().message);
        }
        
        showNotification(summaryTitle, summaryMessage, 0, 7000, "batch_summary");
        qDebug() << "[NotificationManager] Created summary notification for" << notifications.size() << "items";
    }
    
    m_totalCount += notifications.size();
    m_unreadCount += notifications.size();
    emit totalNotificationsChanged();
    emit unreadCountChanged();
}

// Helper methods
bool NotificationManager::shouldBatch(const QString &category) const
{
    if (!m_enableSmartBatching) return false;
    
    // Count recent notifications in this category
    int recentCount = 0;
    QDateTime cutoff = QDateTime::currentDateTime().addSecs(-10); // Last 10 seconds
    
    for (const auto &data : m_history) {
        if (data.category == category && data.timestamp > cutoff) {
            recentCount++;
        }
    }
    
    return recentCount >= m_maxSimilarNotifications;
}

bool NotificationManager::isTooSoon(const QString &category) const
{
    if (!m_lastNotificationTime.contains(category)) {
        return false;
    }
    
    QDateTime lastTime = m_lastNotificationTime[category];
    return lastTime.msecsTo(QDateTime::currentDateTime()) < m_minIntervalMs;
}

QString NotificationManager::findSimilarNotification(const QString &title, const QString &category) const
{
    for (const auto &notification : m_activeNotifications) {
        if (notification.category == category && notification.title == title) {
            return notification.id;
        }
    }
    return QString();
}

// ───────────────────────────────────────────────────────────────
// Dismiss methods - FIXED to properly handle errors
// ───────────────────────────────────────────────────────────────
void NotificationManager::dismissNotification(const QString &id)
{
    qDebug() << "[NotificationManager] Dismissing notification:" << id;
    
    // Clean up group mappings
    for (auto it = m_groupToNotificationMap.begin(); it != m_groupToNotificationMap.end();) {
        if (it.value() == id) {
            it = m_groupToNotificationMap.erase(it);
        } else {
            ++it;
        }
    }
    
    // Clean up category mappings
    for (auto it = m_categoryToNotificationMap.begin(); it != m_categoryToNotificationMap.end();) {
        if (it.value() == id) {
            it = m_categoryToNotificationMap.erase(it);
        } else {
            ++it;
        }
    }
    
    // Remove from active notifications
    bool found = false;
    for (int i = 0; i < m_activeNotifications.size(); ++i) {
        if (m_activeNotifications[i].id == id) {
            m_activeNotifications.removeAt(i);
            found = true;
            qDebug() << "[NotificationManager] Removed notification from active list. Remaining count:" << m_activeNotifications.size();
            break;
        }
    }

    // Remove from active tasks
    if (m_activeTasks.contains(id)) {
        m_activeTasks.remove(id);
        found = true;
        qDebug() << "[NotificationManager] Removed notification from active tasks";
    }
    
    if (found) {
        emit notificationDismissed(id);
        
        // Process next in queue if available
        if (!m_queue.isEmpty() && m_activeNotifications.size() < m_maxVisible) {
            qDebug() << "[NotificationManager] Processing next queued notification";
            processNextInQueue();
        }
        
        // Stop queue processing if queue is empty
        if (m_queue.isEmpty() && m_queueTimer->isActive()) {
            m_queueTimer->stop();
            emit queueCountChanged();
        }
    } else {
        qDebug() << "[NotificationManager] Warning: Notification" << id << "not found for dismissal";
    }
}

void NotificationManager::dismissAll()
{
    qDebug() << "[NotificationManager] Dismissing all notifications";
    m_activeNotifications.clear();
    m_activeTasks.clear();
    m_queue.clear();
    
    if (m_queueTimer->isActive()) {
        m_queueTimer->stop();
    }
    
    emit allNotificationsDismissed();
    emit queueCountChanged();
}

void NotificationManager::dismissCategory(const QString &category)
{
    qDebug() << "[NotificationManager] Dismissing category:" << category;
    
    // Remove from active notifications
    int removedCount = 0;
    auto it = m_activeNotifications.begin();
    while (it != m_activeNotifications.end()) {
        if (it->category == category) {
            it = m_activeNotifications.erase(it);
            removedCount++;
        } else {
            ++it;
        }
    }

    // Remove from queue
    QQueue<NotificationData> filteredQueue;
    while (!m_queue.isEmpty()) {
        NotificationData data = m_queue.dequeue();
        if (data.category != category) {
            filteredQueue.enqueue(data);
        } else {
            removedCount++;
        }
    }
    m_queue = filteredQueue;

    qDebug() << "[NotificationManager] Removed" << removedCount << "notifications from category" << category;
    
    emit categoryDismissed(category);
    emit queueCountChanged();
    
    // Process queue if we now have space
    if (!m_queue.isEmpty() && m_activeNotifications.size() < m_maxVisible) {
        startQueueProcessing();
    }
}

// ───────────────────────────────────────────────────────────────
// Convenience methods
// ───────────────────────────────────────────────────────────────
QString NotificationManager::info(const QString &title, const QString &message, const QString &category)
{
    return showNotification(title, message, 0, 5000, category);
}

QString NotificationManager::success(const QString &title, const QString &message, const QString &category)
{
    return showNotification(title, message, 1, 4000, category);
}

QString NotificationManager::warning(const QString &title, const QString &message, const QString &category)
{
    return showNotification(title, message, 2, 6000, category);
}

QString NotificationManager::error(const QString &title, const QString &message, const QString &category)
{
    return showNotification(title, message, 3, 0, category); // Errors don't auto-dismiss but CAN be manually dismissed
}

// ───────────────────────────────────────────────────────────────
// Task tracking methods
// ───────────────────────────────────────────────────────────────
QString NotificationManager::startTask(const QString &taskName, const QString &description)
{
    QString taskId = generateId();
    
    NotificationData task;
    task.id = taskId;
    task.title = taskName;
    task.message = description.isEmpty() ? "Starting..." : description;
    task.level = NotificationLevel::Progress;
    task.duration = 0;
    task.progress = 0;
    task.category = "task";
    task.timestamp = QDateTime::currentDateTime();

    m_activeTasks[taskId] = task;
    
    // Don't queue tasks - they should show immediately
    if (m_activeNotifications.size() >= m_maxVisible) {
        // Remove oldest non-error notification to make space for task
        for (int i = 0; i < m_activeNotifications.size(); ++i) {
            if (m_activeNotifications[i].level != NotificationLevel::Error) {
                m_activeNotifications.removeAt(i);
                break;
            }
        }
    }
    
    emit notificationAdded(task.id, task.title, task.message, 
                          static_cast<int>(task.level), task.duration, 
                          task.category, task.progress, task.actionText, task.actionId);

    m_totalCount++;
    m_unreadCount++;
    emit totalNotificationsChanged();
    emit unreadCountChanged();

    qDebug() << "[NotificationManager] Started task:" << task.id << "message:" << task.message;
    return taskId;
}

void NotificationManager::updateTask(const QString &taskId, int progress, const QString &status)
{
    if (!m_activeTasks.contains(taskId)) return;
    
    auto &task = m_activeTasks[taskId];
    task.progress = qBound(0, progress, 100);
    if (!status.isEmpty()) {
        task.message = status;
    }
    
    emit notificationUpdated(taskId, task.message, task.progress);
    qDebug() << "[NotificationManager] Updated task:" << task.id << "progress:" << progress << "message:" << task.message;
}

void NotificationManager::completeTask(const QString &taskId, const QString &result)
{
    if (!m_activeTasks.contains(taskId)) return;
    
    auto task = m_activeTasks.take(taskId);
    task.level = NotificationLevel::Success;
    task.progress = 100;
    task.message = result.isEmpty() ? "Completed successfully" : result;
    task.duration = 3000;
    
    addToHistory(task);
    
    emit notificationUpdated(taskId, task.message, task.progress);
    
    // Auto-dismiss after showing success
    QTimer::singleShot(task.duration, this, [this, taskId](){
        dismissNotification(taskId);
    });
    qDebug() << "[NotificationManager] Completed task:" << task.id << "message:" << task.message;
}

void NotificationManager::failTask(const QString &taskId, const QString &error)
{
    if (!m_activeTasks.contains(taskId)) return;
    
    auto task = m_activeTasks.take(taskId);
    task.level = NotificationLevel::Error;
    task.progress = -1; // Hide progress bar for errors
    task.message = error.isEmpty() ? "Task failed" : error;
    task.duration = 0; // Errors require manual dismissal but CAN be dismissed
    
    addToHistory(task);
    
    emit notificationUpdated(taskId, task.message, task.progress);
    qDebug() << "[NotificationManager] Failed task:" << taskId << "message:" << task.message;
}

// ───────────────────────────────────────────────────────────────
// Settings
// ───────────────────────────────────────────────────────────────
void NotificationManager::setMaxVisibleNotifications(int max)
{
    if (m_maxVisible != max) {
        m_maxVisible = qMax(1, max);
        emit maxVisibleNotificationsChanged();
        
        qDebug() << "[NotificationManager] Max visible notifications set to:" << m_maxVisible;
        
        // If we now have more space, process queue
        if (m_activeNotifications.size() < m_maxVisible && !m_queue.isEmpty()) {
            startQueueProcessing();
        }
        
        // If we have too many active, move excess to queue
        while (m_activeNotifications.size() > m_maxVisible) {
            NotificationData data = m_activeNotifications.takeLast();
            m_queue.prepend(data); // Add to front of queue to maintain order
            emit notificationDismissed(data.id);
        }
        
        if (!m_queue.isEmpty()) {
            emit queueCountChanged();
        }
    }
}

void NotificationManager::setGlobalMute(bool mute)
{
    if (m_globalMute != mute) {
        m_globalMute = mute;
        emit globalMuteChanged();
        qDebug() << "[NotificationManager] Global mute set to:" << mute;
    }
}

// ───────────────────────────────────────────────────────────────
// History and filtering
// ───────────────────────────────────────────────────────────────
QJsonArray NotificationManager::getHistory(int limit) const
{
    QJsonArray array;
    int count = 0;
    
    for (auto it = m_history.rbegin(); it != m_history.rend() && count < limit; ++it, ++count) {
        const auto &data = *it;
        QJsonObject obj;
        obj["id"] = data.id;
        obj["title"] = data.title;
        obj["message"] = data.message;
        obj["level"] = static_cast<int>(data.level);
        obj["category"] = data.category;
        obj["timestamp"] = data.timestamp.toString(Qt::ISODate);
        obj["progress"] = data.progress;
        array.append(obj);
    }
    
    return array;
}

QJsonArray NotificationManager::getByCategory(const QString &category) const
{
    QJsonArray array;
    
    for (const auto &data : m_history) {
        if (data.category == category) {
            QJsonObject obj;
            obj["id"] = data.id;
            obj["title"] = data.title;
            obj["message"] = data.message;
            obj["level"] = static_cast<int>(data.level);
            obj["timestamp"] = data.timestamp.toString(Qt::ISODate);
            obj["progress"] = data.progress;
            array.append(obj);
        }
    }
    
    return array;
}

void NotificationManager::clearHistory()
{
    m_history.clear();
    m_totalCount = 0;
    emit totalNotificationsChanged();
    qDebug() << "[NotificationManager] History cleared";
}

void NotificationManager::markAllAsRead()
{
    m_unreadCount = 0;
    emit unreadCountChanged();
    qDebug() << "[NotificationManager] All notifications marked as read";
}

// ───────────────────────────────────────────────────────────────
// Slots
// ───────────────────────────────────────────────────────────────
void NotificationManager::handleNotificationAction(const QString &id, const QString &actionId)
{
    qDebug() << "[NotificationManager] Notification action:" << id << "action:" << actionId;
    emit notificationAction(id, actionId);
}

void NotificationManager::handleNotificationClick(const QString &id)
{
    qDebug() << "[NotificationManager] Notification clicked:" << id;
    
    // Mark as read when clicked
    if (m_unreadCount > 0) {
        m_unreadCount--;
        emit unreadCountChanged();
    }
}

// ───────────────────────────────────────────────────────────────
// Private methods - FIXED queue processing
// ───────────────────────────────────────────────────────────────
void NotificationManager::processQueue()
{
    if (m_queue.isEmpty()) {
        m_queueTimer->stop();
        emit queueCountChanged();
        return;
    }
    
    // Process notifications from queue while we have space
    while (!m_queue.isEmpty() && m_activeNotifications.size() < m_maxVisible) {
        processNextInQueue();
    }
    
    // Update queue count
    emit queueCountChanged();
    
    // Stop timer if queue is empty
    if (m_queue.isEmpty()) {
        m_queueTimer->stop();
        qDebug() << "[NotificationManager] Queue processing completed - all notifications shown";
    }
}

void NotificationManager::handleAutoDissmiss()
{
    // This is handled by individual QTimer::singleShot calls
}

void NotificationManager::cleanupOldNotifications()
{
    // Remove notifications older than 24 hours from history
    QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    
    int originalSize = m_history.size();
    
    m_history.erase(
        std::remove_if(m_history.begin(), m_history.end(),
                      [cutoff](const NotificationData &data) {
                          return data.timestamp < cutoff;
                      }),
        m_history.end());
        
    int removedCount = originalSize - m_history.size();
    if (removedCount > 0) {
        qDebug() << "[NotificationManager] Cleaned up" << removedCount << "old notifications from history";
    }
}

QString NotificationManager::generateId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

void NotificationManager::addToHistory(const NotificationData &data)
{
    m_history.append(data);
    
    // Limit history size
    while (m_history.size() > m_maxHistory) {
        m_history.removeFirst();
    }
}

bool NotificationManager::shouldQueue() const
{
    bool shouldQueue = m_activeNotifications.size() >= m_maxVisible;
    if (shouldQueue) {
        qDebug() << "[NotificationManager] Should queue: active=" << m_activeNotifications.size() << "max=" << m_maxVisible;
    }
    return shouldQueue;
}

void NotificationManager::processNextInQueue()
{
    if (m_queue.isEmpty() || m_activeNotifications.size() >= m_maxVisible) {
        return;
    }
    
    NotificationData data = m_queue.dequeue();
    m_activeNotifications.append(data);
    
    qDebug() << "[NotificationManager] Processing queued notification:" << data.id 
             << "Active count:" << m_activeNotifications.size() 
             << "Queue remaining:" << m_queue.size();
    
    emit notificationAdded(data.id, data.title, data.message, 
                          static_cast<int>(data.level), data.duration, 
                          data.category, data.progress, data.actionText, data.actionId);
    
    // Auto-dismiss timer
    if (data.duration > 0) {
        QTimer::singleShot(data.duration, this, [this, id = data.id](){
            dismissNotification(id);
        });
    }
}

void NotificationManager::startQueueProcessing()
{
    if (!m_queueTimer->isActive() && !m_queue.isEmpty()) {
        qDebug() << "[NotificationManager] Starting queue processing - queue size:" << m_queue.size();
        m_queueTimer->start();
    }
}