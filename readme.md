# FAN01 (Zephyr RTOS)

基于 ESP32-C3 的 USB-PD 调速风扇控制器（Zephyr v4.5.0 实现，工程结构参照 `weight_probe`）。

## 环境搭建

```bash
python3 -m venv --copies .venv
source .venv/bin/activate
pip install west
west init -l project
west update
pip install -r deps/zephyr/scripts/requirements.txt
west sdk install --version 1.0.1 -t riscv64-zephyr-elf
```

## 编译与烧录

```bash
source scripts/help.sh
build
flash
```
