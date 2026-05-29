import paho.mqtt.client as mqtt
import json
import time

broker = "192.168.1.251"
user = "bert"
password = "JustBert1"
topic = "eup/data"

mock_payload = {
    "soc": 82.5,          # 82.5% Batterie
    "volt": 13.2,         # 13.2V Bordnetzspannung
    "temp": 21.0,         # 21°C Batterietemperatur
    "range": 210,         # 210 km Reichweite
    "power": -350,        # -350 Watt (Ruhestrom/Standby)
    "odo": 48250,         # 48.250 km Kilometerstand
    "service_days": 210,  # 210 Tage bis Service
    "service_km": 12500,  # 12.500 km bis Service
    "bat_cap": 96.5,      # 96.5 Ah Batteriekapazität
    "tp_alarm": 0,        # Kein Reifendruckalarm (0 = OK)
    "ts": int(time.time()),
    "src": "MOCK_INJECT"
}

client = mqtt.Client()
client.username_pw_set(user, password)

try:
    client.connect(broker, 1883, 60)
    client.loop_start()
    
    payload_str = json.dumps(mock_payload)
    print(f"Publishing mock data to {topic}: {payload_str}")
    
    ret = client.publish(topic, payload_str, retain=True)
    print(f"Publish status: {ret.rc}")
    
    # Also update lastSync topic
    time_str = time.strftime("%Y-%m-%dT%H:%M:%S+02:00")
    client.publish("eup/lastSync", time_str, retain=True)
    print(f"Updated lastSync to: {time_str}")
    
    time.sleep(1)
    client.loop_stop()
    print("Mock data successfully injected!")
except Exception as e:
    print(f"Error: {e}")
