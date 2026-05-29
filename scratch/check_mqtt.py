import paho.mqtt.client as mqtt
import time

broker = "192.168.1.251"
user = "bert"
password = "JustBert1"

received_eup = []

def on_connect(client, userdata, flags, rc):
    client.subscribe("homeassistant/#")
    client.subscribe("eup/#")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode('utf-8', errors='ignore')
    if "eup" in topic.lower():
        received_eup.append((topic, payload))

client = mqtt.Client()
client.username_pw_set(user, password)
client.on_connect = on_connect
client.on_message = on_message

client.connect(broker, 1883, 60)

start_time = time.time()
client.loop_start()
while time.time() - start_time < 3:
    time.sleep(0.1)
client.loop_stop()

print(f"=== EUP related topics ({len(received_eup)}) ===")
for topic, payload in received_eup:
    print(f"Topic: {topic}")
print("================================")
