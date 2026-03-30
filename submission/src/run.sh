#!/bin/bash
echo "------------ Fractal benchmark starting ---------------"
mkdir -p output
./fractal | tee -a terminal.out
echo "------------ Fractal benchmark done -------------------"
echo
