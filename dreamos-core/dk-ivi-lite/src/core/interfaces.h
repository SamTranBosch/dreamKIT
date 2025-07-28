#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QVariant>
#include <QList>
#include <QFuture>
#include <memory>
#include <QDateTime>

namespace AppManager {

struct AppMetadata {
    QString id;
    QString name;
    QString author;
    QString version;
    QString description;
    QString iconUrl;
    QString category;
    double rating = 0.0;
    int downloads = 0;
    QJsonObject extendedProperties; // For platform-specific data
    
    static AppMetadata fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

struct AppInstallInfo {
    QString appId;
    QString installPath;
    QDateTime installedAt;
    QString installerVersion;
    QJsonObject installConfig;
    bool isActive = false;
    
    static AppInstallInfo fromJson(const QJsonObject& json);
    QJsonObject toJson() const;
};

enum class AppStatus {
    Unknown,
    Available,
    Installing,
    Installed,
    Running,
    Stopped,
    Error,
    Uninstalling
};

//------------------------------------------------------------------------------
// Repository Interface - Abstract app source
//------------------------------------------------------------------------------

class IAppRepository : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~IAppRepository() = default;
    
    virtual QFuture<QList<AppMetadata>> searchApps(
        const QString& query = "",
        const QString& category = "",
        int page = 1,
        int limit = 20
    ) = 0;
    
    virtual QFuture<AppMetadata> getAppDetails(const QString& appId) = 0;
    virtual QFuture<QByteArray> downloadAppPackage(const QString& appId) = 0;
    
    virtual QString name() const = 0;
    virtual bool requiresAuthentication() const = 0;
    virtual QFuture<bool> authenticate(const QString& username, const QString& password) = 0;

signals:
    void searchCompleted(const QList<AppMetadata>& apps);
    void downloadProgress(const QString& appId, int percentage);
    void error(const QString& message);
};

//------------------------------------------------------------------------------
// Installer Interface - Abstract installation mechanism
//------------------------------------------------------------------------------

class IAppInstaller : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~IAppInstaller() = default;
    
    virtual QFuture<bool> installApp(const AppMetadata& app, const QByteArray& packageData) = 0;
    virtual QFuture<bool> uninstallApp(const QString& appId) = 0;
    virtual QFuture<QList<AppInstallInfo>> getInstalledApps() = 0;
    virtual QFuture<bool> updateApp(const QString& appId, const QByteArray& packageData) = 0;
    
    virtual QString installerType() const = 0;
    virtual QStringList supportedPlatforms() const = 0;

signals:
    void installStarted(const QString& appId);
    void installProgress(const QString& appId, int percentage);
    void installCompleted(const QString& appId, bool success);
    void uninstallCompleted(const QString& appId, bool success);
    void error(const QString& appId, const QString& message);
};

//------------------------------------------------------------------------------
// Runtime Interface - Abstract app execution environment
//------------------------------------------------------------------------------

class IAppRuntime : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~IAppRuntime() = default;
    
    virtual QFuture<bool> startApp(const QString& appId) = 0;
    virtual QFuture<bool> stopApp(const QString& appId) = 0;
    virtual QFuture<AppStatus> getAppStatus(const QString& appId) = 0;
    virtual QFuture<QList<QString>> getRunningApps() = 0;
    
    virtual QString runtimeType() const = 0;
    virtual bool supportsApp(const AppMetadata& app) const = 0;

signals:
    void appStarted(const QString& appId);
    void appStopped(const QString& appId);
    void appStatusChanged(const QString& appId, AppStatus status);
    void error(const QString& appId, const QString& message);
};

//------------------------------------------------------------------------------
// Configuration Interface - Abstract configuration management
//------------------------------------------------------------------------------

class IConfigurationProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual ~IConfigurationProvider() = default;
    
    virtual QVariant getValue(const QString& key, const QVariant& defaultValue = QVariant()) const = 0;
    virtual void setValue(const QString& key, const QVariant& value) = 0;
    virtual QStringList getCategories() const = 0;
    virtual QString getInstallPath() const = 0;
    virtual QString getCachePath() const = 0;
    
signals:
    void configurationChanged(const QString& key, const QVariant& value);
};

//------------------------------------------------------------------------------
// Plugin System
//------------------------------------------------------------------------------

class IAppManagerPlugin {
public:
    virtual ~IAppManagerPlugin() = default;
    virtual QString pluginName() const = 0;
    virtual QString pluginVersion() const = 0;
    virtual QStringList supportedTypes() const = 0;
    
    virtual std::unique_ptr<IAppRepository> createRepository(const QVariantMap& config) { return nullptr; }
    virtual std::unique_ptr<IAppInstaller> createInstaller(IConfigurationProvider* config) { return nullptr; }
    virtual std::unique_ptr<IAppRuntime> createRuntime(IConfigurationProvider* config) { return nullptr; }
};

}

#define AppManagerPlugin_IID "com.appmanager.plugin/1.0"
Q_DECLARE_INTERFACE(AppManager::IAppManagerPlugin, AppManagerPlugin_IID)