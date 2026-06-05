REPO_ROOT := ../..
BUILD_DIR := build
TARGET := $(BUILD_DIR)/driver_unit_tests
RESULTS := test-results.txt
REPORT := test-report.html

CC ?= cc
CPPFLAGS := -Istubs -I$(REPO_ROOT)/FSBL/Inc -DAPP_UART_TRANSPORT_FRAME_POINTER=TestFramePointer
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2

SOURCES := \
	test_driver_functions.c \
	$(REPO_ROOT)/FSBL/Src/app_crypto.c \
	$(REPO_ROOT)/FSBL/Src/app_memory.c \
	$(REPO_ROOT)/FSBL/Src/app_transport_uart.c \
	$(REPO_ROOT)/FSBL/Src/snapshot_protocol.c

.PHONY: all test report clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -o $@

test: $(TARGET)
	./$(TARGET)

report: $(TARGET)
	@./$(TARGET) > $(RESULTS); status=$$?; python3 render_html_report.py $(RESULTS) $(REPORT); exit $$status

clean:
	rm -rf $(BUILD_DIR)
