#!/usr/bin/env python3
import json
import time
import threading
import logging
from pathlib import Path

import paho.mqtt.client as mqtt
from paho.mqtt.client import Client as MQTTClient, CallbackAPIVersion

import asyncio
from kuksa_client.grpc import VSSClient
from kuksa_client.grpc import Datapoint
from kuksa_client.grpc.aio import VSSClient as AsyncVSSClient

"""
KUKSA Data Broker Python Client Example

This script demonstrates how to connect to KUKSA Data Broker and perform
common operations like reading, writing, and subscribing to vehicle signals.

Requirements:
- pip install kuksa-client
- KUKSA Data Broker running (default: localhost:55555)
"""

# 
CONFIG_FILE = "config.json"

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

# if you smooth certain VSS‐signals, keep this list
vssSignals = [
    "Vehicle.Cabin.HVAC.Station.Row1.Left.FanSpeed",
    "Vehicle.Cabin.HVAC.Station.Row1.Right.FanSpeed"
]

def load_config(filepath):
    try:
        with Path(filepath).open() as f:
            return json.load(f)
    except Exception as e:
        logging.error("Unable to load config '%s': %s", filepath, e)
        return None

def init_logging(debug_enabled):
    level = logging.DEBUG if debug_enabled else logging.INFO
    fmt   = "[%(asctime)s] %(levelname)-5s %(threadName)s: %(message)s"
    logging.basicConfig(level=level, format=fmt)


# -----------------------------------------------------------------------------
# MQTT Subscriber
# -----------------------------------------------------------------------------
class MQTTSubscriber(threading.Thread):
    def __init__(self, config):
        super().__init__(name="MQTT_Sub", daemon=True)
        self.config    = config
        self.subscribed = False
        self.sync_lock  = threading.Lock()

        self.client = MQTTClient(
            client_id="MQTT_Subscriber",
            callback_api_version=CallbackAPIVersion.VERSION2
        )
        self.client.username_pw_set(config["username"], config["password"])
        self.client.on_connect    = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_message    = self.on_message

    def on_connect(self, client, userdata, flags, rc, props=None):
        if rc == 0 and not self.subscribed:
            topic = f"etas.qnx.signal.actuate.{self.config['instance']}"
            client.subscribe(topic, qos=1)
            self.subscribed = True
            logging.info("Subscribed to %s", topic)
        elif rc != 0:
            logging.error("Subscriber connect failed: rc=%s", rc)

    def on_disconnect(self, client, userdata, rc, props=None):
        logging.warning("Subscriber disconnected: rc=%s", rc)
        self.subscribed = False
        if rc != 0:
            threading.Timer(5, self._reconnect).start()

    def _reconnect(self):
        try:
            self.client.reconnect()
        except Exception as e:
            logging.error("Subscriber reconnect failed: %s", e)

    def on_message(self, client, userdata, msg):
        try:
            payload = msg.payload.decode('utf-8').rstrip('\x00')
            data    = json.loads(payload)
        except Exception as e:
            logging.warning("Bad payload on %s: %s", msg.topic, e)
            return

        items = data if isinstance(data, list) else [data]
        for item in items:
            threading.Thread(
                target=self.process_signal_update_sync,
                args=(item,),
                daemon=True
            ).start()

    def process_signal_update_sync(self, data):
        path      = data.get("path")
        new_value = data.get("value")
        if path is None or new_value is None:
            logging.error("Missing 'path' or 'value': %s", data)
            return

        received = path.strip()
        signal_obj = None
        with self.sync_lock:
            for act in self.config["actuators"]:
                if act["path"].strip() == received:
                    signal_obj = act
                    break

        if not signal_obj:
            logging.warning("Unknown actuator path: %s", path)
            return

        current = signal_obj.get("value", 0)
        db_client = self.config.get("kuksa_client")
        if not db_client:
            logging.warning("No DataBroker client configured. Skipping %s", path)
            return

        try:
            # same smoothing/direct logic you had
            if current == new_value:
                db_client.setValue(path, str(new_value).lower())
            else:
                if path in vssSignals:
                    # reuse your smooth update
                    self.smooth_update_sync(db_client, path, current, new_value)
                else:
                    db_client.setValue(path, str(new_value).lower())
            logging.debug("Wrote %s: %s → %s", path, current, new_value)
        except Exception as e:
            logging.error("Error updating %s: %s", path, e)
            return

        with self.sync_lock:
            signal_obj["value"] = new_value

    def smooth_update_sync(self, client, path, current_value, new_value):
        """
        Send intermediate values from current_value → new_value.
        If |new-current| < max_steps, we only use as many steps as the gap.
        If the gap is < 1 unit, we skip smoothing entirely.
        """
        # nothing to do
        if current_value == new_value:
            return

        diff     = new_value - current_value
        abs_diff = abs(diff)
        max_steps = 10

        # pick steps = min(max_steps, int(abs_diff)) if gap >=1, else no smoothing
        if abs_diff < 1:
            # gap too small to smooth
            client.setValue(path, str(new_value).lower())
            logging.debug("Smooth: gap <1, direct %s→%s", current_value, new_value)
            return
        elif abs_diff < max_steps:
            steps = int(abs_diff)
        else:
            steps = max_steps

        # if steps<2, just do a direct jump
        if steps < 2:
            client.setValue(path, str(new_value).lower())
            logging.debug("Smooth: steps<2, direct %s→%s", current_value, new_value)
            return

        logging.debug("Smooth: %s→%s in %s steps", current_value, new_value, steps)
        for i in range(1, steps):
            # linear interpolation
            intermediate = current_value + diff * (i / steps)
            # round to int if you expect integer signals
            val = int(round(intermediate))
            client.setValue(path, str(val).lower())
            time.sleep(0.1)

        # final exact value
        client.setValue(path, str(new_value).lower())
        logging.debug("Smooth: done %s→%s", current_value, new_value)


    def run(self):
        self.client.reconnect_delay_set(min_delay=1, max_delay=60)
        try:
            self.client.connect(
                self.config["mqtt_host"],
                self.config["mqtt_port"],
                keepalive=60
            )
        except Exception as e:
            logging.error("Subscriber initial connect failed: %s", e)
            self._reconnect()

        self.client.loop_start()
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            self.client.disconnect()
            self.client.loop_stop()


