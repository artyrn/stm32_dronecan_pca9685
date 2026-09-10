#!/bin/bash

set -e

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
VENV="$BASE_DIR/dronecan_output_controller_configurator/.venv"
APP="$BASE_DIR/dronecan_output_controller_configurator/dronecan_output_controller_configurator_rev_23.py"

if [ ! -x "$VENV/bin/python3" ]; then
    echo "Ошибка: не найден Python виртуального окружения:"
    echo "  $VENV/bin/python3"
    echo
    echo "Создайте .venv и установите зависимости:"
    echo "  cd \"$BASE_DIR/dronecan_output_controller_configurator\""
    echo "  python3 -m venv .venv"
    echo "  .venv/bin/pip install -r requirements.txt"
    exit 1
fi

if [ ! -f "$APP" ]; then
    echo "Ошибка: не найден конфигуратор:"
    echo "  $APP"
    exit 1
fi

exec "$VENV/bin/python3" "$APP" "$@"
