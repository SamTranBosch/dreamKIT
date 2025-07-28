#pragma once

#include "../core/interfaces.h"
#include "../core/applicationmanager.h"

namespace AppManager {

class AppManagerFactory {
public:
    static std::unique_ptr<ApplicationManager> createManager(
        const QString& platform = "auto",
        const QString& configPath = ""
    );

    static std::unique_ptr<IAppRepository> createRepository(
        const QString& type,
        const QVariantMap& config
    );

    static std::unique_ptr<IAppInstaller> createInstaller(
        const QString& type,
        IConfigurationProvider* config
    );

    static std::unique_ptr<IAppRuntime> createRuntime(
        const QString& type,
        IConfigurationProvider* config
    );

    static std::unique_ptr<IConfigurationProvider> createConfigProvider(
        const QString& type,
        const QString& configPath
    );
};

}
