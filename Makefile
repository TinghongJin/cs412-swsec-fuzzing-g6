# =================================================================
# CS-412 Fuzzing Lab — Makefile
#
# Targets:
#   make build            Rebuild all 4 harness variants
#   make fuzz             Instrumented campaign        (member A)
#   make fuzz-qemu        QEMU black-box campaign      (member C)
#   make fuzz-persistent  Persistent-mode campaign      (member D)
#   make fuzz-nosan       No-ASan campaign              (member D)
#   make plot             Generate afl-plot for both main campaigns
#   make triage           Reproduce + minimize + ASan trace (member B)
#   make seeds            Generate diverse seeds        (member C)
#   make clean            Remove all build & campaign artifacts
# =================================================================

# --- Paths -------------------------------------------------------
LIBPNG       = libpng-1.6.15
INST         = $(LIBPNG)/install
INST_NOSAN   = $(LIBPNG)/install_nosan
INST_VANILLA = $(LIBPNG)/install_vanilla
DICT         = png.dict
SEEDS        = seeds

# --- Compiler config ---------------------------------------------
AFL_CC  = afl-clang-fast
PLAIN_CC = gcc
COMMON  = -g -O1
ASAN    = -fsanitize=address $(COMMON)

INC_INST     = -I./$(INST)/include
LIB_INST     = -L./$(INST)/lib -lpng16 -lz -lm
INC_NOSAN    = -I./$(INST_NOSAN)/include
LIB_NOSAN    = -L./$(INST_NOSAN)/lib -lpng16 -lz -lm
INC_VANILLA  = -I./$(INST_VANILLA)/include
LIB_VANILLA  = -L./$(INST_VANILLA)/lib -lpng16 -lz -lm

# --- Build targets -----------------------------------------------
.PHONY: build fuzz fuzz-qemu fuzz-persistent fuzz-nosan \
        plot triage seeds clean

build: png_fuzz png_fuzz_qemu png_fuzz_nosan png_fuzz_persistent

png_fuzz: src/harness.c
	$(AFL_CC) $< $(INC_INST) $(LIB_INST) $(ASAN) -o $@

png_fuzz_nosan: src/harness.c
	$(AFL_CC) $< $(INC_NOSAN) $(LIB_NOSAN) $(COMMON) -o $@

png_fuzz_qemu: src/harness.c
	$(PLAIN_CC) $< $(INC_VANILLA) $(LIB_VANILLA) $(COMMON) -o $@

png_fuzz_persistent: src/harness_persistent.c
	$(AFL_CC) $< $(INC_INST) $(LIB_INST) $(ASAN) -o $@

# --- Fuzzing campaigns -------------------------------------------
fuzz: png_fuzz
	AFL_SKIP_CPUFREQ=1 \
	afl-fuzz -i $(SEEDS) -o findings -x $(DICT) -- ./png_fuzz @@

fuzz-qemu: png_fuzz_qemu
	AFL_SKIP_CPUFREQ=1 \
	afl-fuzz -Q -i $(SEEDS) -o findings-qemu -x $(DICT) -- ./png_fuzz_qemu @@

fuzz-persistent: png_fuzz_persistent
	AFL_SKIP_CPUFREQ=1 \
	afl-fuzz -i $(SEEDS) -o findings-persistent -x $(DICT) -- ./png_fuzz_persistent @@

fuzz-nosan: png_fuzz_nosan
	AFL_SKIP_CPUFREQ=1 \
	afl-fuzz -i $(SEEDS) -o findings-nosan -x $(DICT) -- ./png_fuzz_nosan @@

# --- Seed generation (member C) ----------------------------------
seeds:
	@mkdir -p $(SEEDS)
	convert -size 8x8 xc:red               png24:$(SEEDS)/rgb_8x8.png
	convert -size 8x8 -colorspace Gray xc:gray     $(SEEDS)/gray_8x8.png
	convert -size 8x8 -type Palette xc:red          $(SEEDS)/palette_8x8.png
	convert -size 8x8 xc:'rgba(255,0,0,0.5)'       $(SEEDS)/alpha_8x8.png
	convert -size 8x8 -colorspace Gray xc:'graya(50%,0.5)' $(SEEDS)/grayalpha_8x8.png
	convert -size 4x4 -depth 16 xc:blue             $(SEEDS)/16bit_4x4.png
	convert -size 8x8 -interlace PNG xc:green        $(SEEDS)/interlaced_8x8.png
	convert -size 8x8 xc:white -set Comment "fuzz"   $(SEEDS)/text_8x8.png
	@echo "=== Generated $$(ls $(SEEDS)/*.png | wc -l) seed files ==="

# --- Analysis (member B for triage, member A for plot) -----------
plot:
	afl-plot findings/default/        plot_output/
	afl-plot findings-qemu/default/   plot_output_qemu/

triage:
	@echo "=== Crash listing ==="
	@ls findings/default/crashes/ 2>/dev/null || echo "No crashes found."
	@echo ""
	@FIRST=$$(ls findings/default/crashes/id:* 2>/dev/null | head -1); \
	if [ -n "$$FIRST" ]; then \
	    echo "=== Reproducing $$FIRST ==="; \
	    ./png_fuzz "$$FIRST" || true; \
	    echo ""; \
	    echo "=== Minimizing ==="; \
	    afl-tmin -i "$$FIRST" -o minimized.png -- ./png_fuzz @@; \
	    echo ""; \
	    echo "=== ASan stack trace ==="; \
	    ASAN_OPTIONS=symbolize=1 ./png_fuzz minimized.png || true; \
	else \
	    echo "No crashes to triage."; \
	fi

# --- Cleanup -----------------------------------------------------
clean:
	rm -f png_fuzz png_fuzz_qemu png_fuzz_nosan png_fuzz_persistent
	rm -f minimized.png
	rm -rf findings findings-qemu findings-persistent findings-nosan
	rm -rf plot_output plot_output_qemu
