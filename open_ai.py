# gemini_raw_values.py
import os
import json
import numpy as np
import pandas as pd
from google import genai

# ---------- CONFIG ----------
# Provide API key here for quick testing OR export GEMINI_API_KEY environment variable
GENAI_API_KEY = os.getenv("GEMINI_API_KEY", "AIzaSyCl9jnVvBQoPItYEBd0sHJepSiylSWzItc")
MODEL_NAME = "gemini-2.5-flash"   # change if not available in your account

META_PATH   = "model_meta.json"

# ---------- load meta ----------
with open(META_PATH, "r") as f:
    meta = json.load(f)
FEATURE_ORDER = meta.get("features", [])
# If 'Label' was included in features, drop it safely
if "Label" in FEATURE_ORDER:
    FEATURE_ORDER = [f for f in FEATURE_ORDER if f != "Label"]
N_FEATURES = len(FEATURE_ORDER)
print("Expecting", N_FEATURES, "features:", FEATURE_ORDER)

# ---------- helpers ----------
def make_genai_client(api_key=None):
    if api_key is None:
        api_key = os.getenv("GEMINI_API_KEY")
    if not api_key:
        raise RuntimeError("No Gemini API key provided. Set GEMINI_API_KEY env var or pass api_key directly.")
    return genai.Client(api_key=api_key)

def call_gemini_with_raw(client, feature_order, raw_values, model=MODEL_NAME):
    """
    Sends raw (UNSCALED) numeric values to Gemini and requests EXACTLY one JSON object:
    {"label": "Normal" or "Anomaly", "score": 0.0-1.0}
    """
    if len(raw_values) != len(feature_order):
        raise ValueError(f"Length mismatch: got {len(raw_values)} values, expected {len(feature_order)}")

    # Build a strict prompt: emphasize values are raw (units optional)
    # You may optionally annotate units for each feature to help the model (uncomment the units line).
    units_info = ""  # e.g., "Units: Temperature_C (°C), Pressure_hPa (hPa), AngleX (deg), ... "
    prompt_text = (
        "You are a deterministic classifier. Input: a JSON object with 'feature_order' (list of names) "
        "and 'values' (list of numeric RAW/UNSCALED values in the same order). "
        "The values are raw sensor readings (do not attempt to rescale). " + units_info +
        "Output: return EXACTLY one JSON object and nothing else with keys:\n"
        '{"label":"Normal" or "Anomaly", "score": float_between_0_and_1}\n'
        "The 'score' should reflect strength of anomaly evidence (0.0 = definitely Normal, 1.0 = definitely Anomaly). "
        "If you cannot determine, return a numeric score between 0 and 1 anyway. Example: {\"label\":\"Anomaly\",\"score\":0.87}."
        "The JSON is considered anomaly if the Temprature_C > 33 or Temperature < 29 or AngleX > 10 or AngleX < -5 or AngleY > 10 or AngleY < -5"
    )

    contents = [
        {
            "parts": [
                {"text": prompt_text},
                {"text": json.dumps({"feature_order": feature_order, "values": raw_values})}
            ]
        }
    ]

    # Call generate_content (no unsupported kwargs)
    resp = client.models.generate_content(
        model=model,
        contents=contents
    )

    # Extract text: SDK versions differ; try common attributes first
    if hasattr(resp, "text") and resp.text:
        raw_text = resp.text
    else:
        # Fallback to stringifying the response object
        raw_text = str(resp)

    return raw_text

def extract_json(text):
    text = text.strip()
    # Try direct JSON parse
    try:
        return json.loads(text)
    except Exception:
        # Fallback: extract the first {...} block
        import re
        m = re.search(r"\{.*\}", text, flags=re.DOTALL)
        if m:
            return json.loads(m.group(0))
    raise ValueError("No JSON object found in model response. Raw text:\n" + text)

# ---------- MAIN ----------
if __name__ == "__main__":
    # Example RAW (UNSCALED) sensor row in same order as FEATURE_ORDER
    # Replace values below with your incoming sensor readings (must match FEATURE_ORDER length)
    raw_values = [30, 974.98, -1.14, 0.15, -5883.86, -0.012, -0.002, 0.991, 123.45]

    print("Raw values (unsclaed) provided:", raw_values)

    # Make client & call Gemini
    client = make_genai_client(GENAI_API_KEY)
    print("Calling Gemini model:", MODEL_NAME)
    try:
        raw_resp = call_gemini_with_raw(client, FEATURE_ORDER, raw_values)
        print("Raw LLM response:", raw_resp)
        parsed = extract_json(raw_resp)
        label = parsed.get("label")
        score = float(parsed.get("score", 0.0))
        print("Parsed result ->", {"label": label, "score": score})
    except Exception as e:
        print("Error calling Gemini or parsing response:", e)
