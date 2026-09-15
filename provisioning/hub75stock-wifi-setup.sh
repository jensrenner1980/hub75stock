#!/bin/sh
# One-time WiFi provisioning from a plaintext credentials file dropped onto
# the boot partition before first boot.
#
# Why this exists: lets one "generic" SD card image (no WiFi credentials
# baked in via Raspberry Pi Imager) be reused for multiple deployments -
# just drop a wificonfig.json onto the already-flashed card's boot
# partition before powering it on, no re-imaging needed per deployment.
#
# Usage: place /boot/firmware/wificonfig.json before first boot:
#   { "ssid": "MyNetwork", "passphrase": "correct horse battery staple" }
#
# On success this script hands the credentials to NetworkManager (nmcli
# device wifi connect), which creates the usual root-only-readable
# /etc/NetworkManager/system-connections/*.nmconnection profile - the
# credentials are NOT stored a second time anywhere by this script. The
# passphrase field in wificonfig.json is then blanked (ssid is kept, for
# reference), so it does not linger in plaintext on the FAT32 boot
# partition, which - unlike NetworkManager's own storage - has no Unix file
# permissions to protect it. Before that first successful run, the
# SD card's own physical security is what protects the passphrase; that is
# an inherent property of any boot-partition-based provisioning scheme, not
# a limitation specific to this script.
#
# Safe to run on every boot: once the passphrase has been scrubbed, this
# exits immediately without touching networking again.

set -eu

CONFIG_FILE="/boot/firmware/wificonfig.json"

[ -f "$CONFIG_FILE" ] || exit 0

SSID=$(python3 -c 'import json, sys
try:
    print(json.load(open(sys.argv[1])).get("ssid", ""))
except Exception:
    print("")' "$CONFIG_FILE")
PASSPHRASE=$(python3 -c 'import json, sys
try:
    print(json.load(open(sys.argv[1])).get("passphrase", ""))
except Exception:
    print("")' "$CONFIG_FILE")

# Nothing to do: already consumed on a previous boot, or the file is missing
# the fields we need.
if [ -z "$SSID" ] || [ -z "$PASSPHRASE" ]; then
    exit 0
fi

# Give the WiFi radio/NetworkManager a moment to be ready on a cold boot,
# rather than failing the one attempt that matters.
i=0
while [ "$i" -lt 15 ]; do
    if nmcli radio wifi 2>/dev/null | grep -q enabled; then
        break
    fi
    i=$((i + 1))
    sleep 1
done

# The radio reporting "enabled" doesn't mean NetworkManager has actually
# scanned and cached this specific AP yet - confirmed for real: "nmcli
# device wifi connect" failed with "No network with SSID '...' found" this
# early in boot, for an AP that appeared immediately once the same command
# was run by hand moments later (nmcli device wifi connect does not itself
# trigger a scan). Actively rescan and wait for the target SSID to actually
# show up in the AP list before attempting to connect to it, rather than
# racing a scan NetworkManager may not have gotten to yet.
i=0
while [ "$i" -lt 15 ]; do
    nmcli device wifi rescan 2>/dev/null || true
    sleep 2
    if nmcli -t -f SSID device wifi list 2>/dev/null | grep -Fxq "$SSID"; then
        break
    fi
    i=$((i + 1))
done

if nmcli device wifi connect "$SSID" password "$PASSPHRASE"; then
    python3 -c 'import json, sys
path = sys.argv[1]
with open(path) as f:
    data = json.load(f)
data["passphrase"] = ""
with open(path, "w") as f:
    json.dump(data, f, indent=2)
    f.write("\n")' "$CONFIG_FILE"
    logger -t hub75stock-wifi-setup "connected to \"$SSID\"; passphrase scrubbed from $CONFIG_FILE"
else
    logger -t hub75stock-wifi-setup "failed to connect to \"$SSID\"; $CONFIG_FILE left untouched for retry next boot"
    exit 1
fi
