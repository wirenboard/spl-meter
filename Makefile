TARGET = wb-mqtt-spl-meter

CXXFLAGS += -Wall -Wextra -std=c++0x -ggdb -O0
LDFLAGS += -lasound -lm -lmosquitto -lmosquittopp -ljsoncpp -lwbmqtt

ifeq ($(DEB_TARGET_ARCH),armel)
CROSS_COMPILE=arm-linux-gnueabi-
endif

CXX=$(CROSS_COMPILE)g++

.PHONY: all install clean

all: $(TARGET)

$(TARGET): $(TARGET).cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $< -o $@

install: $(TARGET) config.json
	install -d $(DESTDIR)/usr/bin
	install -m 0755 $(TARGET) $(DESTDIR)/usr/bin/
	install -m 0644 config.json $(DESTDIR)/etc/wb-mqtt-spl-meter.json

clean:
	rm -f $(TARGET)