# -----------------------------------------------------------------------------
# MQTT Publisher
# -----------------------------------------------------------------------------
class MQTTPublisher(threading.Thread):
    def __init__(self, config):
        super().__init__(name="MQTT_Pub", daemon=True)
        self.cfg       = config
        self.connected = False

        self.client = mqtt.Client(
            client_id="MQTT_Publisher",
            callback_api_version=CallbackAPIVersion.VERSION2
        )
        self.client.username_pw_set(config["username"], config["password"])
        self.client.on_connect    = self.on_connect
        self.client.on_disconnect = self.on_disconnect
        self.client.on_publish    = self.on_publish

    def on_connect(self, client, userdata, flags, rc, props=None):
        if rc == 0:
            self.connected = True
            logging.info("Publisher connected to %s:%s",
                         self.cfg["mqtt_host"], self.cfg["mqtt_port"])
        else:
            logging.error("Publisher connect failed, rc=%s", rc)

    def on_disconnect(self, client, userdata, rc, props=None):
        self.connected = False
        logging.warning("Publisher disconnected, rc=%s", rc)
        if rc != 0:
            threading.Timer(5, self.reconnect).start()

    # MQTT v5 signature: client, userdata, mid, reasonCode, properties
    def on_publish(self, client, userdata, mid, reasonCode, properties):
        logging.debug("Published mid=%s reason=%s", mid, reasonCode)

    def publish_message(self, topic, payload, qos=1):
        if not self.connected:
            logging.warning("Drop publish to %s (not connected)", topic)
            return
        info = self.client.publish(topic, payload, qos=qos)
        info.wait_for_publish(timeout=5)

    def reconnect(self):
        try:
            self.client.reconnect()
        except Exception as e:
            logging.error("Publisher reconnect failed: %s", e)

    def run(self):
        self.client.reconnect_delay_set(min_delay=1, max_delay=60)
        try:
            self.client.connect(self.cfg["mqtt_host"],
                                self.cfg["mqtt_port"], keepalive=60)
        except Exception as e:
            logging.error("Initial publisher connect failed: %s", e)
            self.reconnect()

        self.client.loop_start()
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            pass
        finally:
            self.client.disconnect()
            self.client.loop_stop()


