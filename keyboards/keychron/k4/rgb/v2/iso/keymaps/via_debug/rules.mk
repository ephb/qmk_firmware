VIA_ENABLE = yes
BLUETOOTH_ENABLE = yes
BLUETOOTH_DRIVER = custom
LTO_ENABLE = yes
SRC += keyboards/keychron/k4/rgb/v2/iso/keymaps/via/bluetooth.c
COMMON_VPATH += keyboards/keychron/common $(DRIVER_PATH)/bluetooth
SRC += keyboards/keychron/common/keychron_bt.c $(DRIVER_PATH)/bluetooth/iton_bt.c
CONSOLE_ENABLE = yes
KEYBOARD_SHARED_EP = yes

# Same debug stack sizes as upstream K4v2 debug PR #487 (8 KiB SRAM).
USE_PROCESS_STACKSIZE = 0x400
USE_EXCEPTIONS_STACKSIZE = 0x200
