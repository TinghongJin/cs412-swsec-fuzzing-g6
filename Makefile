# --- Paths -------------------------------------------------------

INST         = libpng-1.6.15/install
INST_NOSAN   = libpng-1.6.15_nosan/install_nosan
INST_VANILLA = libpng-1.6.15_vanilla/install_vanilla
INST_CRASH   = libpng-1.6.15_crash/install_crash
DICT         = png.dict
SEEDS        = seeds/


FINDINGS  = findings/
FINDINGS_QEMU = findings-qemu/
FINDINGS_PERSISTENT = findings-persistent/
FINDINGS_NOSAN = findings-nosan/

# --- Build targets -----------------------------------------------
.PHONY: build fuzz fuzz-qemu fuzz-persistent fuzz-nosan \
        plot triage seeds clean

build: png_fuzz png_fuzz_qemu png_fuzz_nosan png_fuzz_persistent

png_fuzz: src/harness.c
	afl-clang-fast \
	    -fsanitize=address -g -O1 -fno-omit-frame-pointer \
	    $< \
	    -I$(INST)/include \
	    -L$(INST)/lib \
	    -lpng16 -lz -lm \
	    -o $@

png_fuzz_crash: src/harness.c
	afl-clang-fast \
	    -fsanitize=address -g -O1 -fno-omit-frame-pointer \
	    $< \
	    -I$(INST_CRASH)/include \
	    -L$(INST_CRASH)/lib \
	    -lpng16 -lz -lm \
	    -o $@

png_fuzz_nosan: src/harness.c
	afl-clang-fast \
	    -g -O1 -fno-omit-frame-pointer\
	    $< \
	    -I$(INST_NOSAN)/include \
	    -L$(INST_NOSAN)/lib \
	    -lpng16 -lz -lm \
	    -o $@

png_fuzz_qemu: src/harness.c
	gcc -g -O1 -fno-omit-frame-pointer\
	    $< \
	    -I$(INST_VANILLA)/include \
	    -L$(INST_VANILLA)/lib \
	    -lpng16 -lz -lm \
	    -o $@

png_fuzz_persistent: src/harness_persistent.c
	afl-clang-fast \
	    -fsanitize=address -g -O1 -fno-omit-frame-pointer \
	    $< \
	    -I$(INST)/include \
	    -L$(INST)/lib \
	    -lpng16 -lz -lm \
	    -o $@


# --- Fuzzing campaigns -------------------------------------------
fuzz: png_fuzz
	afl-fuzz -i $(SEEDS) -o $(FINDINGS) -x $(DICT) -- ./png_fuzz @@

fuzz-qemu: png_fuzz_qemu
	afl-fuzz -Q -i $(SEEDS) -o $(FINDINGS_QEMU) -x $(DICT) -- ./png_fuzz_qemu @@

fuzz-persistent: png_fuzz_persistent
	afl-fuzz -i $(SEEDS) -o $(FINDINGS_PERSISTENT) -x $(DICT) -- ./png_fuzz_persistent @@

fuzz-nosan: png_fuzz_nosan
	afl-fuzz -i $(SEEDS) -o $(FINDINGS_NOSAN) -x $(DICT) -- ./png_fuzz_nosan @@


# --- Analysis -----------
plot:
	afl-plot $(FINDINGS)/default/        plot_output/
	afl-plot $(FINDINGS_QEMU)/default/   plot_output_qemu/


# --- Cleanup -----------------------------------------------------
clean:
	rm -f png_fuzz png_fuzz_qemu png_fuzz_nosan png_fuzz_persistent
	rm -rf findings findings-qemu findings-persistent findings-nosan
	rm -rf plot_output plot_output_qemu
