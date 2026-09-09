# STM32 DroneCAN → PCA9685 Bridge

Прошивка для STM32F103C8T6, которая принимает стандартные команды сервоприводов ArduPilot по DroneCAN и управляет 16-канальным PWM-драйвером PCA9685 по I2C.

Проект предназначен для использования как CAN-периферия ArduPilot Rover.

## Архитектура

```text
ArduPilot Rover / Orange Pi H5
        |
        | DroneCAN
        |
      CAN bus
        |
     MCP2562
        |
 STM32F103C8T6
    Blue Pill
        |
        | I2C
        |
     PCA9685
        |
   16 PWM outputs
```

STM32 не выполняет основную логику управления ровером.

Основная логика остаётся в ArduPilot:

```text
ArduPilot / Lua
      |
      | servo outputs
      v
DroneCAN ArrayCommand
      |
      v
STM32F103
      |
      v
PCA9685
```

STM32 является исполнительным CAN-узлом и обеспечивает дополнительный локальный failsafe.

---

# Текущее состояние проекта

Реализовано и проверено на реальном железе:

- STM32F103C8T6, 72 MHz;
- bxCAN, DroneCAN через `libcanard`, CAN 500 kbit/s;
- `NodeStatus`, `GetNodeInfo`, Parameter `GetSet` и `ExecuteOpcode`;
- `uavcan.equipment.actuator.ArrayCommand`, входной тип `PWM`;
- до 32 логических выходов через PCA9685 #0 `0x40` и #1 `0x41`;
- `PCA_COUNT=1..2` (на текущем макете физически проверен PCA0);
- I2C2 PB10/PB11, 100 kHz, восстановление I2C после ошибок;
- SSD1306 128x32 `0x3C`;
- режимы `DISABLED`, `PWM`, `ON_OFF`, `PULSE`;
- PULSE `IGNORE` и `RESTART`;
- локальный failsafe;
- конфигурация во Flash с CRC32, явные SAVE/ERASE;
- исправленный 16-битный TIM2 timebase;
- аппаратные regression-тесты в `tests/`.

---

# Аппаратная часть

## STM32

Используется:

```text
STM32F103C8T6
ARM Cortex-M3
Blue Pill
```

## CAN

STM32F103 имеет встроенный CAN-контроллер bxCAN.

Внешний MCP2515 для STM32 не требуется.

Необходим только CAN-трансивер, например:

```text
MCP2562
```

Подключение:

```text
STM32F103           MCP2562

PA12 CAN_TX   --->  TXD
PA11 CAN_RX   <---  RXD

                    CANH ---> CANH шины
                    CANL ---> CANL шины

GND           ----- GND
```

Для MCP2562 с выводом VIO:

```text
VDD = 5 V
VIO = 3.3 V
```

CAN-шина проекта ArduPilot работает на:

```text
500000 bit/s
```

---

# PCA9685

PCA9685 и OLED подключаются к I2C2 STM32.

```text
STM32F103        PCA9685

PB10 ---------> SCL
PB11 <--------> SDA
GND  ---------- GND
```

Используется адрес:

```text
0x40
```

I2C работает на:

```text
100 kHz
```

SDA и SCL должны иметь pull-up резисторы к 3.3 V.

Типичное значение:

```text
4.7 kOhm
```

Необходимо проверить конкретный модуль PCA9685: некоторые платы уже содержат pull-up резисторы.

Логическое питание PCA9685 рекомендуется делать 3.3 V.

Питание сервоприводов/ESC через вывод `V+` PCA9685 является отдельным от логического питания.

---

# Соответствие DroneCAN каналов PCA9685

```text
actuator_id 1  -> PCA9685 CH0
actuator_id 2  -> PCA9685 CH1
actuator_id 3  -> PCA9685 CH2
...
actuator_id 16 -> PCA9685 CH15
```

Принимается стандартное сообщение:

```text
uavcan.equipment.actuator.ArrayCommand
```

Прошивка обрабатывает команды:

```text
COMMAND_TYPE_PWM
```

Значение `command_value` рассматривается как PWM в микросекундах.

Например:

```text
1000 us
1500 us
2000 us
```

---

# PWM PCA9685

Частота:

```text
50 Hz
```

Период:

```text
20000 us
```

Преобразование PWM в значение счётчика PCA9685:

```text
count = pulse_us * 4096 / 20000
```

Примерные значения:

```text
1000 us -> 205
1500 us -> 307
2000 us -> 410
```

В текущей версии входной PWM ограничивается диапазоном:

```text
500 ... 2500 us
```

---

# Локальный failsafe

STM32 самостоятельно контролирует поступление actuator-команд.