class KuksaClientExample:
    def __init__(self, address="127.0.0.1", port=55555):
        """Initialize KUKSA client with broker address and port."""
        self.address = address
        self.port = port
        self.client = None
    
    def connect_sync(self):
        """Connect to KUKSA Data Broker synchronously."""
        try:
            # Create synchronous client
            self.client = VSSClient(self.address, self.port)
            self.client.connect();
            logger.info(f"Connected to KUKSA Data Broker at {self.address}:{self.port}")
            return True
        except Exception as e:
            logger.error(f"Failed to connect: {e}")
            return False
    
    def read_current_values(self, signal_paths):
        """Read current values of signals from the data broker."""
        try:
            if isinstance(signal_paths, str):
                signal_paths = [signal_paths]
            
            response = self.client.get_current_values(signal_paths)
            if response:
                logger.info("=== Current Values ===")
                for signal, datapoint in response.items():
                    logger.info(f"Signal: {signal}")
                    logger.info(f"Current Value: {datapoint.value}")
                    logger.info(f"Timestamp: {datapoint.timestamp}")
                return response
        except Exception as e:
            logger.error(f"Error reading current values: {e}")
            return None
    
    def read_target_values(self, signal_paths):
        """Read target values of signals from the data broker."""
        try:
            if isinstance(signal_paths, str):
                signal_paths = [signal_paths]
            
            response = self.client.get_target_values(signal_paths)
            if response:
                logger.info("=== Target Values ===")
                for signal, datapoint in response.items():
                    logger.info(f"Signal: {signal}")
                    logger.info(f"Target Value: {datapoint.value}")
                    logger.info(f"Timestamp: {datapoint.timestamp}")
                return response
        except Exception as e:
            logger.error(f"Error reading target values: {e}")
            return None
    
    def read_both_values(self, signal_paths):
        """Read both current and target values for comparison."""
        try:
            if isinstance(signal_paths, str):
                signal_paths = [signal_paths]
            
            current_response = self.client.get_current_values(signal_paths)
            target_response = self.client.get_target_values(signal_paths)
            
            logger.info("=== Current vs Target Values ===")
            for signal in signal_paths:
                current_val = current_response.get(signal)
                target_val = target_response.get(signal)
                
                current_value = current_val.value if current_val else "N/A"
                target_value = target_val.value if target_val else "N/A"
                
                logger.info(f"Signal: {signal}")
                logger.info(f"  Current: {current_value}")
                logger.info(f"  Target:  {target_value}")
                logger.info(f"  Match: {current_value == target_value}")
                logger.info("-" * 40)
            
            return {"current": current_response, "target": target_response}
        except Exception as e:
            logger.error(f"Error reading both values: {e}")
            return None
    
    def write_current_values(self, signal_updates):
        """Write current values to signals."""
        try:
            if isinstance(signal_updates, dict):
                # Convert dict to proper format
                updates = {}
                for signal_path, value in signal_updates.items():
                    updates[signal_path] = Datapoint(value=value)
            else:
                updates = signal_updates
            
            self.client.set_current_values(updates)
            logger.info("=== Current Values Written ===")
            for signal, datapoint in updates.items():
                logger.info(f"Signal: {signal} = {datapoint.value}")
            return True
        except Exception as e:
            logger.error(f"Error writing current values: {e}")
            return False
    
    def write_target_values(self, signal_updates):
        """Write target values to signals."""
        try:
            if isinstance(signal_updates, dict):
                # Convert dict to proper format
                updates = {}
                for signal_path, value in signal_updates.items():
                    updates[signal_path] = Datapoint(value=value)
            else:
                updates = signal_updates
            
            self.client.set_target_values(updates)
            logger.info("=== Target Values Written ===")
            for signal, datapoint in updates.items():
                logger.info(f"Signal: {signal} = {datapoint.value}")
            return True
        except Exception as e:
            logger.error(f"Error writing target values: {e}")
            return False
    
    def write_both_values(self, signal_updates):
        """Write both current and target values (useful for initialization)."""
        try:
            success_current = self.write_current_values(signal_updates)
            success_target = self.write_target_values(signal_updates)
            return success_current and success_target
        except Exception as e:
            logger.error(f"Error writing both values: {e}")
            return False
    
    def subscribe_to_current_values(self, signal_paths, callback_func):
        """Subscribe to current value changes."""
        try:
            logger.info(f"Subscribing to current values: {signal_paths}")
            for update in self.client.subscribe_current_values(signal_paths):
                callback_func(update, "current")
        except Exception as e:
            logger.error(f"Error subscribing to current values: {e}")
    
    def subscribe_to_target_values(self, signal_paths, callback_func):
        """Subscribe to target value changes."""
        try:
            logger.info(f"Subscribing to target values: {signal_paths}")
            for update in self.client.subscribe_target_values(signal_paths):
                callback_func(update, "target")
        except Exception as e:
            logger.error(f"Error subscribing to target values: {e}")
    
    def subscribe_to_both_values(self, signal_paths, callback_func):
        """Subscribe to both current and target value changes."""
        import threading
        
        def current_subscription():
            self.subscribe_to_current_values(signal_paths, callback_func)
        
        def target_subscription():
            self.subscribe_to_target_values(signal_paths, callback_func)
        
        # Start both subscriptions in separate threads
        current_thread = threading.Thread(target=current_subscription, daemon=True)
        target_thread = threading.Thread(target=target_subscription, daemon=True)
        
        current_thread.start()
        target_thread.start()
        
        return current_thread, target_thread
    
    def disconnect(self):
        """Disconnect from the data broker."""
        if self.client:
            self.client.disconnect()
            logger.info("Disconnected from KUKSA Data Broker")

