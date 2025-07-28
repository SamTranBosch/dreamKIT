#pragma once

#include <QObject>
#include <memory>
#include "interfaces.h"
#include "../models/applistmodel.h"

namespace AppManager {

class ApplicationManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(AppListModel* availableApps READ availableAppsModel CONSTANT)
    Q_PROPERTY(AppListModel* installedApps READ installedAppsModel CONSTANT)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)
    Q_PROPERTY(QString currentError READ currentError NOTIFY currentErrorChanged)

public:
    explicit ApplicationManager(QObject* parent = nullptr);
    virtual ~ApplicationManager() = default;
    
    // Component registration
    void setRepository(std::unique_ptr<IAppRepository> repository);
    void setInstaller(std::unique_ptr<IAppInstaller> installer);
    void setRuntime(std::unique_ptr<IAppRuntime> runtime);
    void setConfigurationProvider(std::unique_ptr<IConfigurationProvider> config);
    
    // Properties
    AppListModel* availableAppsModel() const { return m_availableApps; }
    AppListModel* installedAppsModel() const { return m_installedApps; }
    bool isLoading() const { return m_isLoading; }
    QString currentError() const { return m_currentError; }

public slots:
    void searchApps(const QString& query = "", const QString& category = "");
    void refreshInstalledApps();
    void installApp(const QString& appId);
    void uninstallApp(const QString& appId);
    void startApp(const QString& appId);
    void stopApp(const QString& appId);

signals:
    void isLoadingChanged(bool isLoading);
    void currentErrorChanged(const QString& error);
    void appInstalled(const QString& appId, bool success);
    void appUninstalled(const QString& appId, bool success);
    void appStarted(const QString& appId, bool success);
    void appStopped(const QString& appId, bool success);

private slots:
    void onSearchCompleted(const QList<AppMetadata>& apps);
    void onInstallCompleted(const QString& appId, bool success);
    void onAppStatusChanged(const QString& appId, AppStatus status);

private:
    void setLoading(bool loading);
    void setError(const QString& error);
    
    std::unique_ptr<IAppRepository> m_repository;
    std::unique_ptr<IAppInstaller> m_installer;
    std::unique_ptr<IAppRuntime> m_runtime;
    std::unique_ptr<IConfigurationProvider> m_config;
    
    AppListModel* m_availableApps;
    AppListModel* m_installedApps;
    
    bool m_isLoading = false;
    QString m_currentError;
};

}