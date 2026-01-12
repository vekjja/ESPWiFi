#!/bin/bash

SCRIPT_DIR=$(dirname "$0")
PROJECT_DIR=$(dirname "$SCRIPT_DIR")
SDKCONFIG_DIR="$PROJECT_DIR/sdkconfigs"
SDKCONFIG_DEFAULTS="$SDKCONFIG_DIR/defaults.sdkconfig"
PROJECT_SDKCONFIG="$PROJECT_DIR/sdkconfig.defaults"

# get default environment
default_env=$( cd "$PROJECT_DIR" && pio project config --json-output | jq -r '.[] | select(.[0]=="platformio") | .[1][] | select(.[0]=="default_envs") | .[1][]')
SDKCONFIG_ENV="$SDKCONFIG_DIR/$default_env.sdkconfig"

# RM existing sdkconfig files
echo "🗑️  Removing existing sdkconfig files"
# macOS/BSD find lacks -verbose; print then delete for visibility
find "$PROJECT_DIR" -maxdepth 1 -name 'sdkconfig*' -type f -print -delete

# generate sdkconfig for default environment
cp "$SDKCONFIG_DEFAULTS" "$PROJECT_SDKCONFIG"
cat "$SDKCONFIG_ENV" >> "$PROJECT_SDKCONFIG"

echo
echo "🎉 SDK Config Generated 🎉"
echo "--------------------------------"
echo " $PROJECT_SDKCONFIG"
echo "--------------------------------"
echo