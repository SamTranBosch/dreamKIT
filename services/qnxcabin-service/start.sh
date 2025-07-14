#!/bin/bash
set -e

echo "Starting pre-initialization steps..."

# Look for the compiled main.pyc file (the name contains the Python version, e.g., main.cpython-39.pyc)
PYTHON_PYC=$(find __pycache__ -name "main*.pyc" | head -n 1)
if [ -z "$PYTHON_PYC" ]; then
  echo "Compiled main.pyc not found!"
  exit 1
fi

echo "Launching compiled file: $PYTHON_PYC"
exec python "$PYTHON_PYC"
