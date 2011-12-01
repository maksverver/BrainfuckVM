#!/bin/bash

if [ -f /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor ] && \
	[ $(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor) != performance ]
then
	echo 'Warning: you probably forgot to turn off CPU frequency scaling!'
fi

CMD="${*:-../bfi -O}"
echo "Benchmarking using command: $CMD"
echo

echo "Time waster"
time $CMD long.b | diff -q - long.out
echo

echo "Double self-interpreter"
time $CMD -i si-hello.in si.b | diff -q - si-hello.out
echo

echo "Welcome to Code Jam"
time $CMD -i welcome.in welcome.b | diff -q - welcome.out
echo

echo "Snapper Chain"
time $CMD -i snapper.in snapper.b | diff -q - snapper.out
echo

echo "Prime numbers"
time $CMD -b line primes.b | head -5000 | diff -q - primes.out
echo

echo "Factorials"
time $CMD -b line facto.b | head -250 | diff -q - facto.out
echo

echo "Prouhet-Thue-Morse sequence"
time $CMD -b line ptmbsg.b | head -16 | diff -q - ptmbsg.out
echo

echo "Sorting 1"
time tr -d '\0' < /dev/urandom | head -c 54321 | $CMD sort1.b >/dev/null
echo

echo "Sorting 2"
time tr -d '\0' < /dev/urandom | head -c 54321| $CMD sort2.b >/dev/null
echo
