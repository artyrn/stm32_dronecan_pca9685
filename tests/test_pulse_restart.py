#!/usr/bin/env python3

import time
import dronecan

TARGET_NODE_ID = 42
ACTUATOR_ID = 1

node = dronecan.make_node(
    "can0",
    node_id=100,
    bitrate=500000,
)


def request_sync(request, timeout=2.0):
    result = {"done": False, "event": None}

    def callback(event):
        result["done"] = True
        result["event"] = event

    node.request(
        request,
        TARGET_NODE_ID,
        callback,
        timeout=1.0,
    )

    deadline = time.monotonic() + timeout

    while not result["done"] and time.monotonic() < deadline:
        node.spin(0.05)

    return result["event"]


def write_param(name, value):
    req = dronecan.uavcan.protocol.param.GetSet.Request()
    req.name = name
    req.value.integer_value = value

    event = request_sync(req)

    if event is None:
        print(f"{name}: TIMEOUT")
        return False

    try:
        returned = event.response.value.integer_value
    except (AttributeError, TypeError):
        print(f"{name}: WRITE FAILED")
        return False

    print(f"{name}: requested={value}, returned={returned}")
    return returned == value


def send_level(pwm, duration):
    msg = dronecan.uavcan.equipment.actuator.ArrayCommand()

    cmd = dronecan.uavcan.equipment.actuator.Command()
    cmd.actuator_id = ACTUATOR_ID
    cmd.command_type = 4       # PWM
    cmd.command_value = float(pwm)

    msg.commands.append(cmd)

    print(
        f"Sending PWM={pwm} at 20 Hz "
        f"for {duration:.1f} s"
    )

    deadline = time.monotonic() + duration
    next_tx = time.monotonic()

    while time.monotonic() < deadline:
        now = time.monotonic()

        if now >= next_tx:
            node.broadcast(msg)
            next_tx += 0.05

        node.spin(0.005)


try:
    print("Configure OUT01 as PULSE / RETRIG=RESTART")
    print()

    if not write_param("OUT01_TYPE", 3):
        raise SystemExit(1)

    if not write_param("OUT01_ON", 2000):
        raise SystemExit(1)

    if not write_param("OUT01_OFF", 1000):
        raise SystemExit(1)

    if not write_param("OUT01_TIME", 2000):
        raise SystemExit(1)

    if not write_param("OUT01_RETRIG", 1):
        raise SystemExit(1)

    print()
    print("NO SAVE - configuration is RAM only.")
    print()

    # --------------------------------------------------
    # 1. Establish OFF
    # --------------------------------------------------

    print("STEP 1: establish OFF")
    print("Expected output: ~0.16 V")

    send_level(1000, 2.0)

    input("Press Enter for ON test...")

    # --------------------------------------------------
    # 2. Continuous ON
    # --------------------------------------------------

    print()
    print("STEP 2: continuous ON for 6 seconds")
    print()
    print("OUT01_TIME = 2000 ms")
    print("ON commands arrive every 50 ms.")
    print()
    print("Expected:")
    print("  output goes to ~0.32 V")
    print("  and remains ~0.32 V for ALL 6 seconds")
    print("  because every ON restarts the pulse timer.")

    send_level(2000, 6.0)

    print()
    print("ON transmission has now stopped.")
    print()
    print(
        "Do not send anything now. "
        "The last ON should leave the timer running."
    )
    print()
    print(
        "Expected: after about 2 seconds output "
        "would normally return to OFF, but FS_TIMEOUT=700 ms "
        "will occur first."
    )

    input("Observe output / failsafe, then press Enter...")

    # --------------------------------------------------
    # 3. Clear failsafe and establish OFF
    # --------------------------------------------------

    print()
    print("STEP 3: send OFF for 2 seconds")
    print("Expected output: ~0.16 V")

    send_level(1000, 2.0)

    input("Press Enter for second ON test...")

    # --------------------------------------------------
    # 4. Second continuous ON
    # --------------------------------------------------

    print()
    print("STEP 4: continuous ON again")
    print("Expected output: ~0.32 V for all 5 seconds.")

    send_level(2000, 5.0)

    # --------------------------------------------------
    # 5. Explicit OFF
    # --------------------------------------------------

    print()
    print("STEP 5: explicit OFF")
    print()
    print(
        "Important: with the current firmware an OFF command "
        "does NOT abort an already running pulse."
    )
    print(
        "Therefore the output may remain ON until OUT01_TIME "
        "expires after the last ON/restart."
    )

    send_level(1000, 3.0)

    print()
    print("Expected final output: ~0.16 V")
    print()
    print("PULSE RETRIG=RESTART test finished.")

finally:
    node.close()
