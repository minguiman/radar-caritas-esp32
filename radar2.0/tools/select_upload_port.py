"""Detecta puertos COM y permite elegir cuando hay varias opciones."""
import json
import subprocess
import sys

ESPRESSIF_VID = "303A"
WCH_VID = "1A86"


def device_text(device):
    return " ".join(
        str(device.get(key, ""))
        for key in ("port", "description", "hwid", "manufacturer")
    ).upper()


def list_ports():
    out = subprocess.check_output(
        [sys.executable, "-m", "platformio", "device", "list", "--serial", "--json-output"]
    )
    return json.loads(out)


def is_espressif(hwid):
    return ESPRESSIF_VID in (hwid or "")


def is_uart_adapter(device):
    text = device_text(device)
    return (
        WCH_VID in text
        or "CH343" in text
        or "CH340" in text
        or "USB-ENHANCED-SERIAL" in text
        or "USB-SERIAL" in text
    )


def is_builtin_com(device):
    return device.get("port", "").upper() == "COM1"


def normalize_port(value):
    value = value.strip().upper()
    if not value.startswith("COM"):
        value = f"COM{value}"
    return value


def pick_port(devices):
    if not devices:
        print("ERROR: No hay puertos serie conectados.", file=sys.stderr)
        return None

    uart_adapters = [d for d in devices if is_uart_adapter(d)]
    espressif = [d for d in devices if is_espressif(d.get("hwid", ""))]
    usable = [d for d in devices if not is_builtin_com(d)]

    if len(devices) == 1:
        return devices[0]["port"]

    if len(uart_adapters) == 1:
        return uart_adapters[0]["port"]

    if len(espressif) == 1:
        return espressif[0]["port"]

    if len(usable) == 1:
        return usable[0]["port"]

    print("\nPuertos serie detectados:\n", file=sys.stderr)
    for index, device in enumerate(devices, 1):
        if is_uart_adapter(device):
            tag = " [UART CH343/CH340 - recomendado]"
        elif is_espressif(device.get("hwid", "")):
            tag = " [USB nativo ESP32]"
        elif is_builtin_com(device):
            tag = " [COM del PC - evitar]"
        else:
            tag = ""
        print(f"  {index}. {device['port']} - {device.get('description', '?')}{tag}", file=sys.stderr)
        if device.get("hwid"):
            print(f"     {device['hwid']}", file=sys.stderr)
    print("", file=sys.stderr)

    default = (
        uart_adapters[0]["port"]
        if uart_adapters
        else espressif[0]["port"]
        if espressif
        else usable[0]["port"]
        if usable
        else devices[0]["port"]
    )
    default_index = next(
        (index for index, device in enumerate(devices, 1) if device["port"] == default),
        1,
    )

    while True:
        try:
            choice = input(
                f"Elige puerto [1-{len(devices)}] o escribe COMx (Enter = opcion {default_index}): "
            ).strip()
        except EOFError:
            return default

        if choice == "":
            return default

        if choice.upper().startswith("COM"):
            port = normalize_port(choice)
            if any(device["port"].upper() == port for device in devices):
                return port
            print(f"ERROR: {port} no esta en la lista.", file=sys.stderr)
            continue

        try:
            index = int(choice)
            if 1 <= index <= len(devices):
                return devices[index - 1]["port"]
        except ValueError:
            pass

        print("Opcion no valida. Intenta de nuevo.", file=sys.stderr)


def main():
    if len(sys.argv) > 1 and sys.argv[1].strip():
        print(normalize_port(sys.argv[1]))
        return 0

    try:
        devices = list_ports()
    except subprocess.CalledProcessError as error:
        print(f"ERROR: No se pudieron listar los puertos ({error}).", file=sys.stderr)
        return 1
    except json.JSONDecodeError:
        print("ERROR: Respuesta invalida al listar puertos.", file=sys.stderr)
        return 1

    port = pick_port(devices)
    if not port:
        return 1

    print(port)
    return 0


if __name__ == "__main__":
    sys.exit(main())
