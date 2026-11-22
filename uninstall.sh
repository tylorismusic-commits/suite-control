#!/usr/bin/env bash
# Remove the Suite bundle from the per-user plug-in directories.
set -euo pipefail
lv2_dir=${LV2_DIR:-$HOME/.lv2}
clap_dir=${CLAP_DIR:-$HOME/.clap}

rm -rf "$lv2_dir/suite.lv2" "$lv2_dir/suite-control.lv2" \
       "$lv2_dir/suite-channel-plus.lv2"
for plugin in SuiteControl SuiteChannelPlus; do
  rm -f "$clap_dir/$plugin.clap"
  rm -rf "$clap_dir/$plugin.resources"
done
echo "removed the Suite bundle"
