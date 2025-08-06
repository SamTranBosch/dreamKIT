#!/bin/bash

# K3s Debug Script: Launch k9s for debugging K3s cluster
# Usage: sudo ./k3s-debug.sh

# For regular user access, copy the kubeconfig file
sudo mkdir -p ~/.kube
sudo cp /etc/rancher/k3s/k3s.yaml ~/.kube/config
sudo chown $(id -u):$(id -g) ~/.kube/config

# K3s Debug Script: Launch k9s for debugging K3s cluster
# Usage: sudo ./k3s-debug.sh
k9s --kubeconfig ~/.kube/config