Если валидные DroneCAN PWM-команды отсутствуют примерно:

```text
300 ms
```

активируется локальный failsafe.

В текущей версии безопасное значение:

```text
1500 us
```

устанавливается для выходов PCA9685.

В дальнейшем безопасные значения можно сделать индивидуальными для каждого из 16 каналов.

Локальный failsafe работает независимо от failsafe ArduPilot.

Таким образом:

```text
ArduPilot failsafe
       +
STM32 local failsafe
```

образуют два уровня защиты.

---

# LED-индикация

Используется штатный LED Blue Pill:

```text
PC13
```

LED active-low.

Текущая индикация:

```text
короткая вспышка раз в секунду
    NORMAL

2 вспышки
    PCA9685 / I2C ERROR

3 вспышки
    CAN INIT ERROR

быстрое мигание ~5 Hz
    ACTUATOR FAILSAFE

постоянно горит
    FATAL ERROR
```

Индикация предназначена не только для разработки, но и для диагностики узла непосредственно на ровере.

---

# Структура репозитория

```text
stm32_dronecan_pca9685/
├── .gitmodules
├── .gitignore
├── Makefile
├── STM32F103C8T6.ld
│
├── src/
│   ├── main.c
│   ├── startup_stm32f103.s
│   ├── system.c
│   ├── timebase.c
│   ├── minilibc.c
│   ├── platform.c
│   ├── can_hw.c
│   ├── dronecan.c
│   ├── i2c.c
│   ├── pca9685.c
│   ├── status.c
│   └── status.h
│
├── dsdl_generated/
│
├── libcanard/
├── DSDL/
└── dronecan_dsdlc/
```

---

# Внешние зависимости Git

Три внешних проекта подключены как Git submodules:

```text
libcanard
DSDL
dronecan_dsdlc
```

На момент создания первой рабочей версии использовались:

```text
DSDL
b4653c7abc3c47cb31b16efa24ea755232774756

dronecan_dsdlc
431170fa4bfe2212b516b8f33bdc796267907f1c

libcanard
601ed35467e0ac38819df17cd7c918de19f62d58
```

Git автоматически сохраняет эти конкретные ревизии в основном репозитории.

---

# Как забрать проект на другой компьютер

Рекомендуемый способ:

```bash
git clone --recurse-submodules \
    https://github.com/artyrn/stm32_dronecan_pca9685.git
```

После этого:

```bash
cd stm32_dronecan_pca9685
```

Проверить submodules:

```bash
git submodule status
```

Должны присутствовать:

```text
DSDL
dronecan_dsdlc
libcanard
```

---

# Если проект уже был клонирован без submodules

Если был выполнен обычный:

```bash
git clone https://github.com/artyrn/stm32_dronecan_pca9685.git
```

то зависимости можно получить позже:

```bash
cd stm32_dronecan_pca9685

git submodule update --init --recursive
```

Проверка:

```bash
git submodule status
```

---

# Обновление существующей копии проекта

Получить изменения основного репозитория:

```bash
git pull
```

После `git pull` желательно выполнить:

```bash
git submodule update --init --recursive
```

Это гарантирует, что `libcanard`, `DSDL` и `dronecan_dsdlc` переключены именно на те версии, которые указаны текущим коммитом проекта.

---

# Что необходимо установить для компиляции

Проект собирается обычным GNU ARM Embedded toolchain.

Не требуется:

- Arduino;
- PlatformIO;
- STM32CubeIDE;
- HAL STM32Cube;
- операционная система на STM32.

Используется bare-metal код и драйвер bxCAN из libcanard.

Основные необходимые программы:

```text
git
make
python3
arm-none-eabi-gcc
arm-none-eabi-objcopy
arm-none-eabi-size
```

Для прошивки позже потребуется:

```text
OpenOCD
ST-Link V2
```

---

# Ubuntu / Debian

Установить базовые инструменты:

```bash
sudo apt update

sudo apt install \
    git \
    make \
    python3 \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    openocd
```

Проверить компилятор:

```bash
arm-none-eabi-gcc --version
```

Проверить Make:

```bash
make --version
```

Проверить Python:

```bash
python3 --version
```

---

# Используемый компилятор

Разработка первоначально выполнялась с:

```text
arm-none-eabi-gcc 10.2.1
```

из GNU Arm Embedded Toolchain:

```text
gcc-arm-none-eabi-10-2020-q4-major
```

Более новые версии `arm-none-eabi-gcc` также должны собирать проект, но желательно проверять предупреждения и размер прошивки.

В проекте включены:

```text
-Wall
-Wextra
-Werror
```

