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
    m_queueTimer->setSingleShot(true);
    m_queueTimer->setInterval(100); // Process queue every 100ms
    connect(m_queueTimer, &QTimer::timeout, this, &NotificationManager::processQueue);

    m_cleanupTimer = new QTimer(this);
    m_cleanupTimer->setInterval(30000); // Cleanup every 30 seconds
    connect(m_cleanupTimer, &QTimer::timeout, this, &NotificationManager::cleanupOldNotifications);
    m_cleanupTimer->start();

    qDebug() << "[NotificationManager] Initialized";
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

    if (shouldQueue()) {
        m_queue.enqueue(data);
        if (!m_queueTimer->isActive()) {
            m_queueTimer->start();
        }
        return data.id;
    }

    // Add directly
    m_activeNotifications.append(data);
    addToHistory(data);
    
    emit notificationAdded(data.id, data.title, data.message, 
                          static_cast<int>(data.level), data.duration, 
                          data.category, data.progress, data.actionText, data.actionId);
    
    // Auto-dismiss timer
    if (data.duration > 0) {
        QTimer::singleShot(data.duration, this, [this, id = data.id](){
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

// ───────────────────────────────────────────────────────────────
// Dismiss methods
// ───────────────────────────────────────────────────────────────
void NotificationManager::dismissNotification(const QString &id)
{
    // Remove from active notifications
    for (int i = 0; i < m_activeNotifications.size(); ++i) {
        if (m_activeNotifications[i].id == id) {
            m_activeNotifications.removeAt(i);
            emit notificationDismissed(id);
            
            // Process next in queue if available
            if (!m_queue.isEmpty()) {
                processNextInQueue();
            }
            return;
        }
    }

    // Remove from active tasks
    if (m_activeTasks.contains(id)) {
        m_activeTasks.remove(id);
        emit notificationDismissed(id);
    }
}

void NotificationManager::dismissAll()
{
    m_activeNotifications.clear();
    m_activeTasks.clear();
    m_queue.clear();
    emit allNotificationsDismissed();
}

void NotificationManager::dismissCategory(const QString &category)
{
    // Remove from active notifications
    m_activeNotifications.erase(
        std::remove_if(m_activeNotifications.begin(), m_activeNotifications.end(),
                      [&category](const NotificationData &data) {
                          return data.category == category;
                      }),
        m_activeNotifications.end());

    // Remove from queue
    QQueue<NotificationData> filteredQueue;
    while (!m_queue.isEmpty()) {
        NotificationData data = m_queue.dequeue();
        if (data.category != category) {
            filteredQueue.enqueue(data);
        }
    }
    m_queue = filteredQueue;

    emit categoryDismissed(category);
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
    return showNotification(title, message, 3, 0, category); // Errors don't auto-dismiss
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
    
    emit notificationAdded(task.id, task.title, task.message, 
                          static_cast<int>(task.level), task.duration, 
                          task.category, task.progress, task.actionText, task.actionId);

    m_totalCount++;
    m_unreadCount++;
    emit totalNotificationsChanged();
    emit unreadCountChanged();

    qDebug() << "[NotificationManager] taskId.id =" << task.id << "message =" << task.message;
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
    qDebug() << "[NotificationManager] taskId.id =" << task.id << "message =" << task.message;
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
    qDebug() << "[NotificationManager] taskId.id =" << task.id << "message =" << task.message;
}

void NotificationManager::failTask(const QString &taskId, const QString &error)
{
    if (!m_activeTasks.contains(taskId)) return;
    
    auto task = m_activeTasks.take(taskId);
    task.level = NotificationLevel::Error;
    task.progress = -1; // Hide progress bar for errors
    task.message = error.isEmpty() ? "Task failed" : error;
    task.duration = 0; // Errors require manual dismissal
    
    addToHistory(task);
    
    emit notificationUpdated(taskId, task.message, task.progress);
    qDebug() << "[NotificationManager] taskId.id =" << taskId << "message =" << task.message;
}

// ───────────────────────────────────────────────────────────────
// Settings
// ───────────────────────────────────────────────────────────────
void NotificationManager::setMaxVisibleNotifications(int max)
{
    if (m_maxVisible != max) {
        m_maxVisible = qMax(1, max);
        emit maxVisibleNotificationsChanged();
        
        // Process queue if we now have more space
        if (m_activeNotifications.size() < m_maxVisible && !m_queue.isEmpty()) {
            processQueue();
        }
    }
}

void NotificationManager::setGlobalMute(bool mute)
{
    if (m_globalMute != mute) {
        m_globalMute = mute;
        emit globalMuteChanged();
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
}

void NotificationManager::markAllAsRead()
{
    m_unreadCount = 0;
    emit unreadCountChanged();
}

// ───────────────────────────────────────────────────────────────
// Slots
// ───────────────────────────────────────────────────────────────
void NotificationManager::handleNotificationAction(const QString &id, const QString &actionId)
{
    emit notificationAction(id, actionId);
}

void NotificationManager::handleNotificationClick(const QString &id)
{
    // Mark as read when clicked
    if (m_unreadCount > 0) {
        m_unreadCount--;
        emit unreadCountChanged();
    }
}

// ───────────────────────────────────────────────────────────────
// Private methods
// ───────────────────────────────────────────────────────────────
void NotificationManager::processQueue()
{
    while (!m_queue.isEmpty() && m_activeNotifications.size() < m_maxVisible) {
        processNextInQueue();
    }
    
    if (!m_queue.isEmpty()) {
        emit queueChanged();
    }
}

void NotificationManager::handleAutoDissmiss()
{
    // This is handled by individual QTimer::singleShot calls
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

void NotificationManager::cleanupOldNotifications()
{
    // Remove notifications older than 24 hours from history
    QDateTime cutoff = QDateTime::currentDateTime().addDays(-1);
    
    m_history.erase(
        std::remove_if(m_history.begin(), m_history.end(),
                      [cutoff](const NotificationData &data) {
                          return data.timestamp < cutoff;
                      }),
        m_history.end());
}

bool NotificationManager::shouldQueue() const
{
    return m_activeNotifications.size() >= m_maxVisible;
}

void NotificationManager::processNextInQueue()
{
    if (m_queue.isEmpty() || m_activeNotifications.size() >= m_maxVisible) {
        return;
    }
    
    NotificationData data = m_queue.dequeue();
    m_activeNotifications.append(data);
    addToHistory(data);
    
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