#!/bin/bash

# Create the host directory and file
sudo mkdir -p ~/.dk/sdv-runtime/
sudo touch ~/.dk/sdv-runtime/vss.json
sudo cp manifests/default_vss.json ~/.dk/sdv-runtime/vss.json


# Set proper permissions
sudo chmod 644 ~/.dk/sdv-runtime/vss.json