Поэтому предупреждения компилятора считаются ошибками.

---

# Почему используется -nostdlib

Прошивка собирается без стандартной runtime-среды libc:

```text
-nostartfiles
-nostdlib
```

Минимально необходимые функции реализованы внутри проекта.

Например:

```text
memset()
memcpy()
usleep()
```

Однако `libcanard` использует операции с `float`, поэтому при линковке подключается:

```text
-lgcc
```

`libgcc` предоставляет программные ARM runtime-функции, например:

```text
__aeabi_fmul
__aeabi_fcmpge
```

---

# Генерация DroneCAN DSDL

Сгенерированные файлы уже находятся в:

```text
dsdl_generated/
```

Поэтому для обычной сборки повторная генерация не требуется.

Если DSDL были изменены или требуется пересоздать generated-код:

```bash
python3 dronecan_dsdlc/dronecan_dsdlc.py \
    -O dsdl_generated \
    DSDL/uavcan \
    DSDL/dronecan \
    DSDL/com \
    DSDL/ardupilot
```

После этого должны появиться, среди прочих:

```text
dsdl_generated/include/uavcan.protocol.NodeStatus.h
dsdl_generated/include/uavcan.protocol.GetNodeInfo_res.h
dsdl_generated/include/uavcan.equipment.actuator.ArrayCommand.h
```

и соответствующие `.c` файлы в:

```text
dsdl_generated/src/
```

---

# Сборка

Из корня проекта:

```bash
make
```

Для полной пересборки:

```bash
make clean
make
```

После успешной сборки создаются:

```text
build/stm32_dronecan_pca9685.elf
build/stm32_dronecan_pca9685.bin
build/stm32_dronecan_pca9685.map
```

Проверить размер:

```bash
arm-none-eabi-size build/stm32_dronecan_pca9685.elf
```

На текущем этапе размер прошивки примерно:

```text
text    8912
data       0
bss     1124
```

STM32F103C8T6 имеет:

```text
FLASH 64 KB
RAM   20 KB
```

поэтому запас памяти остаётся большим.

---

# Тактирование STM32

Используется внешний кварц:

```text
HSE = 8 MHz
```

PLL:

```text
8 MHz x 9 = 72 MHz
```

Итоговые частоты:

```text
SYSCLK = 72 MHz
AHB    = 72 MHz
APB2   = 72 MHz
APB1   = 36 MHz
```

bxCAN работает от APB1:

```text
CAN clock = 36 MHz
```

TIM2 получает:

```text
72 MHz
```

и настроен на счёт:

```text
1 MHz
```

то есть:

```text
1 count = 1 us
```

---

# DroneCAN Node ID

На текущем этапе node ID задан в прошивке:

```text
42
```

Узел периодически отправляет:

```text
uavcan.protocol.NodeStatus
```

и отвечает на:

```text
uavcan.protocol.GetNodeInfo
```

Имя узла:

```text
com.artyrn.pca9685
```

---

# STM32 Unique ID

В `GetNodeInfo` используется заводской 96-битный Unique Device ID STM32F103.

Адрес UID STM32F103:

```text
0x1FFFF7E8
```

Он используется для формирования уникального hardware ID DroneCAN-узла.

---

# Настройка ArduPilot

На стороне ArduPilot должен быть включён DroneCAN.

Основная схема:

```text
CAN_P1_DRIVER = 1
CAN_D1_PROTOCOL = 1
```

Также необходимо разрешить передачу servo outputs через DroneCAN.

Прошивка STM32 ожидает именно:

```text
uavcan.equipment.actuator.ArrayCommand
```

с командами:

```text
COMMAND_TYPE_PWM
```

То есть ArduPilot должен быть настроен на передачу actuator-команд в режиме PWM, а не UNIT_LESS.

Конкретные параметры ArduPilot и bitmask каналов необходимо настраивать под конкретную конфигурацию Rover.

---

# Соответствие каналов ArduPilot

Пример:

```text
Servo output 1  -> DroneCAN actuator_id 1  -> PCA9685 CH0
Servo output 2  -> DroneCAN actuator_id 2  -> PCA9685 CH1
Servo output 3  -> DroneCAN actuator_id 3  -> PCA9685 CH2
...
Servo output 16 -> DroneCAN actuator_id 16 -> PCA9685 CH15
```

DroneCAN `ArrayCommand` содержит максимум 15 actuator-команд в одном сообщении.

Поэтому 16-й выход может передаваться отдельным сообщением.

Прошивка STM32 обрабатывает каждый `actuator_id` независимо.

---

# Прошивка через ST-Link

