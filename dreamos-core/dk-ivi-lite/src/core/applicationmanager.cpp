#include "applicationmanager.h"
#include "interfaces.h"
#include <QtConcurrent>

namespace AppManager {

ApplicationManager::ApplicationManager(QObject* parent)
    : QObject(parent)
    , m_availableApps(new AppListModel(this))
    , m_installedApps(new AppListModel(this))
{
}

void ApplicationManager::setRepository(std::unique_ptr<IAppRepository> repository) {
    if (m_repository) {
        disconnect(m_repository.get(), nullptr, this, nullptr);
    }
    
    m_repository = std::move(repository);
    
    if (m_repository) {
        connect(m_repository.get(), &IAppRepository::searchCompleted,
                this, &ApplicationManager::onSearchCompleted);
        connect(m_repository.get(), &IAppRepository::error,
                this, &ApplicationManager::setError);
    }
}

void ApplicationManager::setInstaller(std::unique_ptr<IAppInstaller> installer) {
    if (m_installer) {
        disconnect(m_installer.get(), nullptr, this, nullptr);
    }
    
    m_installer = std::move(installer);
    
    if (m_installer) {
        connect(m_installer.get(), &IAppInstaller::installCompleted,
                this, &ApplicationManager::onInstallCompleted);
        connect(m_installer.get(), &IAppInstaller::uninstallCompleted,
                this, [this](const QString& appId, bool success) {
                    emit appUninstalled(appId, success);
                    refreshInstalledApps();
                });
        connect(m_installer.get(), &IAppInstaller::error,
                this, &ApplicationManager::setError);
    }
}

void ApplicationManager::setRuntime(std::unique_ptr<IAppRuntime> runtime) {
    if (m_runtime) {
        disconnect(m_runtime.get(), nullptr, this, nullptr);
    }
    
    m_runtime = std::move(runtime);
    
    if (m_runtime) {
        connect(m_runtime.get(), &IAppRuntime::appStatusChanged,
                this, &ApplicationManager::onAppStatusChanged);
        connect(m_runtime.get(), &IAppRuntime::error,
                this, &ApplicationManager::setError);
    }
}

void ApplicationManager::setConfigurationProvider(std::unique_ptr<IConfigurationProvider> config) {
    m_config = std::move(config);
}

void ApplicationManager::searchApps(const QString& query, const QString& category) {
    if (!m_repository) {
        setError("No repository configured");
        return;
    }
    
    setLoading(true);
    setError("");
    
    auto future = m_repository->searchApps(query, category);
    // Future handling would typically use QtConcurrent or custom async handling
}

void ApplicationManager::refreshInstalledApps() {
    if (!m_installer) {
        setError("No installer configured");
        return;
    }
    
    auto future = m_installer->getInstalledApps();
    // Handle future result to update installed apps model
}

void ApplicationManager::installApp(const QString& appId) {
    if (!m_repository || !m_installer) {
        setError("Repository or installer not configured");
        return;
    }
    
    // Find app in available apps
    int index = m_availableApps->findAppIndex(appId);
    if (index < 0) {
        setError("App not found in available apps");
        return;
    }
    
    auto appData = m_availableApps->get(index);
    AppMetadata metadata;
    metadata.id = appData.value("id").toString();
    metadata.name = appData.value("name").toString();
    // ... populate other fields
    
    setLoading(true);
    
    // Download and install
    auto downloadFuture = m_repository->downloadAppPackage(appId);
    // Chain with install operation
}

void ApplicationManager::uninstallApp(const QString& appId) {
    if (!m_installer) {
        setError("No installer configured");
        return;
    }
    
    setLoading(true);
    auto future = m_installer->uninstallApp(appId);
}

void ApplicationManager::startApp(const QString& appId) {
    if (!m_runtime) {
        setError("No runtime configured");
        return;
    }
    
    auto future = m_runtime->startApp(appId);
}

void ApplicationManager::stopApp(const QString& appId) {
    if (!m_runtime) {
        setError("No runtime configured");
        return;
    }
    
    auto future = m_runtime->stopApp(appId);
}

void ApplicationManager::onSearchCompleted(const QList<AppMetadata>& apps) {
    m_availableApps->updateApps(apps);
    setLoading(false);
}

void ApplicationManager::onInstallCompleted(const QString& appId, bool success) {
    emit appInstalled(appId, success);
    setLoading(false);
    
    if (success) {
        refreshInstalledApps();
    }
}

void ApplicationManager::onAppStatusChanged(const QString& appId, AppStatus status) {
    m_availableApps->updateAppStatus(appId, status);
    m_installedApps->updateAppStatus(appId, status);
}

void ApplicationManager::setLoading(bool loading) {
    if (m_isLoading != loading) {
        m_isLoading = loading;
        emit isLoadingChanged(loading);
    }
}

void ApplicationManager::setError(const QString& error) {
    if (m_currentError != error) {
        m_currentError = error;
        emit currentErrorChanged(error);
    }
}

}