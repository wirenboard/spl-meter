TARGET = wb-mqtt-spl-meter

CFLAGS += -lasound -lm -lmosquitto
CFLAGS += -Wall -Wextra

all: $(TARGET)

$(TARGET): $(TARGET).c

.PHONY: install clean

install: $(TARGET)
	install -d $(DESTDIR)/usr/bin
	install -m 0755 $(TARGET) $(DESTDIR)/usr/bin/

clean:
	rm $(TARGET)
