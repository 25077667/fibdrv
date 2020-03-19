#!/bin/bash

make unload
make load

# save all origin setting
aslr=`cat /proc/sys/kernel/randomize_va_space`
scaling=`cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor`
turbo=`cat /sys/devices/system/cpu/intel_pstate/no_turbo`

# Do my setting
sudo sh -c "echo 0 > /proc/sys/kernel/randomize_va_space"
for i in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
do
    echo performance > ${i}
done
sudo sh -c "echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo"

# run
sudo taskset 0x1 ./client > out

sudo sh -c "echo $turbo > /sys/devices/system/cpu/intel_pstate/no_turbo"
sudo sh -c "echo $scaling > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
sudo sh -c "echo $aslr >  /proc/sys/kernel/randomize_va_space"

make unload
