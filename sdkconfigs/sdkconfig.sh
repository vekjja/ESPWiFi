#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
PROJECT_DIR=$(dirname "$SCRIPT_DIR")
SDKCONFIG_DIR="$PROJECT_DIR/sdkconfigs"
SDKCONFIG_DEFAULTS="$SDKCONFIG_DIR/defaults.sdkconfig"
PROJECT_SDKCONFIG="$PROJECT_DIR/sdkconfig.defaults"

# Accept environment as first argument, or get default from platformio.ini
if [ -n "$1" ]; then
  target_env="$1"
  echo "📦 Using specified environment: $target_env"
else
  # get default environment from platformio.ini
  target_env=$( cd "$PROJECT_DIR" && pio project config --json-output | jq -r '.[] | select(.[0]=="platformio") | .[1][] | select(.[0]=="default_envs") | .[1][]')
  echo "📦 Using default environment from platformio.ini: $target_env"
fi

SDKCONFIG_ENV="$SDKCONFIG_DIR/$target_env.sdkconfig"

# CYD env uses the shared classic-ESP32 board config file
if [ ! -f "$SDKCONFIG_ENV" ] && [ "$target_env" = "cyd" ] && [ -f "$SDKCONFIG_DIR/esp32.sdkconfig" ]; then
  SDKCONFIG_ENV="$SDKCONFIG_DIR/esp32.sdkconfig"
  echo "📦 Using CYD board config: $SDKCONFIG_ENV"
fi

# Check if environment-specific config exists
if [ ! -f "$SDKCONFIG_ENV" ]; then
  echo "❌ Error: SDK config not found for environment '$target_env'"
  echo "   Expected: $SDKCONFIG_ENV"
  exit 1
fi

# RM existing sdkconfig files
echo "🗑️  Removing existing sdkconfig files"
# macOS/BSD find lacks -verbose; print then delete for visibility
find "$PROJECT_DIR" -maxdepth 1 -name '*sdkconfig*' -type f -print -delete

# generate sdkconfig for target environment
cp "$SDKCONFIG_DEFAULTS" "$PROJECT_SDKCONFIG"
# Ensure board-specific overrides start on a new line even if defaults lacks trailing newline
printf '\n' >> "$PROJECT_SDKCONFIG"
cat "$SDKCONFIG_ENV" >> "$PROJECT_SDKCONFIG"

echo
echo "🎉 SDK Config Generated 🎉"
echo "--------------------------------"
echo " Environment: $target_env"
echo " Config: $PROJECT_SDKCONFIG"
echo "--------------------------------"
echo