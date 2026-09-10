# DroneCAN Output Controller

CAN-периферия для **ArduPilot Rover** на STM32F103C8T6 с выходами через
PCA9685.

Контроллер принимает стандартные DroneCAN-команды
`uavcan.equipment.actuator.ArrayCommand` и преобразует их в локальные
выходы `PWM`, `ON_OFF` и `PULSE`. Конфигурация выполняется через
стандартный DroneCAN Parameter Protocol. Для настройки разработан
отдельный графический **DroneCAN Output Controller Configurator**.

Основная логика управления остаётся в ArduPilot/Lua. STM32 является
исполнительным CAN-узлом и дополнительно обеспечивает локальный
failsafe.

``` text
ArduPilot Rover
      |
      | DroneCAN / ArrayCommand
      v
    CAN bus
      |
      v
STM32F103C8T6
      |
      | I2C2 PB10/PB11
      +--------------------+
      |                    |
      v                    v
PCA9685 #0             PCA9685 #1
0x40                   0x41
OUT01..OUT16           OUT17..OUT32
      |
      +---- SSD1306 128x32, 0x3C
```

## Текущее состояние

На реальном контроллере проверены:

-   STM32F103C8T6, 72 MHz;
-   bxCAN + `libcanard`;
-   CAN 500 kbit/s;
-   `NodeStatus`;
-   `GetNodeInfo`;
-   `uavcan.protocol.param.GetSet`;
-   `uavcan.protocol.param.ExecuteOpcode`;
-   `uavcan.equipment.actuator.ArrayCommand`;
-   до 32 логических выходов;
-   PCA9685 #0 `0x40`;
-   поддержка PCA9685 #1 `0x41`;
-   `PCA_COUNT=1..2`;
-   I2C2 PB10/PB11, 100 kHz;
-   восстановление I2C после ошибок;
-   OLED SSD1306 128x32 `0x3C`;
-   режимы `DISABLED`, `PWM`, `ON_OFF`, `PULSE`;
-   PULSE `IGNORE` и `RESTART`;
-   индивидуальный failsafe выходов;
-   конфигурация во Flash с CRC32;
-   явные `SAVE` и `ERASE`;
-   исправленный 16-битный TIM2 timebase;
-   PySide6-конфигуратор;
-   чтение/запись параметров из GUI;
-   выборочная запись только изменённых параметров;
-   сохранение/загрузка конфигурации в файл;
-   индикация GUI → RAM и RAM → Flash;
-   автоматическое чтение узла после `ERASE`;
-   аппаратные regression-тесты.

На текущем макете физически установлен и проверен один PCA9685.
Поддержка второго PCA9685 реализована программно, физическая проверка
будет выполнена на новой плате.

## Аппаратная часть

### STM32F103C8T6

Основные сигналы:

``` text
PA11  CAN_RX
PA12  CAN_TX

PB10  I2C2_SCL
PB11  I2C2_SDA

PA13  SWDIO
PA14  SWCLK

PB4   LED1 RUN
PB3   LED2 FAILSAFE
PA15  LED3 ERROR
```

JTAG отключается, SWD остаётся доступным.

### CAN

Используется встроенный bxCAN STM32F103 и внешний CAN-трансивер
MCP2562/MCP2562FD.

``` text
STM32              MCP2562FD

PA12 CAN_TX  ----> TXD
PA11 CAN_RX  <---- RXD

3.3 V        ----> VIO
5 V          ----> VDD
GND          ----- GND
```

`STBY` трансивера должен иметь определённое состояние. Для постоянной
работы CAN его необходимо удерживать в LOW.

Номинальная скорость CAN:

``` text
500000 bit/s
```

Терминатор 120 Ом устанавливается только на физическом конце CAN-шины.

## I2C2

PCA9685 и OLED работают на одном I2C2:

``` text
PB10 = SCL
PB11 = SDA
```

Скорость:

``` text
100 kHz
```

Адреса:

``` text
0x40  PCA9685 #0
0x41  PCA9685 #1
0x3C  SSD1306 128x32
```

Прошивка содержит восстановление I2C: освобождение линий, импульсы SCL и
повторную инициализацию после ошибок шины.

## Выходы

Соответствие DroneCAN actuator ID:

