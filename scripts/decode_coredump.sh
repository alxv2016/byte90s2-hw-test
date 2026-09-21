#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

PORT="${1:-${SERIAL_PORT:-}}"
if [[ -z "${PORT}" ]]; then
  for candidate in /dev/cu.usbmodem* /dev/tty.usbmodem*; do
    if [[ -e "${candidate}" ]]; then
      PORT="${candidate}"
      break
    fi
  done
fi

if [[ -z "${PORT}" ]]; then
  echo "No serial port detected. Pass one explicitly, e.g.:" >&2
  echo "  scripts/decode_coredump.sh /dev/cu.usbmodem101" >&2
  exit 1
fi

PIO_PYTHON="${HOME}/.platformio/penv/bin/python"
ESPTOOL_PY="${HOME}/.platformio/packages/tool-esptoolpy/esptool.py"
ELF_PATH="${REPO_ROOT}/.pio/build/seeed_xiao_esp32s3/firmware.elf"
COREDUMP_OFFSET="${COREDUMP_OFFSET:-0x4e0000}"
COREDUMP_SIZE="${COREDUMP_SIZE:-0x20000}"
PARTTABLE_OFFSET="${PARTTABLE_OFFSET:-0x8000}"
BAUD="${BAUD:-115200}"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUT_DIR="${REPO_ROOT}/.pio/build/seeed_xiao_esp32s3/coredumps"
RAW_CORE="${OUT_DIR}/coredump-${STAMP}.raw"
REPORT_FILE="${OUT_DIR}/coredump-${STAMP}.txt"

mkdir -p "${OUT_DIR}"

GDB_CANDIDATES=(
  "${HOME}/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-gdb"
  "${HOME}/.platformio/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-gdb"
  "${HOME}/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp-elf-gdb"
)

GDB_BIN=""
for candidate in "${GDB_CANDIDATES[@]}"; do
  if [[ -x "${candidate}" ]]; then
    GDB_BIN="${candidate}"
    break
  fi
done

if [[ ! -x "${PIO_PYTHON}" ]]; then
  echo "PlatformIO Python not found at ${PIO_PYTHON}" >&2
  exit 1
fi

if [[ -z "${GDB_BIN}" ]]; then
  echo "ESP32-S3 GDB not found in PlatformIO packages." >&2
  exit 1
fi

if [[ ! -f "${ELF_PATH}" ]]; then
  echo "Firmware ELF not found at ${ELF_PATH}. Build first." >&2
  exit 1
fi

READ_CMD=(
  "${PIO_PYTHON}" "${ESPTOOL_PY}"
  --chip esp32s3
  --port "${PORT}"
  --baud "${BAUD}"
  --before default-reset
  --after no-reset
  read-flash
  "${COREDUMP_OFFSET}"
  "${COREDUMP_SIZE}"
  "${RAW_CORE}"
)

DECODE_CMD=(
  "${PIO_PYTHON}" -m esp_coredump
  --chip esp32s3
  info_corefile
  --gdb "${GDB_BIN}"
  --core "${RAW_CORE}"
  --core-format raw
  --parttable-off "${PARTTABLE_OFFSET}"
  "${ELF_PATH}"
)

echo "Decoding core dump from ${PORT}"
echo "ELF: ${ELF_PATH}"
echo "Raw core output: ${RAW_CORE}"
echo "Report output: ${REPORT_FILE}"

echo
echo "Reading raw core dump from flash..."
"${READ_CMD[@]}"

echo
echo "Decoding core dump..."
"${DECODE_CMD[@]}" | tee "${REPORT_FILE}"
