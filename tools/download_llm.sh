#!/bin/bash
set -e

# Qwen2.5-0.5B-Instruct converted to a LiteRT CompiledModel (.tflite) with a
# fixed 128-token context and 1280-length external KV cache (seq128_q8).
MODEL_REPO="litert-community/Qwen2.5-0.5B-Instruct"
MODEL_FILE="Qwen2.5-0.5B-Instruct_seq128_q8_ekv1280.tflite"
TOKENIZER_REPO="Qwen/Qwen2.5-0.5B-Instruct"
DEST_DIR="data/models/qwen2_5-0_5b-instruct"

mkdir -p "$DEST_DIR"

echo "Downloading Qwen2.5-0.5B-Instruct (LiteRT tflite) model to $DEST_DIR..."
echo "Note: this is a ~513MB download."

download_file() {
    local file=$1
    local url=$2
    if [ -f "$DEST_DIR/$file" ]; then
        echo " - $file already exists, skipping."
    else
        echo " - Downloading $file..."
        if command -v wget >/dev/null 2>&1; then
            wget -q --show-progress "$url" -O "$DEST_DIR/$file"
        elif command -v curl >/dev/null 2>&1; then
            curl -L "$url" -o "$DEST_DIR/$file"
        else
            echo "Error: Neither wget nor curl found."
            exit 1
        fi
    fi
}

download_file "model.tflite" "https://huggingface.co/$MODEL_REPO/resolve/main/$MODEL_FILE"
download_file "tokenizer.json" "https://huggingface.co/$TOKENIZER_REPO/resolve/main/tokenizer.json"

echo "Download complete."
