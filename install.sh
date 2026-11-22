#!/usr/bin/env bash
# Install the Suite bundle: Suite Control + Suite Channel Plus.
# One LV2 bundle and two CLAP plug-ins, installed together.
set -euo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
build=${1:-$here/build}
lv2_dir=${LV2_DIR:-$HOME/.lv2}
clap_dir=${CLAP_DIR:-$HOME/.clap}

[ -f "$build/lv2/suite.lv2/manifest.ttl" ] || {
  echo "install: build first (cmake --build build)" >&2
  exit 1
}

mkdir -p "$lv2_dir" "$clap_dir"

# Replace the pre-bundle per-plug-in directories, otherwise hosts see the same
# plug-in URI from two bundles.
rm -rf "$lv2_dir/suite.lv2" "$lv2_dir/suite-control.lv2" \
       "$lv2_dir/suite-channel-plus.lv2"
cp -a "$build/lv2/suite.lv2" "$lv2_dir/"

for plugin in SuiteControl SuiteChannelPlus; do
  install -m 755 "$build/clap/$plugin.clap" "$clap_dir/$plugin.clap"
  rm -rf "$clap_dir/$plugin.resources"
  cp -a "$build/clap/$plugin.resources" "$clap_dir/$plugin.resources"
done

echo "installed Suite $(cat "$here/VERSION")"
echo "  LV2:  $lv2_dir/suite.lv2"
echo "  CLAP: $clap_dir/SuiteControl.clap"
echo "  CLAP: $clap_dir/SuiteChannelPlus.clap"
