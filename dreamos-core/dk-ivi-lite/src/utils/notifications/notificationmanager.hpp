#ifndef NOTIFICATIONMANAGER_H
#define NOTIFICATIONMANAGER_H

#include <QObject>
#include <QTimer>
#include <QQueue>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDateTime>
#include <QUuid>

// ───────────────────────────────────────────────────────────────
// Notification Types and Levels
// ───────────────────────────────────────────────────────────────
enum class NotificationLevel {
    Info,
    Success,
    Warning,
    Error,
    Progress
};

Q_DECLARE_METATYPE(NotificationLevel)

// ───────────────────────────────────────────────────────────────
// Notification Data Structure
// ───────────────────────────────────────────────────────────────
struct NotificationData {
    QString id;
    QString title;
    QString message;
    NotificationLevel level;
    int duration;           // Auto-dismiss duration in ms (0 = no auto-dismiss)
    int progress;           // Progress percentage (0-100, -1 = no progress)
    QString category;       // Category for grouping
    QString actionText;     // Optional action button text
    QString actionId;       // Action identifier
    QDateTime timestamp;
    bool persistent;        // Whether notification persists across app sessions
    bool autoQueue;         // Whether to queue if too many notifications
    
    NotificationData() : 
        level(NotificationLevel::Info), 
        duration(5000), 
        progress(-1),
        timestamp(QDateTime::currentDateTime()),
        persistent(false),
        autoQueue(true) {}
};

Q_DECLARE_METATYPE(NotificationData)

// ───────────────────────────────────────────────────────────────
// Modern Notification Manager - Singleton Pattern
// ───────────────────────────────────────────────────────────────
class NotificationManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int maxVisibleNotifications READ maxVisibleNotifications WRITE setMaxVisibleNotifications NOTIFY maxVisibleNotificationsChanged)
    Q_PROPERTY(bool globalMute READ globalMute WRITE setGlobalMute NOTIFY globalMuteChanged)
    Q_PROPERTY(int totalNotifications READ totalNotifications NOTIFY totalNotificationsChanged)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)

public:
    static NotificationManager& instance() {
        static NotificationManager instance;
        return instance;
    }

    // Core notification methods
    Q_INVOKABLE QString showNotification(const QString &title, 
                                        const QString &message,
                                        int level = 0,  // Info
                                        int duration = 5000,
                                        const QString &category = "general");

    Q_INVOKABLE QString showProgress(const QString &title,
                                    const QString &message,
                                    int progress = 0,
                                    const QString &category = "progress");

    Q_INVOKABLE void updateProgress(const QString &id, int progress, const QString &message = "");
    
    Q_INVOKABLE void dismissNotification(const QString &id);
    Q_INVOKABLE void dismissAll();
    Q_INVOKABLE void dismissCategory(const QString &category);
    
    // Convenience methods for different levels
    Q_INVOKABLE QString info(const QString &title, const QString &message, const QString &category = "info");
    Q_INVOKABLE QString success(const QString &title, const QString &message, const QString &category = "success");
    Q_INVOKABLE QString warning(const QString &title, const QString &message, const QString &category = "warning");
    Q_INVOKABLE QString error(const QString &title, const QString &message, const QString &category = "error");
    
    // Task tracking methods
    Q_INVOKABLE QString startTask(const QString &taskName, const QString &description = "");
    Q_INVOKABLE void updateTask(const QString &taskId, int progress, const QString &status = "");
    Q_INVOKABLE void completeTask(const QString &taskId, const QString &result = "");
    Q_INVOKABLE void failTask(const QString &taskId, const QString &error = "");

    // Settings
    Q_INVOKABLE void setMaxVisibleNotifications(int max);
    Q_INVOKABLE int maxVisibleNotifications() const { return m_maxVisible; }
    
    Q_INVOKABLE void setGlobalMute(bool mute);
    Q_INVOKABLE bool globalMute() const { return m_globalMute; }
    
    Q_INVOKABLE int totalNotifications() const { return m_totalCount; }
    Q_INVOKABLE int unreadCount() const { return m_unreadCount; }

    // History and filtering
    Q_INVOKABLE QJsonArray getHistory(int limit = 50) const;
    Q_INVOKABLE QJsonArray getByCategory(const QString &category) const;
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void markAllAsRead();

signals:
    // Core notification signals
    void notificationAdded(QString id, QString title, QString message, 
                          int level, int duration, QString category, 
                          int progress, QString actionText, QString actionId);
    void notificationUpdated(QString id, QString message, int progress);
    void notificationDismissed(QString id);
    void notificationAction(QString id, QString actionId);
    
    // Queue and management signals
    void queueChanged();
    void maxVisibleNotificationsChanged();
    void globalMuteChanged();
    void totalNotificationsChanged();
    void unreadCountChanged();
    
    // Batch operations
    void allNotificationsDismissed();
    void categoryDismissed(QString category);

public slots:
    void handleNotificationAction(const QString &id, const QString &actionId);
    void handleNotificationClick(const QString &id);

private slots:
    void processQueue();
    void handleAutoDissmiss();

private:
    explicit NotificationManager(QObject *parent = nullptr);
    ~NotificationManager() = default;
    
    // Disable copy constructor and assignment
    NotificationManager(const NotificationManager&) = delete;
    NotificationManager& operator=(const NotificationManager&) = delete;

    QString generateId() const;
    void addToHistory(const NotificationData &data);
    void cleanupOldNotifications();
    bool shouldQueue() const;
    void processNextInQueue();

    QQueue<NotificationData> m_queue;
    QList<NotificationData> m_activeNotifications;
    QList<NotificationData> m_history;
    QTimer *m_queueTimer;
    QTimer *m_cleanupTimer;
    
    int m_maxVisible = 5;
    int m_maxHistory = 200;
    bool m_globalMute = false;
    int m_totalCount = 0;
    int m_unreadCount = 0;
    
    // Task tracking
    QHash<QString, NotificationData> m_activeTasks;
};

// ───────────────────────────────────────────────────────────────
// Convenience Macros for Easy Integration
// ───────────────────────────────────────────────────────────────
#define NOTIFY_INFO(title, message) \
    NotificationManager::instance().info(title, message)

#define NOTIFY_SUCCESS(title, message) \
    NotificationManager::instance().success(title, message)

#define NOTIFY_WARNING(title, message) \
    NotificationManager::instance().warning(title, message)

#define NOTIFY_ERROR(title, message) \
    NotificationManager::instance().error(title, message)

#define NOTIFY_PROGRESS(title, message, progress) \
    NotificationManager::instance().showProgress(title, message, progress)

#define START_TASK(name, desc) \
    NotificationManager::instance().startTask(name, desc)

#define UPDATE_TASK(id, progress, status) \
    NotificationManager::instance().updateTask(id, progress, status)

#define COMPLETE_TASK(id, result) \
    NotificationManager::instance().completeTask(id, result)

#define FAIL_TASK(id, error) \
    NotificationManager::instance().failTask(id, error)

#endif // NOTIFICATIONMANAGER_H