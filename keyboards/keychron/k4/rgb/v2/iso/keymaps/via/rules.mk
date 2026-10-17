VIA_ENABLE = yes
BLUETOOTH_ENABLE = yes
BLUETOOTH_DRIVER = custom
LTO_ENABLE = yes
SRC += bluetooth.c
COMMON_VPATH += keyboards/keychron/common $(DRIVER_PATH)/bluetooth
SRC += keyboards/keychron/common/keychron_bt.c $(DRIVER_PATH)/bluetooth/iton_bt.c
