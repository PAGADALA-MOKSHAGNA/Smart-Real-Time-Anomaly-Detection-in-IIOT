# app.py
from flask import Flask, jsonify, request
from flask_cors import CORS
import requests
import joblib
import json
import numpy as np
import os
import logging

# Optional: enable CORS so browser dashboard can fetch from this server
# If you want CORS, install flask_cors and uncomment the two lines below:
# from flask_cors import CORS
# CORS(app)

app = Flask(__name__)
CORS(app, origins=["http://192.168.31.187"])
logging.basicConfig(level=logging.INFO)

# ----- CONFIG -----
ESP32_IP = "192.168.31.187"           # change to your ESP32 IP or keep as variable
ESP32_DATA_URL = f"http://{ESP32_IP}/data"
ESP_AUTH = ('Mokshagna', 'MLG333')   # Basic auth credentials for ESP32

MODEL_PATH = "rf_model_2.joblib"
META_PATH = "model_meta_weighted_no_datetime.json"

# ----- LOAD MODEL + META AT STARTUP -----
if not os.path.exists(MODEL_PATH):
    raise FileNotFoundError(f"Model file not found at: {MODEL_PATH}")
if not os.path.exists(META_PATH):
    raise FileNotFoundError(f"Meta file not found at: {META_PATH}")

# Load model
model = joblib.load(MODEL_PATH)
app.logger.info(f"Loaded model from {MODEL_PATH}. n_features_in_ = {getattr(model, 'n_features_in_', 'unknown')}")

# Load feature order metadata
with open(META_PATH, "r") as f:
    meta = json.load(f)
FEATURE_ORDER = meta.get("features", [])
if not isinstance(FEATURE_ORDER, list) or len(FEATURE_ORDER) == 0:
    raise RuntimeError("Meta file must contain 'features' list in the expected order.")
app.logger.info(f"Feature order loaded from meta: {FEATURE_ORDER}")

# Validate length: should be 9
if len(FEATURE_ORDER) != 9:
    app.logger.warning(f"Meta features length is {len(FEATURE_ORDER)} (expected 9). Proceeding anyway.")

# ----- HELPER: extract and build sample array -----
def build_sample_from_esp_json(esp_json):
    """
    Extracts the nine sensor values in the strict order:
    1. Temperature_C
    2. Pressure_hPa
    3. AngleX
    4. AngleY
    5. AngleZ
    6. AccX_g
    7. AccY_g
    8. AccZ_g
    9. Altitude_m

    Angle negative values -> convert to positive (abs) and round to integers.
    Returns numpy array shaped (1, 9) of floats (or ints where specified).
    """
    keys = [
        "Temperature_C",
        "Pressure_hPa",
        "AngleX",
        "AngleY",
        "AngleZ",
        "AccX_g",
        "AccY_g",
        "AccZ_g",
        "Altitude_m"
    ]

    values = []
    for k in keys:
        if k not in esp_json:
            raise KeyError(f"Key '{k}' missing from ESP32 JSON payload")

        raw = esp_json[k]
        # Try to convert to float; handle strings
        try:
            val = float(raw)
        except Exception as e:
            # if value can't be parsed, raise a clear error
            raise ValueError(f"Cannot convert value for '{k}' to float: {raw}") from e

        # For angles: convert negative to positive and round to integer
        if k in ("AngleX", "AngleY", "AngleZ"):
            val = abs(val)
            val = int(round(val))
        else:
            # keep as float (no rounding specified)
            # but ensure finite numeric
            if np.isnan(val) or np.isinf(val):
                raise ValueError(f"Non-finite value for '{k}': {raw}")

        values.append(val)

    arr = np.array(values, dtype=float).reshape(1, -1)
    return arr

# ----- /predict endpoint -----
@app.route("/predict", methods=["GET"])
def predict():
    """
    Fetch data from ESP32, preprocess to (1,9), call model.predict(),
    and return {"model_output": "<prediction_value>"}
    """
    try:
        # Fetch sensor JSON from ESP32 (with Basic Auth)
        resp = requests.get(ESP32_DATA_URL, auth=ESP_AUTH, timeout=5)
        resp.raise_for_status()
        esp_json = resp.json()
    except requests.RequestException as e:
        app.logger.error("Failed to fetch from ESP32: %s", e)
        return jsonify({"error": "failed_to_fetch_esp_data", "details": str(e)}), 502
    except ValueError as e:
        app.logger.error("Invalid JSON from ESP32: %s", e)
        return jsonify({"error": "invalid_esp_json", "details": str(e)}), 502

    try:
        sample = build_sample_from_esp_json(esp_json)
    except Exception as e:
        app.logger.error("Preprocessing error: %s", e)
        return jsonify({"error": "preprocessing_failed", "details": str(e)}), 400

    try:
        # Predict (model.predict expects shape (n_samples, n_features))
        pred = model.predict(sample)
        # model.predict returns array-like; grab first element and convert to native Python type
        pred_value = pred[0]
        # Convert numpy types to Python native for JSON
        if isinstance(pred_value, (np.generic, np.ndarray)):
            try:
                pred_value = pred_value.item()
            except Exception:
                pred_value = str(pred_value)
        # If the model output is bytes, decode
        if isinstance(pred_value, bytes):
            pred_value = pred_value.decode("utf-8", errors="ignore")
    except Exception as e:
        app.logger.error("Model prediction failed: %s", e)
        return jsonify({"error": "prediction_failed", "details": str(e)}), 500

    return jsonify({"model_output": pred_value})


if __name__ == "__main__":
    # Run on all interfaces or change host as needed
    app.run(host="0.0.0.0", port=5000, debug=False)
