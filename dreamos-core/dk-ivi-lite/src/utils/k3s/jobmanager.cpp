#include "jobmanager.hpp"
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QMetaObject>
#include <QMutexLocker>
#include "../notifications/notificationmanager.hpp"

using namespace K3s;

// Static members
QMutex JobManager::s_instanceMutex;
JobManager* JobManager::s_instance = nullptr;

JobManager::JobManager(QObject *parent)
    : QObject(parent)
    , m_installer(new Installer(this))
    , m_mainThread(QThread::currentThread())
{
    connect(m_installer, &Installer::finished,
            this, &JobManager::onInstallerFinished);
}

JobManager::~JobManager()
{
    QMutexLocker locker(&s_instanceMutex);
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

JobManager* JobManager::instance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        // Create instance in main thread if possible
        if (QThread::currentThread() == qApp->thread()) {
            s_instance = new JobManager(qApp);
        } else {
            // If called from worker thread, create with no parent and move to main thread
            s_instance = new JobManager(nullptr);
            s_instance->moveToThread(qApp->thread());
        }
    }
    return s_instance;
}

template<typename T>
Async::Job<T>* JobManager::createJobSafely(std::function<T()> task)
{
    QMutexLocker locker(&m_mutex);
    
    try {
        // Always create jobs in the main thread to avoid parenting issues
        Async::Job<T>* job = nullptr;
        
        if (QThread::currentThread() == m_mainThread) {
            // We're in main thread - create with this as parent
            job = new Async::Job<T>(task, this);
        } else {
            // We're in worker thread - create in main thread via invokeMethod
            QMetaObject::invokeMethod(this, [&]() {
                job = new Async::Job<T>(task, this);
            }, Qt::BlockingQueuedConnection);
        }
        
        return job;
        
    } catch (const std::bad_alloc &e) {
        qCritical() << "[JobManager] Memory allocation failed in createJobSafely:" << e.what();
        return nullptr;
    } catch (const std::exception &e) {
        qCritical() << "[JobManager] Exception in createJobSafely:" << e.what();
        return nullptr;
    }
}

Async::Chain* JobManager::createChainSafely()
{
    QMutexLocker locker(&m_mutex);
    
    Async::Chain* chain = nullptr;
    
    if (QThread::currentThread() == m_mainThread) {
        chain = new Async::Chain(this);
    } else {
        // Create in main thread via invokeMethod
        QMetaObject::invokeMethod(this, [&]() {
            chain = new Async::Chain(this);
        }, Qt::BlockingQueuedConnection);
    }
    
    return chain;
}

// Enhanced deployment method to ensure fresh images
Async::Job<JobManager::JobResult>* JobManager::deployService(const DeploymentInfo &info)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Deploy %1").arg(info.name));
        
        try {
            // Pre-deployment checks and cleanup
            if (info.subscribe) {
                // Check node ready
                bool nodeReady = false;
                try { 
                    nodeReady = Installer::nodeReady("vip", 3); 
                } catch(...) {}
                
                if (!nodeReady) {
                    result.errorMessage = "Worker node not ready. Deployment may fail.";
                    NOTIFY_WARNING("Deployment", result.errorMessage);
                }
                
                // Force cleanup existing deployment for fresh start
                qDebug() << "[JobManager] Forcing cleanup of existing deployment:" << info.id;
                QString cleanupCmd = QString("kubectl delete deployment %1 -n default --ignore-not-found --wait=true").arg(info.id);
                executeCommandsSync({cleanupCmd}); // Don't fail if cleanup fails
                
                // Wait for cleanup to complete
                QThread::msleep(2000);
                
                // Clear any cached images to force fresh pull
                QString imageClearCmd = QString("docker rmi $(docker images --format '{{.Repository}}:{{.Tag}}' | grep %1 | head -3) 2>/dev/null || true").arg(info.id);
                executeCommandsSync({imageClearCmd}); // Don't fail if image removal fails
            }
            
            // Execute deployment command
            const QString cmd = info.subscribe 
                ? QString("kubectl apply -f %1").arg(info.deploymentYaml)
                : QString("kubectl delete -f %1 --ignore-not-found").arg(info.deploymentYaml);
            
            result = executeCommandsSync({cmd});
            
            // Post-deployment verification for subscriptions
            if (result.success && info.subscribe) {
                // Wait for deployment to be ready with timeout
                QString waitCmd = QString("kubectl rollout status deployment/%1 --timeout=300s").arg(info.id);
                JobResult waitResult = executeCommandsSync({waitCmd});
                
                if (!waitResult.success) {
                    result.errorMessage = QString("Deployment applied but not ready: %1").arg(waitResult.errorMessage);
                    qWarning() << "[JobManager] Deployment not ready:" << result.errorMessage;
                } else {
                    // Get the actual image being used for verification
                    QString imageCmd = QString("kubectl get deployment %1 -o jsonpath='{.spec.template.spec.containers[0].image}'").arg(info.id);
                    JobResult imageResult = executeCommandsSync({imageCmd});
                    
                    if (imageResult.success) {
                        qDebug() << "[JobManager] Deployment" << info.id << "running image:" << imageResult.output.trimmed();
                    }
                }
            }
            
            const QString action = info.subscribe ? "deployed" : "stopped";
            const QString message = QString("Service '%1' %2").arg(info.name, action);
            
            emit jobFinished(QString("Deploy %1").arg(info.name), result.success, message);
            
            if (result.success) {
                NOTIFY_SUCCESS("Deployment", message);
            } else {
                NOTIFY_WARNING("Deployment", QString("Failed to %1 %2: %3")
                    .arg(action, info.name, result.errorMessage));
            }
            
        } catch (const std::exception &e) {
            result.success = false;
            result.errorMessage = QString("Exception: %1").arg(e.what());
            emit jobFinished(QString("Deploy %1").arg(info.name), false, result.errorMessage);
        }
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::undeployService(const DeploymentInfo &info)
{
    DeploymentInfo undeployInfo = info;
    undeployInfo.subscribe = false;
    return deployService(undeployInfo);
}

