#!/bin/bash

rm -rf release
mkdir -p release
mkdir -p release/xwr18xx-custom
cp xwr18xx-custom/xwr18xx_mmw_aop_demo.bin release/xwr18xx-custom/
cp xwr18xx-custom/xwr18xx_mmw_demo.bin release/xwr18xx-custom/

tar -czf release.tar release
