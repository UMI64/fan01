# ESP32-C3 FAN01 接线说明

## I2C0 (SSD1306 OLED 128x32 + HUSB238 PD 诱骗芯片)

| 信号名 | GPIO  | 说明 |
|--------|-------|------|
| SCL    | GPIO8 | I2C 时钟 (400kHz) |
| SDA    | GPIO9 | I2C 数据 (400kHz) |

- SSD1306 地址: `0x3C`
- HUSB238 地址: `0x08`

## 风扇控制

| 信号名 | GPIO   | 说明 |
|--------|--------|------|
| POWER  | GPIO4  | 开漏输出，低电平有效 |
| TACH   | GPIO12 | 转速脉冲输入（上升沿测速，每转2脉冲） |
| PWM    | GPIO13 | LEDC CH0，1.5kHz 反相输出（DIO Flash 模式下可用） |

## ADC1 采样

| 信号名       | 通道  | GPIO  | 分压/换算 |
|--------------|-------|-------|-----------|
| 风扇电流采样 | CH0   | GPIO0 | `raw_mv * 2020 / 2500` (mA) |
| 风扇电压采样 | CH1   | GPIO1 | 预留通道 |
| 输入电压采样 | CH3   | GPIO3 | `raw_mv * 27500 / 2500` (mV) |

## 按键输入 (上拉，低电平有效)

| 按键名 | GPIO   | 映射键码 |
|--------|--------|----------|
| PRESS  | GPIO6  | `INPUT_KEY_ENTER` |
| LEFT   | GPIO7  | `INPUT_KEY_LEFT`  |
| RIGHT  | GPIO10 | `INPUT_KEY_RIGHT` |

## USB / Serial0

| 信号名 | GPIO   | 说明 |
|--------|--------|------|
| DN     | GPIO18 | USB Serial/JTAG |
| DP     | GPIO19 | USB Serial/JTAG |
| RX     | GPIO20 | UART0 Console/Shell |
| TX     | GPIO21 | UART0 Console/Shell |
