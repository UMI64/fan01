#!/bin/bash
PROJECT_ROOT=$(cd $(dirname "$BASH_SOURCE[0]")/../; pwd)
if [ -f "${PROJECT_ROOT}/.venv/bin/activate" ]; then
    source "${PROJECT_ROOT}/.venv/bin/activate"
fi

OPENOCD_BIN=${OPENOCD_BIN:-/usr/local/openocd-esp32/bin/openocd}
OPENOCD_SCRIPTS=${OPENOCD_SCRIPTS:-/usr/local/openocd-esp32/share/openocd/scripts}
SDK_OBJDUMP=${SDK_OBJDUMP:-riscv64-zephyr-elf-objdump}
ESP_PORT=${ESP_PORT:-/dev/ttyACM0}

function build()
{
    west build -p auto -d "${PROJECT_ROOT}/build" -b esp32c3_fan01 "${PROJECT_ROOT}/project" --sysbuild -- \
        -DBOARD_ROOT="${PROJECT_ROOT}" \
        -DOPENOCD="${OPENOCD_BIN}" \
        -DOPENOCD_DEFAULT_PATH="${OPENOCD_SCRIPTS}"
}

function romp()
{
    west build -t rom_plot -d "${PROJECT_ROOT}/build/project"
}

function ramp()
{
    west build -t ram_plot -d "${PROJECT_ROOT}/build/project"
}

function dump()
{
    ${SDK_OBJDUMP} -d "${PROJECT_ROOT}/build/project/zephyr/zephyr.elf" > "${PROJECT_ROOT}/build/procpu.s"
}

function flash()
{
    local target="${1:-all}"
    local support_dir="${PROJECT_ROOT}/boards/umi/esp32c3_fan01/support"

    if [ ! -x "${OPENOCD_BIN}" ]; then
        echo "🔧 使用 esptool (${ESP_PORT}) 烧录 ${target}..."
        case "$target" in
            boot|mcuboot)
                esptool --chip esp32c3 --port "${ESP_PORT}" write-flash 0x0 "${PROJECT_ROOT}/build/mcuboot/zephyr/zephyr.bin"
                ;;
            app)
                esptool --chip esp32c3 --port "${ESP_PORT}" write-flash 0x20000 "${PROJECT_ROOT}/build/project/zephyr/zephyr.signed.bin"
                ;;
            all|"")
                esptool --chip esp32c3 --port "${ESP_PORT}" write-flash \
                    0x0 "${PROJECT_ROOT}/build/mcuboot/zephyr/zephyr.bin" \
                    0x20000 "${PROJECT_ROOT}/build/project/zephyr/zephyr.signed.bin"
                ;;
            *)
                echo "用法: flash [boot|app|all]"
                return 1
                ;;
        esac
        return $?
    fi

    case "$target" in
        boot|mcuboot)
            echo "🔧 烧录 boot 镜像..."
            "${OPENOCD_BIN}" \
                -s "${support_dir}" \
                -s "${OPENOCD_SCRIPTS}" \
                -f "${support_dir}/openocd.cfg" \
                -c 'init' \
                -c 'reset init' \
                -c "program_esp ${PROJECT_ROOT}/build/mcuboot/zephyr/zephyr.bin 0x0" \
                -c 'reset run' \
                -c shutdown
            ;;
        app)
            echo "📱 烧录 app 镜像..."
            "${OPENOCD_BIN}" \
                -s "${support_dir}" \
                -s "${OPENOCD_SCRIPTS}" \
                -f "${support_dir}/openocd.cfg" \
                -c 'init' \
                -c 'reset init' \
                -c "program_esp ${PROJECT_ROOT}/build/project/zephyr/zephyr.signed.bin 0x20000" \
                -c 'reset run' \
                -c shutdown
            ;;
        all|"")
            echo "🚀 烧录所有镜像 (boot + app)..."
            flash app
            flash boot
            ;;
        *)
            echo "错误: 无效的参数 '$target'"
            echo "用法: flash [boot|app|all]"
            return 1
            ;;
    esac
}

function gdb()
{
    "${OPENOCD_BIN}" \
        -s "${OPENOCD_SCRIPTS}" \
        -s "${PROJECT_ROOT}/boards/umi/esp32c3_fan01/support" \
        -f openocd.cfg \
        -c 'tcl_port 6333' -c 'telnet_port 4444' -c 'gdb_port 3333'
}

echo "可用命令:"
echo "  build  - 编译项目"
echo "  romp   - ROM 占用可视化"
echo "  ramp   - RAM 占用可视化"
echo "  dump   - 生成反汇编文件"
echo "  flash  - 烧录固件 [boot|app|all]"
echo "  gdb    - 启动 GDB 调试服务器"
