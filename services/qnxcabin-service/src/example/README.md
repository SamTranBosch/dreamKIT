# ETAS Demo Python Sample

The Python samples demonstrate the process of publishing MQTT messages and illustrate the anticipated message format for signal data. Please note that the config.json file is subject to modification upon finalization of the signal data specifications.

## Python Version
This project has been developed and tested using Python 3.10 and 3.11. To confirm that you have the correct Python version installed, you can use the following command.

```
$ python3 --version 
Python 3.11.8
```

## Method 1: Setup in virtual environment

The samples depend on the paho-mqtt library. To install the dependencies, run the following commands.

> python3 -m venv venv
> . venv/bin/activate
> pip3 install -r requirements.txt

### Config

Both samples depend on the config.json configuration file present in the repo. The config file will determine the address of the MQTT broker to connect to and what sample data to be sent. Additionally there is an "instance" field which can be used to add a suffix to any MQTT message topics to avoid collisions between different users.

### Subscriber

The subscriber sample will subscribe to both the etas.qnx.signal.publish.<instance> and the etas.qnx.signal.actuate.<instance> MQTT topics for a given instance. 

> . venv/bin/activate
> ./etas-mqtt-subscribe.py

### Publisher

The publisher sample will periodically publish the sample signals to the MQTT broker.

Messages defined in the "signal" section of the config file will be sent to the topic etas.qnx.signal.publish.<instance>.
Messages defined in the "actuator" section of the config file will be sent to the topic etas.qnx.signal.actuate.<instance>.

> . venv/bin/activate
> ./etas-mqtt-publish.py

## Method 2 Setup in non-virtual environment

The samples depend on the paho-mqtt library. To install the dependencies, run the following commands.

Install the paho-mqtt 2.X using Pip.

> pip3 install paho-mqtt

### Config

Both samples depend on the config.json configuration file present in the repo. The config file will determine the address of the MQTT broker to connect to and what sample data to be sent. Additionally there is an "instance" field which can be used to add a suffix to any MQTT message topics to avoid collisions between different users.

### Subscriber

The subscriber sample will subscribe to both the etas.qnx.signal.publish.<instance> and the etas.qnx.signal.actuate.<instance> MQTT topics for a given instance. 

> python3 ./etas-mqtt-subscribe.py

### Publisher

The publisher sample will periodically publish the sample signals to the MQTT broker.

Messages defined in the "signal" section of the config file will be sent to the topic etas.qnx.signal.publish.<instance>.
Messages defined in the "actuator" section of the config file will be sent to the topic etas.qnx.signal.actuate.<instance>.

> python3 ./etas-mqtt-publish.py


## Test

### Step 1: Provide ETAS IP to whitelist
Please provide the ETAS IP address for whitelisting on the QNX MQTT broker.  

### Step 2: Test Subscribe
Upon executing the MQTT subscription script 'etas-mqtt-subscribe.py', the client will establish a successful connection and enter a state of awaiting published messages from the publisher.

```
$ python3 etas-mqtt-subscriber.py 
Connected to: 3.239.90.138:80
subcribing to: etas.qnx.signal.actuate.10
subcribing to: etas.qnx.signal.publish.10
```

### Step 3: Test Publish
Upon execution of the MQTT message publishing script 'etas-mqtt-publish.py', the client will establish a successful connection and transmit messages. Concurrently, the 'etas-mqtt-subscribe.py' script will successfully receive these messages.

```
$ python3 etas-mqtt-publisher.py 
Publishing to: etas.qnx.signal.publish.10. Message: [{"path": "Vehicle.Cabin.Seat.Row1.Pos1.Position", "value": 3}, {"path": "Vehicle.Body.Lights.IsLowBeamOn", "value": true}, {"path": "Vehicle.Body.Lights.IsHazardOn", "value": true}, {"path": "Vehicle.Body.Lights.IsBrakeOn", "value": true}]
Connected to: 3.239.90.138:80
Publishing to: etas.qnx.signal.actuate.10. Message: [{"path": "Vehicle.Cabin.HVAC.Station.Row1.Left.FanSpeed", "value": 86}, {"path": "Vehicle.Cabin.HVAC.Station.Row1.Right.FanSpeed", "value": 87}]
Publishing to: etas.qnx.signal.publish.10. Message: [{"path": "Vehicle.Cabin.Seat.Row1.Pos1.Position", "value": 3}, {"path": "Vehicle.Body.Lights.IsLowBeamOn", "value": true}, {"path": "Vehicle.Body.Lights.IsHazardOn", "value": true}, {"path": "Vehicle.Body.Lights.IsBrakeOn", "value": true}]

$ python3 etas-mqtt-subscriber.py 
Connected to: 3.239.90.138:80
subcribing to: etas.qnx.signal.actuate.10
subcribing to: etas.qnx.signal.publish.10
Message on Topic: etas.qnx.signal.publish.10 Payload: b'[{"path": "Vehicle.Cabin.Seat.Row1.Pos1.Position", "value": 3}, {"path": "Vehicle.Body.Lights.IsLowBeamOn", "value": true}, {"path": "Vehicle.Body.Lights.IsHazardOn", "value": true}, {"path": "Vehicle.Body.Lights.IsBrakeOn", "value": true}]'
Message on Topic: etas.qnx.signal.actuate.10 Payload: b'[{"path": "Vehicle.Cabin.HVAC.Station.Row1.Left.FanSpeed", "value": 86}, {"path": "Vehicle.Cabin.HVAC.Station.Row1.Right.FanSpeed", "value": 87}]'
```