Async::Job<JobManager::JobResult>* JobManager::removeService(const QString &id, const QString &deploymentYaml)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        result.success = true;
        
        emit jobStarted(QString("Remove %1").arg(id));
        
        try {
            QStringList cleanupCommands;
            
            // Step 1: Scale deployment to 0 first (graceful shutdown)
            cleanupCommands << QString("kubectl scale deployment %1 --replicas=0 -n default --ignore-not-found").arg(id);
            
            // Step 2: Wait for pods to terminate gracefully
            cleanupCommands << QString("kubectl wait --for=delete pod -l app=%1 -n default --timeout=30s || true").arg(id);
            
            // Step 3: Force delete any remaining pods
            cleanupCommands << QString("kubectl delete pods -l app=%1 -n default --force --grace-period=0 --ignore-not-found").arg(id);
            
            // Step 4: Delete the deployment
            cleanupCommands << QString("kubectl delete -f %1 --ignore-not-found --wait=true").arg(deploymentYaml);
            
            // Step 5: Delete any related jobs (pull, mirror)
            cleanupCommands << QString("kubectl delete job pull-%1 --ignore-not-found").arg(id);
            cleanupCommands << QString("kubectl delete job mirror-%1 --ignore-not-found").arg(id);
            
            // Step 6: Clean up any configmaps or secrets (if they follow naming convention)
            cleanupCommands << QString("kubectl delete configmap %1-config --ignore-not-found").arg(id);
            cleanupCommands << QString("kubectl delete secret %1-secret --ignore-not-found").arg(id);
            
            // Step 7: Remove cached Docker images (both original and mirrored)
            cleanupCommands << QString("docker rmi $(docker images --format '{{.Repository}}:{{.Tag}}' | grep %1 | head -5) 2>/dev/null || true").arg(id);
            
            // Execute all cleanup commands
            for (const QString &cmd : cleanupCommands) {
                qDebug() << "[JobManager] Executing cleanup:" << cmd;
                JobResult cmdResult = executeCommandsSync({cmd});
                
                if (!cmdResult.success && !cmd.contains("--ignore-not-found") && !cmd.contains("|| true")) {
                    qWarning() << "[JobManager] Cleanup command failed:" << cmd 
                               << "Error:" << cmdResult.errorMessage;
                    // Don't fail the entire operation for individual cleanup failures
                }
            }
            
            // Step 8: Final verification - ensure no resources remain
            QString verifyCmd = QString("kubectl get all -l app=%1 -n default --no-headers 2>/dev/null | wc -l").arg(id);
            JobResult verifyResult = executeCommandsSync({verifyCmd});
            
            if (verifyResult.success) {
                int remainingResources = verifyResult.output.trimmed().toInt();
                if (remainingResources > 0) {
                    qWarning() << "[JobManager]" << remainingResources << "resources still remain for" << id;
                    result.errorMessage = QString("Some resources may still remain (%1). Wait for service Stop first").arg(remainingResources);
                    NOTIFY_WARNING("Removal", result.errorMessage);
                } else {
                    QString message = QString("All resources successfully removed for %1").arg(id);
                    qDebug() << message;
                    NOTIFY_SUCCESS("Removal", message);
                }
            }
            
            emit jobFinished(QString("Remove %1").arg(id), result.success, 
                result.success ? QString("%1 completely removed").arg(id) : result.errorMessage);
            
        } catch (const std::exception &e) {
            result.success = false;
            result.errorMessage = QString("Exception during removal: %1").arg(e.what());
            emit jobFinished(QString("Remove %1").arg(id), false, result.errorMessage);
        }
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::scaleDeployment(const QString &deploymentName, int replicas)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Scale %1 to %2 replicas").arg(deploymentName).arg(replicas));
        
        const QString cmd = QString("kubectl scale deployment %1 --replicas=%2 -n default")
            .arg(deploymentName).arg(replicas);
        
        result = executeCommandsSync({cmd});
        
        const QString message = result.success 
            ? QString("Scaled %1 to %2 replicas").arg(deploymentName).arg(replicas)
            : QString("Failed to scale %1").arg(deploymentName);
            
        emit jobFinished(QString("Scale %1").arg(deploymentName), result.success, message);
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::restartDeployment(const QString &deploymentName)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Restart %1").arg(deploymentName));
        
        const QString cmd = QString("kubectl rollout restart deployment/%1 -n default")
            .arg(deploymentName);
        
        result = executeCommandsSync({cmd});
        
        const QString message = result.success 
            ? QString("Restart initiated for %1").arg(deploymentName)
            : QString("Failed to restart %1").arg(deploymentName);
            
        emit jobFinished(QString("Restart %1").arg(deploymentName), result.success, message);
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::runInstallationCommands(const QStringList &commands)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted("Installation Commands");
        
        // Execute commands synchronously in GUI thread
        bool ok = false;
        QMetaObject::invokeMethod(this, [&]() {
            QEventLoop loop;
            connect(m_installer, &Installer::finished, &loop,
                   [&](bool success) { ok = success; loop.quit(); });
            m_installer->queueAndRun(commands);
            loop.exec();
        }, Qt::BlockingQueuedConnection);
        
        result.success = ok;
        if (!ok) {
            result.errorMessage = "Installation commands failed";
        }
        
        emit jobFinished("Installation Commands", result.success, 
            result.success ? "Installation completed" : result.errorMessage);
        
        return result;
    });
}

