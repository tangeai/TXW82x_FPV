#!/bin/bash
set -e
rm -f Obj/common_common.o
if grep -E -q "^[[:space:]]*#define[[:space:]]+CUSTOMER_ID[[:space:]]+12([[:space:]]|$)" project_config.h; then
    cp config_double_sensor.cfg config.cfg
fi
