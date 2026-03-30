# =============================================================================
#  Makefile — lora_edge_gateway
#
#  BUILD DUAL:
#    make          → cross-compila para ARM (requiere SDK mLinux activo)
#    make test     → compila y ejecuta tests en x86 nativo (NO requiere SDK)
#    make clean    → limpia todo
#    make deploy   → scp al gateway (make deploy TARGET_IP=192.168.2.1)
#
#  La clave de la build dual:
#    - El target ARM usa $(CXX) exportado por el SDK (arm-linux-gnueabi-g++)
#    - El target test fuerza CXX_NATIVE=g++ del host x86, ignorando el SDK
#    - lora_codec.cpp no depende de Mosquitto ni jsoncpp → compila en ambos
# =============================================================================

# ---- Binario principal (ARM) -------------------------------------------------
TARGET      := lora_edge_gateway

# ---- Fuentes del firmware (requieren libmosquittopp + libjsoncpp) ------------
APP_SRCS    := main.cpp mqtt_sample.cpp #lora_codec.cpp
APP_OBJS    := $(APP_SRCS:.cpp=.o)

# ---- Compilador cross (ARM) — exportado por el SDK mLinux -------------------
CXX         ?= g++
SYSROOT     ?= /usr/local/oecore-x86_64/sysroots/arm926ejste-mlinux-linux-gnueabi

CXXFLAGS    += -std=c++11 -Wall -Wextra -Wpedantic -O2
CXXFLAGS    += -ffunction-sections -fdata-sections
CXXFLAGS    += -I$(SYSROOT)/usr/include

LDFLAGS     += -Wl,--gc-sections
LDLIBS      := -lmosquittopp -lmosquitto -ljsoncpp

# ---- Compilador nativo (x86) — SIEMPRE g++ del host, nunca el SDK -----------
CXX_NATIVE  := g++

TEST_DIR    := build_test
TEST_BIN    := $(TEST_DIR)/test_lora_codec
TEST_SRCS   := test/test_lora_codec.cpp lora_codec.cpp
TEST_OBJS   := $(addprefix $(TEST_DIR)/, $(notdir $(TEST_SRCS:.cpp=.o)))

CXXFLAGS_NATIVE := -std=c++11 -Wall -Wextra -O0 -g -I.
LDLIBS_NATIVE   := -lgtest -lpthread

# =============================================================================
#  Targets
# =============================================================================

.PHONY: all test clean deploy check-sdk

all: check-sdk $(TARGET)

$(TARGET): $(APP_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)
	@echo ""
	@echo ">>> Build ARM completada: $(TARGET)"
	@file $(TARGET) 2>/dev/null || true

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

check-sdk:
	@if [ "$(CXX)" = "g++" ]; then \
	  echo ""; \
	  echo "ADVERTENCIA: SDK mLinux no activo. El binario será x86, no ARM."; \
	  echo "  . /usr/local/oecore-x86_64/environment-setup-arm926ejste-mlinux-linux-gnueabi"; \
	  echo ""; \
	fi

# ---- Tests nativos x86 -------------------------------------------------------
test: $(TEST_BIN)
	@echo ""
	@echo ">>> Ejecutando tests..."
	@echo "------------------------------------------------------------"
	./$(TEST_BIN) --gtest_color=yes
	@echo "------------------------------------------------------------"

$(TEST_BIN): $(addprefix $(TEST_DIR)/, test_lora_codec.o lora_codec.o)
	@mkdir -p $(TEST_DIR)
	$(CXX_NATIVE) -o $@ $^ $(LDLIBS_NATIVE)

$(TEST_DIR)/test_lora_codec.o: test/test_lora_codec.cpp lora_codec.h
	@mkdir -p $(TEST_DIR)
	$(CXX_NATIVE) $(CXXFLAGS_NATIVE) -c -o $@ $<

$(TEST_DIR)/lora_codec.o: lora_codec.cpp lora_codec.h
	@mkdir -p $(TEST_DIR)
	$(CXX_NATIVE) $(CXXFLAGS_NATIVE) -c -o $@ $<

# ---- Limpieza ----------------------------------------------------------------
clean:
	rm -f $(APP_OBJS) $(TARGET)
	rm -rf $(TEST_DIR)
	@echo ">>> Limpieza completa."

deploy: $(TARGET)
ifndef TARGET_IP
	$(error TARGET_IP no definido. Uso: make deploy TARGET_IP=<ip>)
endif
	scp $(TARGET) admin@$(TARGET_IP):/home/admin/$(TARGET)