После появления ST-Link V2 прошивку можно будет выполнять через SWD.

Типичное подключение:

```text
ST-Link          STM32F103

SWDIO   -------> PA13 / SWDIO
SWCLK   -------> PA14 / SWCLK
GND     -------- GND
3.3V    -------- 3.3V reference
```

Пример OpenOCD:

```bash
openocd \
    -f interface/stlink.cfg \
    -f target/stm32f1x.cfg \
    -c "program build/stm32_dronecan_pca9685.elf verify reset exit"
```

После появления реального ST-Link команда будет проверена на используемом адаптере.

---

# Git workflow

Текущая основная ветка:

```text
main
```

Первый рабочий коммит:

```text
04c7998 Initial DroneCAN PCA9685 STM32 bridge
```

Проверить состояние:

```bash
git status
```

Проверить submodules:

```bash
git submodule status
```

Добавить изменения:

```bash
git add .
```

Создать коммит:

```bash
git commit -m "Описание изменений"
```

Отправить на GitHub:

```bash
git push
```

---

# Важное замечание о submodules

Не следует вручную заменять содержимое:

```text
libcanard/
DSDL/
dronecan_dsdlc/
```

если изменение этих зависимостей не является осознанным.

Основной Git-репозиторий хранит конкретный commit каждого submodule.

Если submodule был случайно переключён:

```bash
git submodule update --init --recursive
```

вернёт его к версии, сохранённой в основном проекте.

---

# План дальнейшей разработки

Следующие этапы:

- автоматическое восстановление PCA9685 после runtime I2C error;
- улучшение приоритетов диагностических состояний;
- передача состояния failsafe и ошибок через DroneCAN NodeStatus;
- индивидуальные safe PWM для 16 каналов;
- аппаратная проверка STM32F103 + MCP2562;
- аппаратная проверка PCA9685;
- тест DroneCAN с ArduPilot Rover;
- тест потери CAN;
- тест отключения PCA9685;
- тест длительной работы;
- проверка CAN BUS-OFF и восстановления;
- параметризация node ID и других настроек;
- проверка работы с реальными ESC и исполнительными устройствами Rover.

---

# Репозиторий

```text
https://github.com/artyrn/stm32_dronecan_pca9685
```

Для полного клонирования вместе со всеми зависимостями:

```bash
git clone --recurse-submodules \
    https://github.com/artyrn/stm32_dronecan_pca9685.git
```

После клонирования:

```bash
cd stm32_dronecan_pca9685

make clean
make
```\n\n---\n\n# Актуальная конфигурация выходов и параметры\n\nКаждый выход имеет тип:\n\n```text\n0 = DISABLED\n1 = PWM\n2 = ON_OFF\n3 = PULSE\n```\n\nДля `ON_OFF` и `PULSE` вход всё равно принимается как DroneCAN `COMMAND_TYPE_PWM`. Логический порог фиксирован: `<1500 us = OFF`, `>=1500 us = ON`.\n\nПараметры каждого активного выхода:\n\n```text\nOUT01_TYPE\nOUT01_ON\nOUT01_OFF\nOUT01_FS\nOUT01_FS_STATE\nOUT01_TIME\nOUT01_RETRIG\n```\n\nАналогично до `OUT32`. При `PCA_COUNT=1` публикуются OUT01..OUT16, при `PCA_COUNT=2` — OUT01..OUT32.\n\nДиапазоны и defaults:\n\n| Поле | Диапазон | Default |\n|---|---:|---:|\n| TYPE | 0..3 | 1 |\n| ON | 500..2500 us | 2000 |\n| OFF | 500..2500 us | 1000 |\n| FS | 500..2500 us | 1500 |\n| FS_STATE | 0..1 | 0 |\n| TIME | 1..60000 ms | 1000 |\n| RETRIG | 0..1 | 0 |\n\nСистемные параметры:\n\n```text\nNODE_ID       default 42\nCAN_BITRATE   default 500000\nPCA_COUNT     default 1\nFS_TIMEOUT    default 300 ms\n```\n\nВ аппаратных тестах текущая сохранённая конфигурация использовала `FS_TIMEOUT=700 ms`. Поддерживаемые значения CAN_BITRATE в конфигурации: 125000, 250000, 500000 и 1000000. Перед эксплуатационным изменением скорости необходимо проверить, что текущий CAN startup применяет сохранённый `CAN_BITRATE`.\n\n## PULSE\n\n`OUTxx_RETRIG=0` (`IGNORE`): OFF→ON запускает импульс. Повторные ON игнорируются; после завершения нужен OFF для re-arm.\n\n`OUTxx_RETRIG=1` (`RESTART`): каждый ON во время активного импульса перезапускает таймер. Непрерывный ON удерживает выход активным. Обычный OFF не обрывает уже запущенный импульс.\n\n## Failsafe\n\nДля PWM при failsafe используется `OUTxx_FS`. Для ON_OFF/PULSE используется `OUTxx_FS_STATE`: 0 выбирает `OUTxx_OFF`, 1 — `OUTxx_ON`. Failsafe имеет приоритет над PULSE и отменяет активный импульс.\n\nТекущий failsafe общий для узла: принятая actuator-команда обновляет общий таймер последней команды.\n\n# Flash configuration\n\nПоследняя 1 KB страница Flash STM32F103C8 зарезервирована по адресу:\n\n```text\n0x0800FC00\n```\n\nLinker script оставляет firmware 63 KB Flash. Конфигурация защищена magic/version/CRC32. Изменение параметров через `GetSet` работает в RAM и **не сохраняется автоматически**.\n\nСтандартный `uavcan.protocol.param.ExecuteOpcode`:\n\n```text\nSAVE  = 0\nERASE = 1\n```\n\nSAVE и восстановление после полного отключения питания проверены аппаратно. Текущая схема одно-страничная и не гарантирует power-fail-safe непосредственно во время erase/program; при необходимости можно перейти на двухстраничный journal.\n\n# OLED и LED\n\nSSD1306 128x32 имеет адрес `0x3C`. Экран показывает CAN/FS, состояние PCA0/PCA1, RX count и CAN diagnostics (TEC/REC/overflow/flags). При `PCA_COUNT=1` второй драйвер отображается как `P1 --`. Отказ OLED не должен останавливать PCA9685.\n\nДиагностические LED текущей платы:\n\n```text\nPB4  LED1 RUN\nPB3  LED2 FAILSAFE\nPA15 LED3 ERROR\n```\n\nОни active-high. JTAG отключается с сохранением SWD, чтобы освободить PB3/PB4; SWD остаётся на PA13/PA14.\n\n# Исправление TIM2\n\nTIM2 используется как 16-битный таймер 1 MHz с `ARR=0xFFFF`. Update IRQ увеличивает программную старшую часть счётчика; на этой основе реализованы `micros32()`/`micros64()`. Это устраняет прежнее переполнение каждые 65.536 ms, которое ломало failsafe и остальные интервалы.\n\n# Тесты\n\nАппаратные тесты находятся в `tests/`:\n\n```text\ntest_params.py\ntest_params_read_outputs.py\ntest_param_write_output.py\ntest_onoff.py\ntest_pulse.py\ntest_pulse_restart.py\ntest_failsafe_output.py\ntest_pulse_failsafe.py\n```\n\nПроверено на реальном железе:\n\n```text\n[OK] CAN 500 kbit/s\n[OK] NodeStatus / GetNodeInfo\n[OK] Parameter GetSet\n[OK] Parameter SAVE\n[OK] Flash restore after cold power cycle\n[OK] PCA_COUNT=1 / PCA0 0x40\n[OK] OLED 0x3C\n[OK] I2C2 PB10/PB11\n[OK] corrected TIM2 timebase\n[OK] PWM\n[OK] ON_OFF\n[OK] PULSE IGNORE\n[OK] PULSE RESTART\n[OK] ON_OFF failsafe FS_STATE=0/1\n[OK] PULSE interrupted by failsafe\n```\n\nВ тестах ON_OFF с 50 Hz PCA9685 измерено примерно `1000 us -> 0.16 V`, `2000 us -> 0.32 V`. PULSE IGNORE/RESTART и failsafe с `FS_TIMEOUT=700 ms` отработали согласно заданной логике.\n\nВторой физический PCA9685 (`0x41`, OUT17..OUT32) программно предусмотрен, но пока не отмечается как аппаратно проверенный.\n\n# SocketCAN для тестов\n\n```bash\nsudo ip link set can0 down\nsudo ip link set can0 type can bitrate 500000\nsudo ip link set can0 up\ncandump can0\n```\n\nДля используемого USB-CAN `restart-ms` не требуется и может не поддерживаться. В корне репозитория находится `can0.sh`.\n\n# Следующие проверки\n\n- физический PCA9685 #1 при `PCA_COUNT=2`;\n- применение сохранённого `CAN_BITRATE` при CAN startup;\n- recovery при ошибочно сохранённом Node ID/bitrate;\n- CAN BUS-OFF/recovery и длительный stress test;\n- интеграция всех требуемых выходов с ArduPilot Rover;\n- при необходимости двухстраничное power-fail-safe хранение конфигурации.\n