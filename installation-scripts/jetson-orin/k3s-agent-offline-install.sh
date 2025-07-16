#!/bin/bash

sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 'mkdir -p ~/.dk/'
scp -r ../nxp-s32g root@192.168.56.49:~/.dk/

sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 'chmod +x ~/.dk/nxp-s32g/'
sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 'chmod +x ~/.dk/nxp-s32g/scripts'


sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 './.dk/nxp-s32g/dk_install.sh'

# WARN[0000] Unable to read /etc/rancher/k3s/k3s.yaml, please start server with --write-kubeconfig-mode or --write-kubeconfig-group to modify kube config permissions
export KUBECONFIG=~/.kube/config
mkdir ~/.kube 2> /dev/null
sudo k3s kubectl config view --raw > "$KUBECONFIG"
chmod 600 "$KUBECONFIG"

# Delete the node for new one connected
sudo kubectl delete node vip