Async::Job<JobManager::JobResult>* JobManager::cleanupInstallationJobs(const QString &appId)
{
    return createJobSafely<JobResult>([=]() -> JobResult {
        JobResult result;
        
        emit jobStarted(QString("Cleanup %1").arg(appId));
        
        QStringList cleanupCmds;
        cleanupCmds << QString("kubectl delete job mirror-%1 --ignore-not-found").arg(appId)
                   << QString("kubectl delete job pull-%1 --ignore-not-found").arg(appId);
        
        // Execute in GUI thread
        QMetaObject::invokeMethod(this, [&]() {
            QEventLoop loop;
            connect(m_installer, &Installer::finished, &loop,
                   [&](bool) { loop.quit(); }); // Don't care about result for cleanup
            m_installer->queueAndRun(cleanupCmds);
            loop.exec();
        }, Qt::BlockingQueuedConnection);
        
        result.success = true; // Cleanup always "succeeds"
        
        emit jobFinished(QString("Cleanup %1").arg(appId), true, 
            QString("Cleanup completed for %1").arg(appId));
        
        return result;
    });
}

Async::Job<bool>* JobManager::checkNodeReady(const QString &nodeName, int timeoutSec)
{
    return createJobSafely<bool>([=]() -> bool {
        try {
            // Simple command that works reliably
            const QString cmd = QString("kubectl get node %1 --no-headers 2>/dev/null").arg(nodeName);
            
            JobResult result = executeCommandsSync({cmd});
            
            if (!result.success) {
                qDebug() << "[JobManager] Node" << nodeName << "not found or kubectl failed";
                return false;
            }
            
            // Check if output contains "Ready" status
            bool ready = result.output.contains("Ready") && !result.output.contains("NotReady");
            
            qDebug() << "[JobManager] Node" << nodeName << "ready check:"
                     << ". Success:" << result.success 
                     << ". Output:" << result.output.trimmed()
                     << ". Ready:" << ready;
            
            return ready;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception in node ready check:" << e.what();
            return false;
        } catch (...) {
            qCritical() << "[JobManager] Unknown exception in node ready check";
            return false;
        }
    });
}