``` text
actuator_id  1 -> PCA0 CH0
...
actuator_id 16 -> PCA0 CH15

actuator_id 17 -> PCA1 CH0
...
actuator_id 32 -> PCA1 CH15
```

Количество доступных выходов определяется параметром `PCA_COUNT`.

### Типы выходов

``` text
0 = DISABLED
1 = PWM
2 = ON_OFF
3 = PULSE
```

Для каждого выхода имеются параметры:

``` text
OUT01_TYPE
OUT01_ON
OUT01_OFF
OUT01_FS
OUT01_FS_STATE
OUT01_TIME
OUT01_RETRIG
```

Аналогично для `OUT02` ... `OUT32`.

### PWM

Входной DroneCAN command type для всех активных типов выхода --- `PWM`.

Диапазон:

``` text
500 ... 2500 us
```

PCA9685 работает на 50 Hz.

Для `PWM` значение DroneCAN передаётся непосредственно как ширина
импульса.

### ON_OFF

Логическое состояние определяется входным PWM:

``` text
PWM < 1500 us   -> OFF
PWM >= 1500 us  -> ON
```

Физические значения задаются:

``` text
OUTxx_OFF
OUTxx_ON
```

### PULSE

Переход `OFF -> ON` запускает импульс длительностью:

``` text
OUTxx_TIME
```

Режим:

``` text
OUTxx_RETRIG = 0  IGNORE
OUTxx_RETRIG = 1  RESTART
```

`IGNORE` не позволяет повторным ON продлевать импульс. После завершения
требуется получить OFF перед новым запуском.

`RESTART` перезапускает таймер при повторных ON во время активного
импульса.

## Failsafe

Глобальный таймаут:

``` text
FS_TIMEOUT
```

Значение по умолчанию:

``` text
300 ms
```

Для `PWM` при failsafe используется:

``` text
OUTxx_FS
```

Для `ON_OFF` и `PULSE`:

``` text
OUTxx_FS_STATE
```

где `0` выбирает `OUTxx_OFF`, а `1` --- `OUTxx_ON`.

Failsafe имеет приоритет над PULSE и немедленно отменяет активный pulse
timer.

## Системные DroneCAN-параметры

``` text
NODE_ID
CAN_BITRATE
PCA_COUNT
FS_TIMEOUT
```

Текущие значения по умолчанию:

``` text
NODE_ID      = 42
CAN_BITRATE  = 500000
PCA_COUNT    = 1
FS_TIMEOUT   = 300 ms
```

Поддерживаемые значения `CAN_BITRATE` в конфигурации:

``` text
125000
250000
500000
1000000
```

**Важно:** параметр `CAN_BITRATE` в конфигурации ещё необходимо
окончательно проверить на предмет применения при запуске CAN. До этой
проверки рабочей скоростью проекта считается 500 kbit/s.

## Flash-конфигурация

Конфигурация хранится в последней странице Flash STM32F103C8T6:

``` text
0x0800FC00
```

Используются:

-   magic;
-   version;
-   CRC32;
-   проверка диапазонов параметров.

Запись выполняется только по явной команде:

``` text
SAVE Flash
```

`ERASE Flash` удаляет сохранённую конфигурацию. При отсутствии валидной
Flash-конфигурации прошивка использует значения по умолчанию.

Сохранение во Flash и восстановление после многократных холодных
перезапусков проверены на реальном устройстве.

## OLED

SSD1306 128x32 показывает состояние узла.

Пример:

``` text
CAN OK FS ON
P0 OK P1 --
RX 0 E 0
T0 R0 O0 F8
```

Где:

``` text
CAN      состояние CAN
FS       состояние failsafe
P0/P1    состояние PCA9685
RX       принятые actuator-команды
E        ошибки canardSTM32
T        TEC
R        REC
O        RX overflow
F        status flags
```

При `PCA_COUNT=1` второй PCA отображается как:

``` text
P1 --
```

## LED

На проектной плате:

``` text
PB4   RUN
PB3   FAILSAFE
PA15  ERROR
```

RUN использует исправленный TIM2 timebase и даёт короткую индикацию
примерно раз в секунду.

## TIM2

TIM2 у STM32F103C8T6 в данной реализации используется как 16-битный
таймер 1 MHz.

Переполнение происходит каждые:

``` text
65.536 ms
```

Для формирования непрерывного времени реализован `TIM2_IRQHandler`,
программная старшая часть счётчика и функции `micros32()` /
`micros64()`.

