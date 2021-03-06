#!/bin/bash

CONF=rdc.conf

cat >$CONF <<EOF
load name=rdc plugin=rdc_sampler
config name=rdc metrics=RDC_FI_GPU_CLOCK,RDC_FI_GPU_TEMP,RDC_FI_POWER_USAGE,RDC_FI_GPU_MEMORY_USAGE
config name=rdc update_freq=1000000 max_keep_age=60 max_keep_samples=10
smplr_add name=smp instance=rdc
smplr_start name=smp interval=1000000 offset=0
EOF