Async::Job<bool>* JobManager::checkDeploymentAvailable(const QString &deploymentId, int timeoutSec)
{
    return createJobSafely<bool>([=]() -> bool {
        bool available = false;
        try {
            available = Installer::deploymentAvailable(deploymentId, timeoutSec);
        } catch(...) {}
        return available;
    });
}

Async::Chain* JobManager::createAutoRestartChain(const QString &deploymentName)
{
    auto *chain = createChainSafely();
    
    // Step 1: Check if deployment exists - simplified
    auto deploymentExists = std::make_shared<bool>(false);
    
    chain->add([=]() -> bool {
        *deploymentExists = this->deploymentExists(deploymentName);
        return true; // Always continue
    });
    
    // Step 2: Scale down to 0
    chain->add([=]() -> bool {
        if (!*deploymentExists) {
            qDebug() << "[JobManager] Skipping scale down - deployment doesn't exist";
            return true;
        }
        
        qDebug() << "[JobManager] Scaling down deployment to 0 replicas";
        
        try {
            const QString cmd = QString("kubectl scale deployment %1 --replicas=0 -n default")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            if (result.success) {
                qDebug() << "[JobManager] Scale down successful";
                emit jobFinished(QString("Scale %1").arg(deploymentName), true, 
                    QString("Scaled %1 to 0 replicas").arg(deploymentName));
            } else {
                qWarning() << "[JobManager] Scale down failed:" << result.errorMessage;
                emit jobFinished(QString("Scale %1").arg(deploymentName), false, result.errorMessage);
            }
            
            return result.success;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Scale down exception:" << e.what();
            return false;
        }
    });
    
    // Step 3: Wait for deployment to scale down
    chain->add([=]() -> bool {
        if (!*deploymentExists) return true;
        
        qDebug() << "[JobManager] Waiting for deployment to scale down";
        bool scaled = waitForPodTermination(deploymentName, 30); // 30 second timeout
        
        if (!scaled) {
            qDebug() << "[JobManager] Deployment didn't scale down within timeout, force deleting pods...";
            forceDeletePods(deploymentName);
            QThread::sleep(5); // Wait after force delete
        }
        
        return true; // Always continue
    });
    
    // Step 4: Wait for clean state
    chain->add([=]() -> bool {
        if (!*deploymentExists) return true;
        
        qDebug() << "[JobManager] Waiting for clean state (3 seconds)";
        QThread::sleep(5);
        return true;
    });
    
    // Step 5: Scale back up to 1
    chain->add([=]() -> bool {
        if (!*deploymentExists) {
            qDebug() << "[JobManager] Skipping scale up - deployment doesn't exist";
            return true;
        }
        
        qDebug() << "[JobManager] Scaling up deployment to 1 replica";
        
        try {
            const QString cmd = QString("kubectl scale deployment %1 --replicas=1 -n default")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            if (result.success) {
                qDebug() << "[JobManager] Scale up successful";
                emit jobFinished(QString("Scale %1").arg(deploymentName), true, 
                    QString("Scaled %1 to 1 replica").arg(deploymentName));
            } else {
                qWarning() << "[JobManager] Scale up failed:" << result.errorMessage;
                emit jobFinished(QString("Scale %1").arg(deploymentName), false, result.errorMessage);
            }
            
            return result.success;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Scale up exception:" << e.what();
            return false;
        }
    });
    
    // Step 6: Wait for deployment to be ready
    chain->add([=]() -> bool {
        if (!*deploymentExists) return true;
        
        qDebug() << "[JobManager] Waiting for deployment to be ready";
        
        try {
            bool ready = waitForPodsReady(deploymentName, 60); // 60 second timeout for startup
            
            if (ready) {
                qDebug() << "[JobManager] Auto-restart sequence completed successfully";
                emit jobFinished(QString("Restart %1").arg(deploymentName), true, 
                    QString("Auto-restart completed successfully for %1").arg(deploymentName));
            } else {
                qWarning() << "[JobManager] Auto-restart completed but deployment may not be ready";
                emit jobFinished(QString("Restart %1").arg(deploymentName), false, 
                    QString("Auto-restart completed but deployment may not be ready for %1").arg(deploymentName));
            }
            
            return ready;
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception in deployment ready check:" << e.what();
            emit jobFinished(QString("Restart %1").arg(deploymentName), false, 
                QString("Auto-restart failed: %1").arg(e.what()));
            return false;
        }
    });
    
    return chain;
}

