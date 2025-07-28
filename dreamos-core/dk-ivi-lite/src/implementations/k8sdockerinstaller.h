#pragma once

#include "../core/interfaces.h"

namespace AppManager {

class K8sDockerInstaller : public IAppInstaller {
    Q_OBJECT

public:
    explicit K8sDockerInstaller(IConfigurationProvider* config, QObject* parent = nullptr);

    QFuture<bool> installApp(const AppMetadata& app, const QByteArray& packageData) override;
    QFuture<bool> uninstallApp(const QString& appId) override;
    QFuture<QList<AppInstallInfo>> getInstalledApps() override;
    QFuture<bool> updateApp(const QString& appId, const QByteArray& packageData) override;

    QString installerType() const override { return "kubernetes-docker"; }
    QStringList supportedPlatforms() const override { return {"linux", "vehicle"}; }

private:
    struct InstallTask {
        QString appId;
        QStringList commands;
        int currentStep = 0;
    };

    void executeNextStep(InstallTask& task);
    QString generateDeploymentYaml(const AppMetadata& app) const;
    QString generatePullJobYaml(const AppMetadata& app) const;

    IConfigurationProvider* m_config;
    QHash<QString, InstallTask> m_activeTasks;
};

}
