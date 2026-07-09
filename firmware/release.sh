#!/bin/bash

rm -rf release
mkdir -p release
mkdir -p release/xwr18xx
cp xwr18xx/xwr18xx_mmw_aop_demo.bin release/xwr18xx/
cp xwr18xx/xwr18xx_mmw_demo.bin release/xwr18xx/

tar -czf release.tar release