void JobManager::onInstallerFinished(bool success)
{
    qDebug() << "[JobManager] Installer finished with result:" << success;
}

JobManager::JobResult JobManager::executeCommandsSync(const QStringList &commands)
{
    JobResult result;
    result.success = false;
    
    try {
        if (commands.isEmpty()) {
            result.errorMessage = "No commands provided";
            return result;
        }
        
        // For simple kubectl commands, use QProcess directly to ensure output capture
        QString command = commands.first();
        
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        
        // Set working directory and environment
        process.setWorkingDirectory("/tmp");
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        process.setProcessEnvironment(env);
        
        qDebug() << "[JobManager] Executing command:" << command;
        
        // Start the process
        process.start("/bin/bash", QStringList() << "-c" << command);
        
        if (!process.waitForStarted(3000)) { // Reduced from 5000 to 3000
            result.errorMessage = "Failed to start process: " + process.errorString();
            return result;
        }
        
        // Reduced timeout for node checks to prevent hanging
        int timeout = command.contains("get node") ? 5000 : 30000; // 5s for node checks, 30s for others
        
        if (!process.waitForFinished(timeout)) {
            qWarning() << "[JobManager] Process timed out after" << timeout << "ms";
            process.kill();
            process.waitForFinished(1000); // Brief wait for cleanup
            result.errorMessage = "Process timed out";
            return result;
        }
        
        int exitCode = process.exitCode();
        QProcess::ExitStatus exitStatus = process.exitStatus();
        
        result.output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
        
        result.success = (exitStatus == QProcess::NormalExit && exitCode == 0);
        
        if (!result.success) {
            result.errorMessage = QString("Process failed with exit code %1: %2")
                .arg(exitCode).arg(error.isEmpty() ? "Unknown error" : error);
        }
        
        // qDebug() << "[JobManager] Command result - Success:" << result.success 
        //          << "Exit code:" << exitCode
        //          << "Output:'" << result.output << "'"
        //          << "Error:'" << error << "'";
        
    } catch (const std::exception &e) {
        result.success = false;
        result.errorMessage = QString("Exception: %1").arg(e.what());
        qWarning() << "[JobManager] Exception in executeCommandsSync:" << e.what();
    } catch (...) {
        result.success = false;
        result.errorMessage = "Unknown error occurred";
        qCritical() << "[JobManager] Unknown exception in executeCommandsSync";
    }
    
    return result;
}

bool JobManager::waitForPodTermination(const QString &deploymentName, int maxWaitSec)
{
    qDebug() << "[JobManager] Waiting for deployment to scale down (max wait:" << maxWaitSec << "seconds)";
    
    int checkCount = 0;
    int maxChecks = maxWaitSec; // 1 check per second
    
    while (checkCount < maxChecks) {
        try {
            // Check deployment replicas
            const QString cmd = QString("kubectl get deployment %1 -n default -o jsonpath='{.status.replicas}' 2>/dev/null")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            if (result.success) {
                QString output = result.output.trimmed();
                qDebug() << "[JobManager] Deployment replicas:" << output;
                
                // If output is empty or "0", no replicas are running
                if (output.isEmpty() || output == "0") {
                    qDebug() << "[JobManager] Deployment scaled down after" << checkCount << "checks";
                    return true;
                }
            } else {
                qDebug() << "[JobManager] Deployment check failed:" << result.errorMessage;
                // If deployment doesn't exist, consider it terminated
                return true;
            }
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception during deployment check:" << e.what();
            return true;
        }
        
        checkCount++;
        if (checkCount % 10 == 0) {
            qDebug() << "[JobManager] Still waiting for deployment to scale down... (check" << checkCount << "/" << maxChecks << ")";
        }
        
        QThread::sleep(5);
    }
    
    qWarning() << "[JobManager] Deployment didn't scale down within" << maxWaitSec << "seconds";
    return false;
}

