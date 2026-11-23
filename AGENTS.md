# AGENTS.md

本指南面向在本仓库中开展开发、调试与维护工作的 AI 编程助手。

## 1. 仓库结构

本项目基于 **Zephyr RTOS v4.5.0 + MCUboot（Sysbuild）**，运行在自研 **ESP32-C3 USB-PD 调速风扇控制板（`esp32c3_fan01`）** 上：

- `project/`：Zephyr 主应用程序源码、配置、Sysbuild 与 Web 前端
  - `project/src/`：核心业务模块（`main.c`、`pd_manager.c`、`fan_ctrl.c`、`display_manager.c`、`input_manager.c`、`wifi_manager.c`、`web_server.c`、`settings_manager.c`）
  - `project/web/index.html`：嵌入式 Web UI（CMake 构建时自动 gzip 压缩生成 `index.html.gz.inc`）
  - `project/prj.conf` / `project/boards/esp32c3_fan01.overlay`：应用 Kconfig 与板级外设覆盖（ADC、PWM、GPIO、Wi-Fi AP）
  - `project/sysbuild/`：MCUboot 配置及 bootloader 阶段外设禁用覆盖
  - `project/west.yml`：West manifest（导入 `deps/` 下 Zephyr 及相关模块）
- `boards/umi/esp32c3_fan01/`：板级定义、DTS、分区表及硬件文档
  - `esp32c3_fan01.dts` / `esp32c3_fan01-pinctrl.dtsi` / `esp32c3_fan01_partitions.dtsi`
  - `wires.md`：GPIO 引脚与采样换算说明
  - `doc/`：硬件原理图与 PCB PDF（`SCH_Schematic1_2026-11-23.pdf`、`PCB_PCB1_2026-11-23.pdf`）
- `modules/husb238/`：自定义 Zephyr `HUSB238` I2C USB-PD 诱骗驱动模块（通过 `EXTRA_ZEPHYR_MODULES` 引入）
- `scripts/help.sh`：构建、烧录、反汇编与 GDB 辅助脚本
- `code/`：早期 ESP-IDF 原型代码（仅作引脚、PID 参数及原始交互逻辑参考，非必要勿改动）
- `deps/`、`build/`、`.venv/`、`.west/`：本地依赖与构建产物（已在 `.gitignore` 忽略，**严禁提交到 Git**）

## 2. 编译与烧录规范

### 工具链与环境变量
运行 `west` 或 `esptool` 前必须激活或加入 `.venv/bin` 到 `PATH`：

```bash
source /home/coresat/projects/fan01/scripts/help.sh
```

### 常用命令
- **完整 Sysbuild 编译（首次、修改 DTS / Kconfig / 分区后）**：
  ```bash
  source scripts/help.sh && build
  # 等价于：
  # west build -p auto -d build -b esp32c3_fan01 project --sysbuild -- -DBOARD_ROOT=$PWD
  ```
- **增量编译（仅修改 C/HTML 代码时）**：
  ```bash
  PATH="/home/coresat/projects/fan01/.venv/bin:$PATH" west build -d build project
  ```
- **烧录到设备（默认 `/dev/ttyACM0`，ESP32-C3 USB Serial/JTAG）**：
  ```bash
  # 仅烧录 App（保留 MCUboot 与 NVS 分区数据）：
  PATH="/home/coresat/projects/fan01/.venv/bin:$PATH" west flash -d build --domain project --esp-device /dev/ttyACM0
  # 或使用脚本烧录 MCUboot + App：
  source scripts/help.sh && flash all
  ```

> **注意**：`west flash --domain project` 仅更新 App 分区（`0x20000`），不会擦除 NVS 中已保存的 Wi-Fi/风扇/显示配置。

### 串口调试（`/dev/ttyACM0` @ `115200`）
- 常用 Shell 验证命令：
  - `device list`：确认 `husb238@8`、`ssd1306@3c`、`wifi`、`adc0`、`ledc0` 等驱动均为 `READY`
  - `pd status` / `pd set <5|9|12|15|18|20>`：查看/请求 USB-PD 档位与 PDO 列表
  - `fan status` / `fan switch <on|off>` / `fan speed <0..8000>`：检查电压/电流/功率、开关与目标转速
  - `wmgr status` / `wmgr scan` / `wmgr add ...`：查看多 SSID 配置、扫描周边 AP、测试连接与静态 IP
  - `kernel reboot cold`：触发芯片软件冷重启
- 在 WSL/Linux (`usbipd` + `vhci_hcd`) 环境下，设备复位或冷拔插时 `/dev/ttyACM0` 会短暂断开重连，串口脚本需做好重连等待。

## 3. 硬件与固件关键约束（踩坑必读）

