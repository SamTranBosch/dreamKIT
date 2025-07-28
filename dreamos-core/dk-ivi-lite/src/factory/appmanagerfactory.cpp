#include "appmanagerfactory.h"
#include "../core/applicationmanager.h"
#include "../implementations/httpmarketplacerepository.h"
#include "../implementations/jsonconfigurationprovider.h"
#include "../implementations/k8sdockerinstaller.h"
#include "../implementations/k8sdockerruntime.h"
#include <QStandardPaths>

namespace AppManager {

std::unique_ptr<ApplicationManager> AppManagerFactory::createManager(
    const QString& platform, const QString& configPath) {

    auto manager = std::make_unique<ApplicationManager>();

    // Create configuration provider
    auto config = createConfigProvider("json", configPath.isEmpty() ? 
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/appmanager.json" : configPath);

    // Create components based on platform
    QString actualPlatform = platform;
    if (actualPlatform == "auto") {
        // Auto-detect platform
        #ifdef Q_OS_LINUX
        actualPlatform = "kubernetes";
        #else
        actualPlatform = "local";
        #endif
    }

    // Create repository
    QVariantMap repoConfig;
    repoConfig["url"] = config->getValue("repositories.0.url", "https://store-be.digitalauto.tech");
    repoConfig["loginUrl"] = config->getValue("repositories.0.loginUrl", "");
    auto repository = createRepository("http", repoConfig);

    // Create installer and runtime
    auto installer = createInstaller(actualPlatform, config.get());
    auto runtime = createRuntime(actualPlatform, config.get());

    // Set components
    manager->setRepository(std::move(repository));
    manager->setInstaller(std::move(installer));
    manager->setRuntime(std::move(runtime));
    manager->setConfigurationProvider(std::move(config));

    return manager;
}

std::unique_ptr<IAppRepository> AppManagerFactory::createRepository(
    const QString& type, const QVariantMap& config) {

    if (type == "http") {
        QString url = config.value("url").toString();
        auto repo = std::make_unique<HttpMarketplaceRepository>(url);
        return std::move(repo);
    }

    return nullptr;
}

std::unique_ptr<IAppInstaller> AppManagerFactory::createInstaller(
    const QString& type, IConfigurationProvider* config) {

    if (type == "kubernetes" || type == "k8s") {
        return std::make_unique<K8sDockerInstaller>(config);
    }

    return nullptr;
}

std::unique_ptr<IAppRuntime> AppManagerFactory::createRuntime(
    const QString& type, IConfigurationProvider* config) {

    if (type == "kubernetes" || type == "k8s") {
        return std::make_unique<K8sDockerRuntime>(config);
    }

    return nullptr;
}

std::unique_ptr<IConfigurationProvider> AppManagerFactory::createConfigProvider(
    const QString& type, const QString& configPath) {

    if (type == "json") {
        return std::make_unique<JsonConfigurationProvider>(configPath);
    }

    return nullptr;
}

}
