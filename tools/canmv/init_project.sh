#!/bin/bash

echo "初始化 repo 工作区..."
repo init -u https://github.com/hexchip/k230_rtos_sdk_manifest.git -b main

if [ -d ".git" ]; then
    mv .git .repo/project-objects/hexchip/k230_rtos_sdk.git
fi

repo sync