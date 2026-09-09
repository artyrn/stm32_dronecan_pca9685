#!/usr/bin/env python3

import time
import dronecan

NODE_ID = 42

node = dronecan.make_node(
    "can0",
    node_id=100,
    bitrate=500000,
)

names = [
    "NODE_ID",
    "CAN_BITRATE",
    "PCA_COUNT",
    "FS_TIMEOUT",

    "OUT01_TYPE",
    "OUT01_ON",
    "OUT01_OFF",
    "OUT01_FS",
    "OUT01_FS_STATE",
    "OUT01_TIME",
    "OUT01_RETRIG",

    "OUT16_TYPE",
    "OUT16_FS",

    # При PCA_COUNT=1 этого параметра быть не должно
    "OUT17_TYPE",
]


def read_param(name):
    request = dronecan.uavcan.protocol.param.GetSet.Request()
    request.name = name

    result = {"done": False}

    def callback(event):
        result["done"] = True

        if event is None:
            print(f"{name:15} TIMEOUT")
            return

        response = event.response

        # Не найденный параметр возвращает EMPTY.
        try:
            value = response.value.integer_value
            print(f"{name:15} = {value}")
        except (AttributeError, TypeError):
            print(f"{name:15} = NOT FOUND ({response.value})")

    node.request(
        request,
        NODE_ID,
        callback,
        timeout=1.0,
    )

    deadline = time.monotonic() + 2.0

    while not result["done"] and time.monotonic() < deadline:
        node.spin(0.05)

    if not result["done"]:
        print(f"{name:15} NO CALLBACK")


try:
    for name in names:
        read_param(name)
finally:
    node.close()