# -----------------------------------------------------------------------------
# DataBrokerClient
# -----------------------------------------------------------------------------
class DataBrokerClient(threading.Thread):
    """
    - GRPC connect + authorize
    - initial getValue → MQTT publish
    - subscribe to signals & actuators
    - on signal update → MQTT publish
    - setValue(path, value) for actuator writes
    - auto‐reconnect on failure
    """
    def __init__(self, config):
        super().__init__(name="DataBroker", daemon=True)
        self.cfg       = config
        self.db_host   = config["databroker"]["host"]
        self.db_port   = config["databroker"]["port"]
        self.db_insec  = config["databroker"].get("insecure", True)
        self.db_token  = config["databroker"]["token"]
        self.signals   = config["signals"]
        self.actuators = config["actuators"]
        self.mqtt_pub  = config["mqtt_publisher"]
        self.instance  = config["instance"]
        self.backoff   = config.get("backoff", 5)

        self._stop_ev = threading.Event()
        self._lock    = threading.Lock()
        self.client   = None

    def _extract_dp_value(self, dp):
        # primitive?
        if isinstance(dp, (str, bool, int, float)):
            return dp
        # .value attribute?
        if hasattr(dp, "value"):
            try:
                return dp.value
            except Exception:
                pass
        # dict with "value"
        if isinstance(dp, dict) and "value" in dp:
            return dp["value"]
        return None

    def _initial_sync(self):
        for sig in self.signals:
            path = sig["path"]
            try:
                dp  = self.client.read_target_values(path)
                val = self._extract_dp_value(dp)
                if val is not None:
                    sig["value"] = val
                    topic   = f"etas.qnx.signal.publish.{self.instance}"
                    payload = json.dumps({"path": path, "value": val})
                    self.mqtt_pub.publish_message(topic, payload)
                    logging.debug("Initial sync %s=%s", path, val)
            except Exception as e:
                logging.warning("Initial sync failed %s: %s", path, e)

    def _subscribe_all(self):
        # Your VSS signals
        vss_signals = [
            "Vehicle.Cabin.Seat.Row1.Pos1.Position",
            "Vehicle.Body.Lights.IsLowBeamOn",
            "Vehicle.Body.Lights.IsHazardOn",
            "Vehicle.Body.Lights.IsBrakeOn",
            "Vehicle.Cabin.HVAC.Station.Row1.Left.FanSpeed",
            "Vehicle.Cabin.HVAC.Station.Row1.Right.FanSpeed"
        ]
        self.client.subscribe_to_target_values(vss_signals, self.signal_callback)
        # 
        # for group in ("signals", "actuators"):
        #     for entry in getattr(self, group):
        #         p = entry["path"]
        #         self.client.subscribe_to_target_values(p, self._on_db_message)
        #         logging.debug("Subscribed to DB %s", p)

    def _extract_raw_value(self, raw):
        if isinstance(raw, (str, bool, int, float)):
            return raw
        if isinstance(raw, dict):
            for k in ("bool","int","double","float","string","value"):
                if k in raw:
                    return raw[k]
        return raw
    
    def signal_callback(self, update, value_type="current"):
        """Callback function for signal updates."""
        logger.info(f"=== {value_type.upper()} Value Update ===")
        for signal, datapoint in update.items():
            logger.info(f"Signal: {signal}")
            logger.info(f"{value_type.title()} Value: {datapoint.value}")
            logger.info(f"Timestamp: {datapoint.timestamp}")
            logger.info("-" * 40)
        self._on_db_message(signal, datapoint.value)

    def _on_db_message(self, path, value):
        with self._lock:
            for sig in self.signals:
                if sig["path"] == path:
                    if sig.get("value") != value:
                        sig["value"] = value
                        topic   = f"etas.qnx.signal.publish.{self.instance}"
                        payld   = json.dumps({"path": path, "value": value})
                        logging.info("DB→MQTT %s=%s", path, value)
                        self.mqtt_pub.publish_message(topic, payld)
                    break

    def set_value(self, path, value):
        """Thread‐safe write into DataBroker for actuators."""
        try:
            updates = {path: value}
            self.client.write_target_values(updates)
            logging.debug("MQTT→DB setValue %s=%s", path, value)
            with self._lock:
                for act in self.actuators:
                    if act["path"] == path:
                        act["value"] = value
        except Exception as e:
            logging.error("Failed to write %s=%s to DB: %s", path, value, e)

    # alias so your existing code can call .setValue()
    setValue = set_value

    def run(self):
        while not self._stop_ev.is_set():
            try:
                # connect & authorize
                cfg = {
                    "protocol":   "grpc",
                    "serveraddr": self.db_host,
                    "port":       self.db_port,
                    "insecure":   self.db_insec
                }
                self.client = KuksaClientExample()
                
                # Connect to broker
                if not self.client.connect_sync():
                    logger.error("Failed to connect to KUKSA Data Broker")
                    return
                logging.info("Connected to DataBroker %s:%s", self.db_host, self.db_port)

                # initial sync + subscribe
                self._initial_sync()
                self._subscribe_all()

                # idle until stop()
                while not self._stop_ev.is_set():
                    time.sleep(0.2)

            except Exception as e:
                logging.error("DataBrokerClient error: %s   retry in %ss", e, self.backoff)
                time.sleep(self.backoff)

        # cleanup
        if self.client:
            self.client.disconnect()
            logging.info("DataBrokerClient stopped")

    def stop(self):
        self._stop_ev.set()


