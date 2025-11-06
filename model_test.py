# Robust, modular manual inference helper
import joblib
import json
import pandas as pd
import numpy as np
from typing import List, Dict, Tuple, Any

# ---------- CONFIG: filenames (adjust if needed) ----------
RF_MODEL_FILE = "rf_model.joblib"
SCALER_FILE   = "scaler.joblib"
META_FILE     = "model_meta.json"
ENC_FILE      = "label_encoder.joblib"   # optional but recommended

# ---------- 1) Load artifacts ----------
model = joblib.load(RF_MODEL_FILE)
print("Loaded model. n_features_in_ =", model.n_features_in_)

# scaler (may be None)
scaler = None
try:
    scaler = joblib.load(SCALER_FILE)
    print("Loaded scaler.")
except Exception as e:
    print("No scaler loaded or failed:", e)

# metadata: must contain ordered feature list used for training
with open(META_FILE, "r") as f:
    meta = json.load(f)
meta_features = meta.get("features", None)
if not isinstance(meta_features, list):
    raise RuntimeError("model_meta.json must contain a 'features' list in training order")

# Clean meta features (remove accidental label/result entries)
feature_names = [f for f in meta_features if f.lower() not in ("label","result")]
print("Feature names (meta, cleaned):", feature_names)

# If mismatch, try fallback to model.feature_names_in_
if len(feature_names) != model.n_features_in_:
    if hasattr(model, "feature_names_in_"):
        feature_names = list(model.feature_names_in_)
        print("Using model.feature_names_in_ instead:", feature_names)
    else:
        raise RuntimeError(f"Feature count mismatch: meta has {len(feature_names)} but model expects {model.n_features_in_}")

# Load label encoder if present
le = None
try:
    le = joblib.load(ENC_FILE)
    print("Loaded LabelEncoder. classes_:", list(le.classes_))
except Exception as e:
    print("LabelEncoder not found or failed to load:", e)
    if hasattr(model, "classes_"):
        print("Model.classes_:", list(model.classes_))

# Utility: determine the index of the Anomaly class (if present)
from typing import Optional

def get_anomaly_index(le_obj, model_obj) -> Optional[int]:
    """Return numeric index corresponding to the 'Anomaly' class."""
    # prefer explicit encoder
    if le_obj is not None:
        classes = np.array(le_obj.classes_, dtype=object)
        match = np.where(classes == "Anomaly")[0]
        if len(match) == 1:
            return int(match[0])
        # try lowercase match
        match = np.where(np.char.lower(classes.astype(str)) == "anomaly")[0]
        if len(match) == 1:
            return int(match[0])
    # fallback to model.classes_
    if hasattr(model_obj, "classes_"):
        classes = np.array(model_obj.classes_, dtype=object)
        match = np.where(classes == "Anomaly")[0]
        if len(match) == 1:
            return int(match[0])
        match = np.where(np.char.lower(classes.astype(str)) == "anomaly")[0]
        if len(match) == 1:
            return int(match[0])
    # as last resort return None
    return None

anomaly_index = get_anomaly_index(le, model)
print("Anomaly class index (in predict_proba output):", anomaly_index)

# ---------- 2) Helper: build a DataFrame sample ----------
def build_sample_from_values(values: List[float]) -> pd.DataFrame:
    """
    Build a single-row DataFrame in the exact feature order expected by the model.
    - values: list of numbers in the same order as `feature_names`
    """
    if len(values) != len(feature_names):
        raise ValueError(f"Expected {len(feature_names)} values (got {len(values)}). Feature order: {feature_names}")
    sample = { feature_names[i]: values[i] for i in range(len(feature_names)) }
    df = pd.DataFrame([sample], columns=feature_names)
    return df

def build_sample_from_dict(sample_dict: Dict[str,Any]) -> pd.DataFrame:
    """
    Build a single-row DataFrame from a dict (keys = feature names).
    Missing features are filled with 0.0.
    """
    row = { f: sample_dict.get(f, 0.0) for f in feature_names }
    df = pd.DataFrame([row], columns=feature_names)
    return df

# ---------- 3) Helper: prepare DataFrame for model (scaler + reorder) ----------
def prepare_for_model(X_df: pd.DataFrame) -> np.ndarray:
    """
    Reindex X_df to the scaler/model expected order and apply scaler if present.
    Returns a numpy array suitable for model.predict/_proba.
    """
    # If scaler has feature_names_in_, align to it (recommended)
    if scaler is not None and hasattr(scaler, "feature_names_in_"):
        scal_feat = list(scaler.feature_names_in_)
        # add missing columns with zeros
        for f in scal_feat:
            if f not in X_df.columns:
                X_df[f] = 0.0
        X_df = X_df[scal_feat]
    else:
        # ensure columns are in model's feature order
        if hasattr(model, "feature_names_in_"):
            needed = list(model.feature_names_in_)
            for f in needed:
                if f not in X_df.columns:
                    X_df[f] = 0.0
            X_df = X_df[needed]
        else:
            # already in feature_names order, ensure columns present
            for f in feature_names:
                if f not in X_df.columns:
                    X_df[f] = 0.0
            X_df = X_df[feature_names]
    # apply scaler if available
    if scaler is not None:
        X_in = scaler.transform(X_df)
    else:
        X_in = X_df.values.astype(float)
    # final shape check
    if X_in.shape[1] != model.n_features_in_:
        raise ValueError(f"Prepared input has {X_in.shape[1]} features but model expects {model.n_features_in_}.")
    return X_in

# ---------- 4) Helper: run prediction and return useful info ----------
def predict_sample(X_df: pd.DataFrame, return_proba: bool=True) -> Dict[str,Any]:
    """
    Input: single-row DataFrame in any column order (will be aligned).
    Returns: dict with keys: pred_numeric, pred_label, confidence_pred, prob_anomaly, proba_all
    """
    X_in = prepare_for_model(X_df)
    pred = model.predict(X_in)[0]
    proba = model.predict_proba(X_in)[0]
    # predicted label (human-readable)
    if le is not None:
        pred_label = le.inverse_transform([pred])[0]
    else:
        # fallback -> model.classes_
        pred_label = str(model.classes_[pred]) if hasattr(model, "classes_") else str(pred)
    confidence_pred = float(proba[pred])
    prob_anomaly = float(proba[anomaly_index]) if anomaly_index is not None else None
    # build human-readable proba dict
    class_names = list(le.classes_) if le is not None else (list(model.classes_) if hasattr(model, "classes_") else [str(i) for i in range(len(proba))])
    proba_all = { class_names[i]: float(proba[i]) for i in range(len(proba)) }
    return {
        "pred_numeric": int(pred),
        "pred_label": pred_label,
        "confidence_pred": confidence_pred,
        "prob_anomaly": prob_anomaly,
        "proba_all": proba_all
    }

# ---------- 5) MAIN: interactive/test usage ----------
if __name__ == "__main__":
    # Example: change these values to test different inputs
    # The order must match feature_names printed above
    values = [10.27, 973.48, -1.27, -3.63, -5391.72, 0.05, -0.002, 0.99, 336.46]
    print("Using feature order:", feature_names)
    print("Values provided:", values)
    X_df = build_sample_from_values(values)
    result = predict_sample(X_df)
    print("\nPrediction result:")
    print(" Numeric prediction:", result['pred_numeric'])
    print(" Label prediction  :", result['pred_label'])
    print(f" Confidence (pred) : {result['confidence_pred']*100:.2f}%")
    if result['prob_anomaly'] is not None:
        print(f" Probability(Anomaly): {result['prob_anomaly']:.4f}")
    print(" All class probabilities:", result['proba_all'])
