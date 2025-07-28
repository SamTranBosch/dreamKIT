#pragma once

#include "../core/interfaces.h"

namespace AppManager {

class K8sDockerRuntime : public IAppRuntime {
    Q_OBJECT

public:
    explicit K8sDockerRuntime(IConfigurationProvider* config, QObject* parent = nullptr);

    QFuture<bool> startApp(const QString& appId) override;
    QFuture<bool> stopApp(const QString& appId) override;
    QFuture<AppStatus> getAppStatus(const QString& appId) override;
    QFuture<QList<QString>> getRunningApps() override;

    QString runtimeType() const override { return "kubernetes-docker"; }
    bool supportsApp(const AppMetadata& app) const override;

private:
    IConfigurationProvider* m_config;
};

}
