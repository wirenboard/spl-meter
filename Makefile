TARGET = wb-mqtt-spl-meter

CFLAGS += -lasound -lm -lmosquitto
CFLAGS += -Wall -Wextra

all: $(TARGET)

$(TARGET): $(TARGET).c
