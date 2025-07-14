# Overview


# Presequisites

## build docker image

### local built docker container
```shell
    # Common - linux/arm64 (local-arch build)
    docker build -t qnxcabin-service .

    # Push (Optional)
    # docker tag qnxcabin-service samtranbosch/qnxcabin-service
    # docker push samtranbosch/qnxcabin-service
```

### docker container from docker hub
```shell
    # Specific - multi-arch build and push to docker hub
    # - Enable Docker Buildx (if not already enabled) > Run the docker container
    docker buildx create --name qnxcabin-service_multiarch_build --use
    
    # Remeber to login
    # docker login
    # - Build - Establish connection to that docker container
    # docker buildx build --platform linux/amd64,linux/arm64,linux/arm64/v8 -t samtranbosch/qnxcabin-service:latest --push .
    docker buildx build --platform linux/amd64,linux/arm64 -t samtranbosch/qnxcabin-service:latest --push .
```


## Run
### local built docker container

```shell
    #Option 1 - Debug with share file
    docker run -v /media/sf_Workspace/AD/VSS/repo/FullHouse/dk_service_qnxcloud/:/app/src  --network="host" -d qnxcabin-service

    #Option 2 - Debug
    docker run --network="host" -d qnxcabin-service

    #Option 3 - Launch from docker hubs
    docker run --network="host" -d samtranbosch/qnxcabin-service

    docker kill qnxcabin-service
    docker rm qnxcabin-service
    docker run -d -it --name qnxcabin-service --network host --restart unless-stopped --log-opt max-size=10m --log-opt max-file=3 samtranbosch/qnxcabin-service:latest
```

### docker container from docker hub

```shell
    #Option 1
    docker pull samtranbosch/qnxcabin-service
    docker run --network="host" -d samtranbosch/qnxcabin-service

    #Option 2
    docker pull samtranbosch/qnxcabin-service:latest
    docker kill qnxcabin-service;docker rm qnxcabin-service;docker run -d -it --name qnxcabin-service --network host --restart unless-stopped --log-opt max-size=10m --log-opt max-file=3 samtranbosch/qnxcabin-service:latest
```


# Testing

## Environment
-   Workspace: /home/developer/workspace/
-   Ip address for Host VM - S32G

```shell
    developer@ubuntu-22:~$ curl ipinfo.io
    {
    "ip": "149.226.193.17",
    "city": "Singapore",
    "region": "Singapore",
    "country": "SG",
    "loc": "1.2897,103.8501",
    "org": "AS133466 Robert Bosch GmbH",
    "postal": "018989",
    "timezone": "Asia/Singapore",
    "readme": "https://ipinfo.io/missingauth"
    }
```

```shell
    # NetworkManager Restart
    sudo systemctl restart NetworkManager
```

```shell
    sudo ip link add name br0 type bridge
    sudo ip link set dev br0 up
    sudo ip link set dev enp0s8 master br0
    # Orin
    sudo ifconfig br0 192.168.56.48
    sudo ip link set br0 address 5e:5e:c9:a7:22:55

    # S32G
    sudo ifconfig br0 192.168.56.49

    pass: dev12345
```

```shell
    #Option 1
    # docker run -it --rm -p 50051:50051 -p 8090:8090 -e LOG_LEVEL=ALL -e KUKSAVAL_OPTARGS="--insecure" ghcr.io/eclipse/kuksa.val/kuksa-val:0.2.5
    # docker run -it -d -p 50051:50051 -p 8090:8090 -e LOG_LEVEL=ALL -e KUKSAVAL_OPTARGS="--insecure" ghcr.io/eclipse/kuksa.val/kuksa-val:0.2.5
    # docker run -it -d -p 50051:50051 -e LOG_LEVEL=ALL -e KUKSAVAL_OPTARGS="--insecure" ghcr.io/eclipse/kuksa.val/kuksa-val:0.2.5

    docker run -it -d --restart unless-stopped --network="host" -e LOG_LEVEL=ALL -e KUKSAVAL_OPTARGS="--insecure" ghcr.io/eclipse/kuksa.val/kuksa-val:0.2.5
    
    #Option 2
    cd /home/developer/Workspace/kuksa.val/kuksa-val-server/build/src/
    ./kuksa-val-server --insecure --vss ./vss_release_3.0.json &
```

```shell
    # Verify the Server Is Running and Listening on Port 8090
    netstat -tuln | grep 8090
    or
    lsof -i :8090
```

```shell
    kuksa-client grpc://127.0.0.1:55555

    authorize /home/developer/Workspace/kuksa.val/kuksa_certificates/jwt/super-admin.json.token
    authorize /media/sdv-orin/d75c2dae-f123-48ae-bcdd-a3d9830ba1b7/workspace/kuksa.val/kuksa_certificates/jwt/super-admin.json.token

    setValue Vehicle.Cabin.Seat.Row1.Pos1.Position 99
    setValue Vehicle.Body.Lights.IsLowBeamOn true
    setValue Vehicle.Body.Lights.IsHazardOn true
    setValue Vehicle.Body.Lights.IsBrakeOn true
    setValue Vehicle.Cabin.HVAC.Station.Row1.Left.FanSpeed 99
    setValue Vehicle.Cabin.HVAC.Station.Row1.Right.FanSpeed 99

    setValue Vehicle.Cabin.Seat.Row1.Pos1.Position 10
    setValue Vehicle.Body.Lights.IsLowBeamOn false
    setValue Vehicle.Body.Lights.IsHazardOn false
    setValue Vehicle.Body.Lights.IsBrakeOn false
    setValue Vehicle.Cabin.HVAC.Station.Row1.Left.FanSpeed 10
    setValue Vehicle.Cabin.HVAC.Station.Row1.Right.FanSpeed 10

```

```shell
    kuksa-client grpc://127.0.0.1:55555

    authorize /home/developer/Workspace/kuksa.val/kuksa_certificates/jwt/super-admin.json.token
    authorize /media/sdv-orin/d75c2dae-f123-48ae-bcdd-a3d9830ba1b7/workspace/kuksa.val/kuksa_certificates/jwt/super-admin.json.token

    subscribe Vehicle.Cabin.Seat.Row1.Pos1.Position
    subscribe Vehicle.Body.Lights.IsLowBeamOn
    subscribe Vehicle.Body.Lights.IsHazardOn
    subscribe Vehicle.Body.Lights.IsBrakeOn
    subscribe Vehicle.Cabin.HVAC.Station.Row1.Left.FanSpeed
    subscribe Vehicle.Cabin.HVAC.Station.Row1.Right.FanSpeed

```shell

```shell
    "host": "test.mosquitto.org",
    "port": 1883,
```

### Test on DreamKIT

```shell
    # Clone the software
    # Execute the bash file
    chmod +x ./run.sh
    ./run.sh
```