Это исправление критично для failsafe, PULSE, NodeStatus и периодических
задач.

# DroneCAN Output Controller Configurator

GUI находится в:

``` text
dronecan_output_controller_configurator/
```

Текущая стабильная версия на момент этого README:

``` text
rev23
```

Используются:

``` text
Python 3
PySide6
dronecan
SocketCAN
```

## Установка GUI

Из каталога конфигуратора:

``` bash
python3 -m venv .venv
source .venv/bin/activate

pip install -r requirements.txt
```

Проверка:

``` bash
python -c "import PySide6, dronecan; print(PySide6.__version__)"
```

## Запуск

Сначала поднять SocketCAN:

``` bash
sudo ip link set can0 down
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up
```

Проверить:

``` bash
ip -details -statistics link show can0
```

Затем:

``` bash
cd dronecan_output_controller_configurator
source .venv/bin/activate
python dronecan_output_controller_configurator_rev_23.py
```

## Возможности GUI rev23

Конфигуратор умеет:

-   подключаться к `can0`;
-   работать с заданным Node ID;
-   читать параметры узла;
-   изменять системные параметры;
-   изменять OUT01...OUT32;
-   автоматически отключать неприменимые поля в зависимости от `TYPE`;
-   учитывать `PCA_COUNT`;
-   записывать только реально изменённые параметры;
-   повторять DroneCAN-запрос при временном timeout;
-   выполнять `SAVE Flash`;
-   выполнять `ERASE Flash`;
-   автоматически перечитывать узел через 500 ms после успешного
    `ERASE`;
-   сохранять конфигурацию в JSON;
-   загружать конфигурацию из JSON;
-   запоминать положение/размер окна;
-   запоминать светлую/тёмную тему;
-   показывать подробные tooltips с задержкой 600 ms.

### Маркеры изменений

``` text
●  изменено в GUI, но ещё не записано в RAM

◆  эта GUI-сессия успешно записала параметр в RAM,
   но после этого ещё не выполнялся SAVE Flash
```

GUI не пытается угадывать содержимое Flash после обычного чтения узла.

`Прочитать узел` отображает фактическую конфигурацию, которую вернул
контроллер.

После `ERASE` старые `◆` удаляются и автоматически выполняется новое
чтение узла.

### Безопасная схема работы

``` text
Прочитать узел
      |
      v
изменить параметры
      |
      | ●
      v
Записать в узел
      |
      | ◆
      v
проверить работу
      |
      v
SAVE Flash
```

Это позволяет сначала проверить конфигурацию в RAM и только затем
сделать её постоянной.

## Горячие клавиши GUI

``` text
Ctrl+Shift+C   открыть соединение
Ctrl+O         открыть файл
Ctrl+S         сохранить файл
Ctrl+Q         выход

F5             прочитать узел
F6             записать в узел
Ctrl+Shift+S   SAVE Flash
F1             помощь
```

# Аппаратные тесты

В `tests/` находятся вспомогательные тесты, использовавшиеся при
разработке:

``` text
test_onoff.py
test_pulse.py
test_pulse_restart.py
test_failsafe_output.py
test_pulse_failsafe.py
test_param_write_output.py
test_params.py
test_params_read_outputs.py
```

На реальном PCA9685 подтверждены:

-   PWM;
-   ON_OFF;
-   PULSE IGNORE;
-   PULSE RESTART;
-   ON_OFF failsafe state 0/1;
-   прерывание PULSE при failsafe;
-   запись параметров в RAM;
-   SAVE Flash;
-   восстановление после холодного старта;
-   ERASE и переход к defaults.

# Сборка прошивки

Клонирование:

``` bash
git clone --recurse-submodules \
    https://github.com/artyrn/stm32_dronecan_pca9685.git

cd stm32_dronecan_pca9685
```

Если submodules не были загружены:

``` bash
git submodule update --init --recursive
```

Установка toolchain на Ubuntu/Debian:

``` bash
sudo apt update
sudo apt install \
    git \
    make \
    python3 \
    gcc-arm-none-eabi \
    binutils-arm-none-eabi \
    openocd
```

Сборка:

``` bash
make clean
make
```

Результаты:

``` text
build/stm32_dronecan_pca9685.elf
build/stm32_dronecan_pca9685.bin
build/stm32_dronecan_pca9685.hex
build/stm32_dronecan_pca9685.map
```

Последняя подтверждённая сборка после реализации логических выходов:

``` text
text 16936
data     0
bss   3800
dec  20736
hex   5100
```

# Прошивка ST-Link

Пример OpenOCD:

``` bash
openocd \
    -f interface/stlink.cfg \
    -f target/stm32f1x.cfg \
    -c "program build/stm32_dronecan_pca9685.elf verify reset exit"
```

# ArduPilot

Контроллер предназначен для работы как DroneCAN-периферия ArduPilot
Rover.

ArduPilot должен передавать actuator commands как PWM в микросекундах.
Для этого используется режим DroneCAN actuator PWM (`USE_ACTUATOR_PWM`).

Окончательные параметры ArduPilot следует фиксировать после проверки на
используемой версии ArduPilot Rover, поскольку имена/опции параметров
могут меняться между версиями.

# TODO

## Конфигуратор

### Поиск контроллера при неизвестном Node ID

Добавить кнопку поиска узла без необходимости заранее знать Node ID.

Предпочтительный способ --- пассивно слушать
`uavcan.protocol.NodeStatus`, а не перебирать Node ID 1...125 запросами
параметров.

После обнаружения узла:

``` text
найденный Node ID
      |
      v
выбор пользователем
      |
      v
автоматическое заполнение Node ID
      |
      v
Прочитать узел
```

### Вкладка CAN-сеть

Добавить сканирование активных DroneCAN-узлов.

Предполагаемая таблица:

``` text
Node ID | Name | Status | Uptime | Health | Mode | Last seen
```

Нужно:

-   пассивно собирать `NodeStatus`;
-   получать `GetNodeInfo` для найденных узлов;
-   показывать активные Node ID;
-   позволить выбрать найденный DroneCAN Output Controller;
-   диагностировать возможные проблемы с Node ID.

### Вкладка Тесты

Добавить безопасное ручное тестирование выходов без изменения
конфигурационных параметров.

Для `PWM`:

``` text
ручная установка PWM 500...2500 us
```

Для `ON_OFF`:

``` text
OFF
ON
```

Для `PULSE`:

``` text
Trigger
```

Нужно учитывать:

-   текущий `TYPE`;
-   `PCA_COUNT`;
-   только реально доступные OUT;
-   безопасное завершение тестового режима;
-   кнопку `Все в безопасное состояние`;
-   тестовые команды не должны автоматически сохранять или изменять
    конфигурацию.

### CAN diagnostics

Добавить отображение:

-   состояния `can0`;
-   bitrate;
-   RX/TX;
-   найденных DroneCAN nodes;
-   доступной через SocketCAN статистики ошибок.

## CAN stress test

Перед окончательной интеграцией на Rover:

-   проверить STM32 на загруженной CAN 500 kbit/s;
-   создать фоновый трафик через `cangen`;
-   измерять загрузку через `canbusload`;
-   проверить примерно 20 / 40 / 60 / 80 % bus load;
-   одновременно передавать DroneCAN actuator commands;
-   одновременно читать/писать параметры;
-   контролировать OLED: CAN, E, TEC, REC, RX overflow;
-   `RX overflow` должен оставаться `0`;
-   проверить failsafe под нагрузкой;
-   после теста решить, нужны ли bxCAN acceptance filters.

## Второй PCA9685

После получения новой платы:

-   физически установить PCA9685 #1 `0x41`;
-   установить `PCA_COUNT=2`;
-   проверить OUT17...OUT32;
-   проверить одновременную работу двух PCA;
-   проверить recovery каждого PCA независимо;
-   проверить failsafe на всех 32 выходах.

## CAN bitrate

Проверить и при необходимости связать `CAN_BITRATE` из
Flash-конфигурации с реальной инициализацией bxCAN.

Нужен безопасный механизм восстановления, если пользователь сохранит
неправильный bitrate или Node ID и потеряет связь с узлом.

# Принцип проекта

Контроллер должен оставаться простым исполнительным CAN-устройством:

``` text
ArduPilot / Lua
    = логика управления

DroneCAN
    = транспорт

STM32
    = исполнитель + локальная защита

PCA9685
    = физические выходы
```

Сложную логику Rover не следует переносить в STM32 без необходимости.
