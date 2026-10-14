#!/bin/bash
set -e
rm -f Obj/common_common.o
# Select the matching board map on every switch between CUSTOMER_ID 3, 4 and 12.
if grep -E -q "^[[:space:]]*#define[[:space:]]+CUSTOMER_ID[[:space:]]+3([[:space:]]|$)" project_config.h; then
    cp config_single_1080p.cfg config.cfg
elif grep -E -q "^[[:space:]]*#define[[:space:]]+CUSTOMER_ID[[:space:]]+4([[:space:]]|$)" project_config.h; then
    cp config_single_1080p.cfg config.cfg
elif grep -E -q "^[[:space:]]*#define[[:space:]]+CUSTOMER_ID[[:space:]]+12([[:space:]]|$)" project_config.h; then
    cp config_double_sensor.cfg config.cfg
fi
