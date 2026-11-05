#!/bin/bash
set -e
export PATH=/tmp/nomad-pio-venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
/tmp/nomad-pio-venv/bin/pip install platformio >/tmp/nomad-pio-install.log 2>&1
mkdir -p /tmp/nomad-fw-tests
tar -xf /mnt/d/Project/Nomad-Meshtastic/nomad-test-source.tar -C /tmp/nomad-fw-tests
cd /tmp/nomad-fw-tests
./bin/run-tests.sh -f "${1:-test_radio}"