bool JobManager::forceDeletePods(const QString &deploymentName)
{
    qDebug() << "[JobManager] Force deleting pods for deployment:" << deploymentName;
    
    try {
        // Force delete all pods for this deployment
        const QString cmd = QString("kubectl delete pods -l app=%1 -n default --force --grace-period=0 --ignore-not-found")
            .arg(deploymentName);
        
        JobResult result = executeCommandsSync({cmd});
        
        if (result.success) {
            qDebug() << "[JobManager] Force delete completed successfully";
            // Wait a moment for cleanup to take effect
            QThread::sleep(5);
        } else {
            qWarning() << "[JobManager] Force delete failed:" << result.errorMessage;
        }
        
        return result.success;
        
    } catch (const std::exception &e) {
        qWarning() << "[JobManager] Exception during force delete:" << e.what();
        return false;
    }
}

bool JobManager::waitForPodsReady(const QString &deploymentName, int maxWaitSec)
{
    qDebug() << "[JobManager] Waiting for deployment to be ready (max wait:" << maxWaitSec << "seconds)";
    
    int checkCount = 0;
    int maxChecks = maxWaitSec; // 1 check per 10 second
    
    while (checkCount < maxChecks) {
        try {
            // Check deployment status in one command
            const QString cmd = QString("kubectl get deployment %1 -n default -o jsonpath='{.status.readyReplicas}/{.status.replicas}' 2>/dev/null")
                .arg(deploymentName);
            
            JobResult result = executeCommandsSync({cmd});
            
            qDebug() << "[JobManager] Deployment status check - Success:" << result.success 
                     << "Output:'" << result.output.trimmed() << "'";
            
            if (result.success) {
                QString output = result.output.trimmed();
                
                if (!output.isEmpty() && output.contains('/')) {
                    QStringList parts = output.split('/');
                    if (parts.size() == 2) {
                        int ready = parts[0].toInt();
                        int total = parts[1].toInt();
                        
                        qDebug() << "[JobManager] Deployment status: Ready=" << ready << "Total=" << total;
                        
                        // Check if all replicas are ready and we have at least 1
                        if (ready > 0 && ready == total) {
                            qDebug() << "[JobManager] Deployment is ready with" << ready << "replicas after" << checkCount << "checks";
                            return true;
                        } else {
                            qDebug() << "[JobManager] Deployment not fully ready yet. Ready:" << ready << "Total=" << total;
                        }
                    } else {
                        qDebug() << "[JobManager] Could not parse deployment status:" << output;
                    }
                } else if (output.isEmpty()) {
                    qDebug() << "[JobManager] No deployment status yet, continuing to wait...";
                } else {
                    qDebug() << "[JobManager] Unexpected deployment status format:" << output;
                }
            } else {
                qDebug() << "[JobManager] Deployment status check failed:" << result.errorMessage;
                // Don't immediately fail - deployment might be starting
            }
            
        } catch (const std::exception &e) {
            qWarning() << "[JobManager] Exception during deployment ready check:" << e.what();
            return true; // Don't fail the chain
        } catch (...) {
            qCritical() << "[JobManager] Unknown exception during deployment ready check";
            return true;
        }
        
        checkCount++;
        if (checkCount % 10 == 0) {
            qDebug() << "[JobManager] Waiting for deployment to be ready... (check" << checkCount << "/" << maxChecks << ")";
        }
        
        QThread::sleep(10);
    }
    
    qWarning() << "[JobManager] Deployment may not be fully ready after" << maxWaitSec << "seconds";
    return true; // Don't fail the chain, just warn
}

bool JobManager::deploymentExists(const QString &deploymentName)
{
    try {
        const QString cmd = QString("kubectl get deployment %1 -n default --no-headers 2>/dev/null")
            .arg(deploymentName);
        
        JobResult result = executeCommandsSync({cmd});
        
        bool exists = result.success && !result.output.trimmed().isEmpty();
        qDebug() << "[JobManager] Deployment" << deploymentName << "exists:" << exists;
        
        return exists;
        
    } catch (const std::exception &e) {
        qWarning() << "[JobManager] Exception checking deployment existence:" << e.what();
        return false;
    }
}
