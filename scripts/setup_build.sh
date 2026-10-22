#!/usr/bin/env bash
# Project-local dependencies. No Homebrew or training-environment changes.
set -euo pipefail
cd "$(dirname "$0")/.."
juce_tag=8.0.16-security
juce_commit=ea77444750de45e036e0279bb0291144bf8e3688

if [[ ! -d JUCE ]]; then
  git clone --depth 1 --branch "$juce_tag" https://github.com/juce-framework/JUCE.git JUCE
fi
if [[ "$(git -C JUCE rev-parse HEAD)" != "$juce_commit" ]]; then
  echo "Expected JUCE $juce_tag ($juce_commit). Existing JUCE checkout was left unchanged." >&2
  exit 1
fi

if [[ ! -x build/tools/bin/cmake ]]; then
  "${PYTHON:-python3}" -m venv build/tools
  build/tools/bin/python -m pip install --disable-pip-version-check \
    --index-url https://pypi.org/simple cmake==3.31.10
fi
build/tools/bin/cmake --version
