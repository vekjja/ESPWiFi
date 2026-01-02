#!/bin/bash
# Update mDNS component from upstream esp-protocols repository

set -e

echo "🔄 Updating mDNS component from upstream..."

# Navigate to project root
cd "$(dirname "$0")/.."

# Update the submodule
echo "📥 Fetching latest esp-protocols..."
git submodule update --remote components/esp-protocols

# Show the current version
cd components/esp-protocols
CURRENT_COMMIT=$(git rev-parse --short HEAD)
CURRENT_DATE=$(git log -1 --format=%cd --date=short)
echo "✅ Updated to commit: $CURRENT_COMMIT ($CURRENT_DATE)"

# Show mdns version if available
if [ -f components/mdns/idf_component.yml ]; then
    echo "📦 mDNS component version:"
    grep "version:" components/mdns/idf_component.yml || echo "   Version not specified"
fi

cd ../..

echo ""
echo "✨ Update complete! Run 'pio run -e esp32' to rebuild."
echo ""
echo "To commit the update:"
echo "  git add components/esp-protocols"
echo "  git commit -m 'Update mDNS component'"