修改电源、I2C、显示、Wi-Fi 或启动流程前，务必遵守以下已验证的硬件事实：

1. **供电拓扑（`USB1` vs `USB2`）**：
   - `USB1` 是整板唯一电源输入（`PVBUS` -> `U1` ME3121AM6G -> `+3.3V`），`+3.3V` 挂有 `820uF` 大电容（`C2`）。
   - `USB2` 仅连接 `D+/D-`（`GPIO19/GPIO18`）与 CC 下拉电阻，**VBUS 未连接**，无法独立供电。
2. **冷上电欠压复位（BOD）与启动时序**：
   - ESP32-C3 默认开启无消抖的硬件模拟 BOD 复位且阈值较高，在 `USB1` 冷插拔及 `820uF` 电容充电阶段易触发 `RESET_REASON_SYS_BROWN_OUT (0x0F)`。
   - `project/src/main.c` 在 `PRE_KERNEL_1` 阶段注册了 `early_brownout_cfg()`（关闭 `ana_rst_en`、阈值设为最低档 `7` 即 `2.51V`、开启 `0x3ff` 周期数字消抖复位）。
   - **禁止将 `wifi_manager_init()` 提前到 PD 协商之前**：Wi-Fi 启动时的射频 TX 校准峰值电流约 350–450mA，必须在 `main.c` 等待 `pd_manager_is_settled()`（最多 800ms）完成 `5V -> 20V` 协商后再初始化 Wi-Fi 并恢复风扇供电。
3. **SSD1306 OLED（`0x3C`）无外部 RESET 引脚与烧屏防护**：
   - `esp32c3_fan01.dts` 中 `ssd1306@3c` 必须保留 `ready-time-ms = <200>`，等待 `+3.3V` 稳定与 OLED 上电复位。
   - 物理拔插 `USB1` 接触弹跳可能导致 OLED 丢失电荷泵状态（`0x8D 0x14`），`display_manager.c` 通过 `ssd1306_i2c_sync()` 在初始化、启动后约 `1.2s`、唤醒及写屏失败时自动重同步，并在 Zephyr 驱动未就绪时回退为直接 I2C 刷屏。
   - **必须使用黑底亮字正常模式（`PIXEL_FORMAT_MONO01`、`0xA6`）**，严禁全局反显白底，并保留自动调暗/熄屏逻辑以防烧屏。
4. **HUSB238 寄存器编码差异**：
   - 写入目标 PDO（`REG_SRC_PDO 0x08`）使用的编码为 `0x01/0x02/0x03/0x08/0x09/0x0A`，而读取当前合同状态（`REG_PD_STATUS0 0x00` 高 4 位）返回的编码为 `1..6`（对应 `5/9/12/15/18/20V`）。`modules/husb238/husb238.c` 中 `husb238_get_status0()` 已做转换，切勿直接把两种编码混用。
   - `pd_manager.c` 保留后台周期巡检（接入后每 `2s`、未接入 `500ms`），即使 `HUSB238` 因瞬态跌落复位回 5V 也能自动重新请求目标电压。
5. **ESP32-C3 Wi-Fi 射频与事件队列约束**：
   - `project/prj.conf` 中必须保留 `CONFIG_ESP32_PHY_MAX_TX_POWER=14`（避免过高 TX 功率导致板载射频前端/电源跌落引起握手失败）。
   - 必须保留 `CONFIG_NET_MGMT_EVENT_QUEUE_SIZE=64`，否则 Wi-Fi 扫描事件易丢包导致扫描结果为空。
   - Wi-Fi 支持最多 5 组配置（NVS `wmgr/*`），按优先级顺序尝试连接，每组支持独立 DHCP / 静态 IP（包含掩码、网关、DNS），请勿删除旧键迁移兼容逻辑。
6. **MCUboot OTA 确认**：
   - App 启动完成后须保持 `main.c` 中的 `boot_write_img_confirmed()`，否则通过 Web `/api/ota` 上传的新固件在下次重启时会触发回滚。

## 4. 代码与提交规范

- **保持接口与前端一致**：修改 HTTP API（`web_server.c`）时同步更新 `project/web/index.html`（注意保留 Web 目标转速防误触锁定交互 `rpmLocked`）。
- **严禁提交敏感信息或临时文件**：不得将个人 Wi-Fi SSID/密码、密钥、构建目录（`build/`）、依赖目录（`deps/`、`.venv/`、`.west/`）提交到版本库；提交前请用 `git status` 检查变更清单。
- **修改后必须实测验证**：
  - 所有固件修改后至少运行一次 `west build` 确认无编译错误；
  - 涉及硬件/驱动/网络/Web/UI 的改动应烧录至设备并通过 `/dev/ttyACM0` 串口或 Web 接口验证功能与日志正常。
