# ml_mqtt_bridge.py
import json, time
import joblib
import paho.mqtt.client as mqtt
import pandas as pd

BROKER = " 10.39.45.145"
PORT = 1883
DEVICE_ID = "esp32-01"
SUB_TOPIC = f"iiot/sensors/{DEVICE_ID}"
PUB_TOPIC = f"iiot/alerts/{DEVICE_ID}"
THRESHOLD = 0.5

MODEL_PATH = "rf_model_weighted_no_datetime.joblib"
META_PATH  = "model_meta_weighted_no_datetime.json"

# Load artifacts
rf = joblib.load(MODEL_PATH)
with open(META_PATH, "r") as f:
    meta = json.load(f)
FEATURE_ORDER = meta["features"]
print("Model features:", FEATURE_ORDER)

def predict_from_json(payload: dict):
    # Build DF with correct feature order
    values = [payload[k] for k in FEATURE_ORDER]
    df = pd.DataFrame([values], columns=FEATURE_ORDER)
    prob = float(rf.predict_proba(df)[0, 1])   # class 1 = Anomaly
    label = "Anomaly" if prob >= THRESHOLD else "Normal"
    return label, prob

def on_connect(client, userdata, flags, rc):
    print("MQTT connected with rc:", rc)
    client.subscribe(SUB_TOPIC, qos=1)

def on_message(client, userdata, msg):
    try:
        data = json.loads(msg.payload.decode())
        label, prob = predict_from_json(data)
        alert = {
            "ts": data.get("ts"),
            "device_id": data.get("device_id"),
            "label": label,
            "probability": round(prob, 6),
            "threshold": THRESHOLD,
            "features_order": FEATURE_ORDER
        }
        client.publish(PUB_TOPIC, json.dumps(alert), qos=1, retain=True)
        print(f"[PREDICT] {label} p={prob:.3f}")
    except Exception as e:
        print("Error handling message:", e)

if __name__ == "__main__":
    client = mqtt.Client(client_id="rf-bridge")
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER, PORT, keepalive=30)
    client.loop_forever()
