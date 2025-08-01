#!/bin/bash

sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 'mkdir -p ~/.dk/'
scp -r ../nxp-s32g root@192.168.56.49:~/.dk/

sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 'chmod +x ~/.dk/nxp-s32g/'
sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 'chmod +x ~/.dk/nxp-s32g/scripts'


sshpass -p '' ssh -o StrictHostKeyChecking=no root@192.168.56.49 './.dk/nxp-s32g/dk_install.sh'

# Delete the node for new one connected
sudo kubectl delete node vip