# -----------------------------------------------------------------------------
# Main: wire it all up
# -----------------------------------------------------------------------------
def main():
    cfg = load_config(CONFIG_FILE)
    if not cfg:
        exit(1)

    # flatten for MQTT threads + DataBrokerClient
    config_data = {
        "mqtt_host":  cfg["broker"]["host"],
        "mqtt_port":  cfg["broker"]["port"],
        "username":   cfg["broker"]["username"],
        "password":   cfg["broker"]["password"],
        "signals":    cfg.get("signal", []),
        "actuators":  cfg.get("actuator", []),
        "instance":   cfg.get("instance", ""),
        "debug":      cfg.get("debug", False),
        "databroker": cfg["databroker"]
    }

    init_logging(config_data["debug"])

    # 1) Publisher
    mqtt_pub = MQTTPublisher(config_data)
    config_data["mqtt_publisher"] = mqtt_pub
    mqtt_pub.start()

    # 2) DataBrokerClient
    db_client = DataBrokerClient(config_data)
    config_data["kuksa_client"] = db_client
    db_client.start()

    # 3) Subscriber
    mqtt_sub = MQTTSubscriber(config_data)
    mqtt_sub.start()

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        logging.info("Shutting down…")
    finally:
        db_client.stop()
        mqtt_pub.client.disconnect()
        mqtt_sub.client.disconnect()

if __name__ == "__main__":
    main()
