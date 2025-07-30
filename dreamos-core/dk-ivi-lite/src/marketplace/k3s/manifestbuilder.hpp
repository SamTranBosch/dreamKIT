#pragma once
// k3s/manifestbuilder.hpp
//
// Emits dashboard JSON + deployment / pull / mirror job YAML files.
//
#include "../core/datamanager.hpp"
#include <QString>

namespace K3s {

struct ManifestInfo
{
    QString dir;               // <root>/<appId>
    QString dashboardJson;
    QString deploymentYaml;
    QString pullJobYaml;
    QString mirrorJobYaml;
    bool    isRemoteNode = false;
};

class ManifestBuilder
{
public:
    // rootDir == “…/dk_marketplace”
    static ManifestInfo write(const AppInfo &app);
};

} // namespace K3s
