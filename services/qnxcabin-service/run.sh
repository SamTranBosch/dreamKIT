
#DOCKER
docker run -it -d --restart unless-stopped --network="host" -e LOG_LEVEL=ALL -e KUKSAVAL_OPTARGS="--insecure" ghcr.io/eclipse/kuksa.val/kuksa-val:0.2.5

#IVI
export DISPLAY=:0
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/usr/local/Qt-6.6.0/lib/

cd ../../In-Vehicle-Software/sdvivi/build
# To build
qmake ..
make -j

./sdvivi &

#DOCKER
docker pull samtranbosch/dreamkit_qnxcloud:latest
docker kill dreamkit_qnxcloud;docker rm dreamkit_qnxcloud;docker run -d -it --name dreamkit_qnxcloud --network host --restart unless-stopped --log-opt max-size=10m --log-opt max-file=3 samtranbosch/dreamkit_qnxcloud:latest

#IVI-VSS
cd ../../../../workspace/40/vrte/usr/bin

export LD_LIBRARY_PATH=/opt/vrte/lib:/opt/vrte/usr/lib:../lib:\$LD_LIBRARY_PATH
./soa_client_vss &
