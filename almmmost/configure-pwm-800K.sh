#!/bin/bash

# Script to configure PWM on NTC Chip for baud rate clock input to Z85C30

#800KHz for baud rate
NS=1250

DC=$((NS/2))

echo 0 > /sys/class/pwm/pwmchip0/export
sleep 1
echo normal > /sys/class/pwm/pwmchip0/pwm0/polarity
echo 1 > /sys/class/pwm/pwmchip0/pwm0/enable
echo $NS > /sys/class/pwm/pwmchip0/pwm0/period
echo $DC > /sys/class/pwm/pwmchip0/pwm0/duty_cycle
echo auto > /sys/class/pwm/pwmchip0/pwm0/power/control
