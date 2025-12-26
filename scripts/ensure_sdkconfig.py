#!/usr/bin/env python3
"""
PlatformIO pre-build script to ensure sdkconfig is properly loaded.
This script ensures the sdkconfig file from board_build.sdkconfig is copied
to the build directory so it's properly used during the build process.
"""

import os
from pathlib import Path

Import("env")


def ensure_sdkconfig():
    """Ensure sdkconfig file is properly set up for the build."""
    project_dir = Path(env.get("PROJECT_DIR"))
    build_dir = Path(env.get("BUILD_DIR"))

    # Get the sdkconfig path from board config
    try:
        sdkconfig_source = env.BoardConfig().get("build.sdkconfig", "")
        if sdkconfig_source:
            # Resolve the source path (relative to project directory)
            if not os.path.isabs(sdkconfig_source):
                sdkconfig_source_path = project_dir / sdkconfig_source
            else:
                sdkconfig_source_path = Path(sdkconfig_source)

            # Destination is the build directory
            sdkconfig_dest_path = build_dir / "sdkconfig"

            # Create build directory if it doesn't exist
            build_dir.mkdir(parents=True, exist_ok=True)

            # Copy sdkconfig if source exists
            if sdkconfig_source_path.exists():
                # Read the source content
                with open(sdkconfig_source_path, "r") as f:
                    content = f.read()

                # Write to destination (merge with existing if present)
                existing_content = ""
                if sdkconfig_dest_path.exists():
                    with open(sdkconfig_dest_path, "r") as f:
                        existing_content = f.read()

                # Write merged content (source takes precedence)
                with open(sdkconfig_dest_path, "w") as f:
                    # Write source content first
                    f.write(content)
                    # Append any additional lines from existing that aren't in source
                    if existing_content:
                        existing_lines = set(existing_content.splitlines())
                        source_lines = set(content.splitlines())
                        additional_lines = existing_lines - source_lines
                        if additional_lines:
                            f.write("\n")
                            f.write("\n".join(additional_lines))
                            f.write("\n")

                print(
                    f"✓ SDK config copied: {sdkconfig_source} -> {sdkconfig_dest_path}"
                )
            else:
                print(
                    f"⚠️  SDK config source not found: {sdkconfig_source_path}"
                )
    except Exception as e:
        print(f"⚠️  Could not ensure sdkconfig: {e}")
        # Don't fail the build if this fails, just warn


# Run when script is imported by PlatformIO
ensure_sdkconfig()
