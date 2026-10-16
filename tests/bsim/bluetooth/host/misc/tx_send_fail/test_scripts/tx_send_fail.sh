#!/usr/bin/env bash
# Copyright (c) 2026 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

source ${ZEPHYR_BASE}/tests/bsim/sh_common.source

simulation_id="${BOARD_TS}_misc_tx_send_fail"
verbosity_level=2
exe="bs_${BOARD_TS}_tests_bsim_bluetooth_host_misc_tx_send_fail_prj_conf"

cd ${BSIM_OUT_PATH}/bin

Execute ./bs_2G4_phy_v1 -v=${verbosity_level} -s=${simulation_id} \
    -D=2 -sim_length=40e6 $@

Execute ./${exe} -v=${verbosity_level} -s=${simulation_id} -d=0 -testid=central -RealEncryption=0
Execute ./${exe} -v=${verbosity_level} -s=${simulation_id} -d=1 -testid=peripheral -RealEncryption=0

wait_for_background_jobs
