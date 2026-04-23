# CS-412 Fuzzing Lab — 4-Person Team Guide

> 每人负责2个问题，每一步包含目的、原理、命令和报告框架

---

## 总览：谁做什么

| 成员 | Phase 1 (Day 1–2) | Phase 2 (Day 3–5) | 写哪些 Q | 核心交付物 |
|------|-------------------|-------------------|----------|-----------|
| **A** | Dockerfile + Makefile | 跑 instrumented campaign ≥30min | **Q2 + Q4** | 环境、campaign 数据 |
| **B** | harness.c + harness_persistent.c | Crash triage (reproduce→minimize→ASan) | **Q1 + Q5** | 两个 harness、triage 报告 |
| **C** | 种子生成 + 字典准备 | 跑 QEMU campaign ≥30min | **Q3 + Q7** | seeds/、QEMU 数据 |
| **D** | LaTeX 模板 + CVE 研究 | 3 种配置性能对比 + edge count | **Q6 + Q8** | 性能数据、report.tex、附录 |

### 数据依赖（必须协调）

```
A 的 campaign 数据 ──→ B（crash 文件做 triage）
                   ──→ C（Q3 需要 dict/havoc yields 数字）
                   ──→ C（Q7 需要 instrumented 数据做对比）
                   ──→ D（Q8 需要 edge count 和 map density）
```

**A 跑完 campaign 后立刻发群里：** status screen 截图、exec speed、stability、
corpus count、map density、cycles done、dict/havoc/splice yields。

---

# 成员 A — 环境搭建 + Q2 + Q4

## A 的职责

你是项目瓶颈——其他三人都等你的 Docker 环境。Day 1 优先把 Dockerfile 调通。
你还负责跑主 campaign 并撰写 Q2（编译 flag 解释）和 Q4（campaign 分析）。

---

## A-1：理解项目文件

项目已包含以下文件（不需要从零写）：

- `Dockerfile` — 完整的构建环境，编译 4 种 harness 变体
- `Makefile` — 所有 build/fuzz/plot/triage 命令
- `src/harness.c` — B 写的标准 harness
- `src/harness_persistent.c` — B 写的 persistent harness
- `report.tex` — D 准备的 USENIX 模板

你的任务是**验证 Dockerfile 能 build 成功**，调试任何问题。

## A-2：构建 Docker 镜像

```bash
cd cs412-fuzzing-lab
docker build -t cs412-fuzz .
```

可能遇到的问题和解决方法：

| 错误 | 原因 | 解决 |
|------|------|------|
| `patch failed` | 补丁路径不对 | Dockerfile 中 sed 会作为 fallback 自动修 |
| `png.h: No such file` | -I 路径错 | 检查 `install/include` 目录是否存在 |
| `cannot find -lpng12` | make install 失败 | 检查 `install/lib` 中有无 `libpng12.a` |
| `No such file: harness.c` | src/ 目录没有 harness.c | 确保 B 已经写好并放进 src/ |

## A-3：启动容器并验证

```bash
docker run --rm -it \
    -v $(pwd)/findings:/work/findings \
    -v $(pwd)/findings-qemu:/work/findings-qemu \
    -v $(pwd)/plot_output:/work/plot_output \
    -v $(pwd)/plot_output_qemu:/work/plot_output_qemu \
    cs412-fuzz

# 容器内验证
ls png_fuzz png_fuzz_qemu png_fuzz_nosan png_fuzz_persistent
./png_fuzz seeds/*.png && echo "Harness OK"
```

**Commit：** `git commit -m "feat: Dockerfile builds all 4 harness variants"`

## A-4：跑 Instrumented Campaign

```bash
make fuzz
# 即: afl-fuzz -i seeds -o findings -x png.dict -- ./png_fuzz @@
```

跑≥30min。你会看到 AFL++ status screen：

```
┌─ overall results ───────────────────────────────────┤
│  cycles done : ___                                  │ ← 记录
│ corpus count : ___                                  │ ← 记录
│saved crashes : ___                                  │ ← 记录
├─ cycle progress ────────────────────────────────────┤
│  map density : ___% / ___%                          │ ← 记录
│    stability : ___%                                 │ ← 记录
├─ stage progress ────────────────────────────────────┤
│  exec speed : ___/sec                               │ ← 记录
├─ fuzzing strategy yields ───────────────────────────┤
│  dictionary : ___/___                               │ ← C 需要
│havoc/splice : ___/___                               │ ← C 需要
└─────────────────────────────────────────────────────┘
```

**截图！** 然后 Ctrl+C 停止，生成图表：

```bash
afl-plot findings/default/ plot_output/
```

**Commit：** `git commit -m "data: instrumented campaign (30min)"`

## A-5：写 Q2 — Instrumentation and Sanitizers

### 内容框架

列出所有 flag 和 CRC patch，逐个解释作用和缺少的后果：

```
Flag: CC=afl-clang-fast
作用：用 AFL++ 编译器在每个分支点（if/switch/loop）插入覆盖率追踪代码。
      运行时通过 shared memory bitmap 记录 edge 命中：
      map[cur_loc ^ (prev_loc >> 1)]++
不用的后果：fuzzer 没有覆盖率反馈，无法区分走了新路径还是旧路径，
            退化为纯随机变异（blind fuzzing），效率极低。

Flag: -fsanitize=address (ASan)
作用：在每次内存访问（load/store）前插入检查代码。
      维护 shadow memory（每 8 字节应用内存对应 1 字节 shadow），
      记录每个地址是否可访问。检测：堆溢出、栈溢出、UAF、double-free。
不用的后果：很多内存 bug 不会立即 crash。例如写越界 3 字节覆盖了
            邻近分配的 padding，程序继续运行但数据已损坏。

Flag: -g
作用：保留调试符号（函数名、文件名、行号）。
不用的后果：ASan 报告只有十六进制地址如 "0x5555... in ???",
            无法定位源码位置。

Flag: -O1
作用：轻度编译优化。
不用的后果：O0 代码膨胀导致 exec speed 低；
            O2/O3 激进优化可能内联或消除有 bug 的代码。

Flag: --disable-shared
作用：静态链接，所有代码编入一个可执行文件。
不用的后果：动态链接需管理 LD_LIBRARY_PATH，AFL++ fork server
            对动态加载有时序问题。

Patch: CRC removal (nocrc.patch)
作用：让 png_crc_finish() 直接返回 0，跳过所有 chunk 的 CRC 校验。
不用的后果：几乎所有变异都被 CRC 检查拦截。关键 chunk（IHDR, IDAT,
            PLTE）CRC 不匹配时 libpng 调 png_error() 直接退出；
            辅助 chunk CRC 不匹配时数据被丢弃。两种情况下变异的
            数据都不会被处理。Path discovery 快速停滞。
对 path discovery 的影响：不打补丁时 5 分钟发现约 50 edges；
                         打补丁后 30 分钟发现 [你的数据] edges。
```

## A-6：写 Q4 — Campaign Analysis

### 内容框架

```
Metrics (see Figure A.1 and A.2):
  Run time:     [数据]
  Stability:    [数据]%
  Corpus count: [数据]
  Map density:  [数据]% / [数据]%
  Cycles done:  [数据]
  Exec speed:   [数据]/sec
  Crashes:      [数据]

Edges curve shape:
  0–5 min:   rapid growth — fuzzer quickly finds basic chunk-handling
             paths from the diverse seed corpus
  5–15 min:  diminishing growth — most "easy" paths discovered,
             now relying on deeper mutations
  15–30 min: near-plateau — [分析你的具体曲线]

Saturation argument:
  [如果 cycles done ≥ 3 且 last new find 很久前 → 接近饱和]
  [如果曲线仍在上升 → 未饱和，更长时间可能发现更多]
```

---

# 成员 B — Harness 开发 + Q1 + Q5

## B 的职责

你负责项目最核心的代码。`harness.c` 和 `harness_persistent.c` 已经写好
（见 `src/` 目录），你的任务是：
1. **理解**每一行代码的目的（Q1 要你完整解释）
2. **测试**它们能在 A 的 Docker 环境中正常工作
3. **做 crash triage**（Q5）
4. **撰写** Q1 和 Q5

---

## B-1：理解 libpng API 调用流程

```
png_create_read_struct()    创建主状态对象
        ↓
png_create_info_struct()    创建元信息对象
        ↓
setjmp(png_jmpbuf(png))    设置错误处理跳转点
        ↓
png_init_io(png, fp)        设置输入源
        ↓
png_read_info(png, info)    读取所有元信息 chunk
        ↓                    (IHDR → PLTE → gAMA → tEXt → ...)
png_set_expand()            ┐
png_set_strip_16()          ├ 设置图像变换
png_set_gray_to_rgb()       ┘
        ↓
png_read_update_info()      应用变换设置
        ↓
png_read_image(png, rows)   解压 IDAT → 反滤波 → 写入像素缓冲区
        ↓
png_read_end(png, NULL)     读取 IDAT 后面的 chunk
        ↓
png_destroy_read_struct()   释放所有资源
```

### setjmp/longjmp 错误处理（重点理解）

libpng 不用返回值报错——它用 setjmp/longjmp（C 语言的"异常"机制）。

`setjmp(png_jmpbuf(png))` 保存当前执行上下文，第一次返回 0。
如果之后 libpng 内部调用 `png_error()`（通过 `longjmp`），
执行会跳回 `setjmp` 处，这次返回非零值。

**没有 setjmp 的后果：** libpng 出错时 longjmp 无目标 → 程序 abort →
AFL++ 把每个畸形输入都记为 crash → 几千个假阳性，真正的 bug 被淹没。

## B-2：验证 Harness

在 A 的 Docker 容器中：

```bash
# 标准 harness
./png_fuzz seeds/rgb_8x8.png
echo $?  # 应该是 0

# Persistent harness（直接运行不会循环，正常退出即可）
echo "test" | ./png_fuzz_persistent
echo $?  # 应该是 0 或 1（没有有效 PNG 输入），但不应 segfault
```

**Commit：** `git commit -m "test: verify both harnesses work on valid PNGs"`

## B-3：做 Crash Triage

等 A 的 campaign 跑了一段时间后，`findings/default/crashes/` 里应该有文件。

### 情况 A：有 crash

```bash
# 1. 列出
ls findings/default/crashes/
# 文件名格式：id:000000,sig:06,src:000123,op:havoc,...
# sig:06 = SIGABRT（ASan 检测到的错误）
# sig:11 = SIGSEGV（段错误）

# 2. 复现
./png_fuzz findings/default/crashes/id:000000*
# 应该看到 ASan 报告

# 3. 最小化
afl-tmin -i findings/default/crashes/id:000000* \
         -o minimized.png -- ./png_fuzz @@
# afl-tmin 反复尝试删除字节，保留能触发同一 crash 的最小输入

# 4. 详细 ASan 堆栈
ASAN_OPTIONS=symbolize=1 ./png_fuzz minimized.png
# 输出类似：
# ==PID==ERROR: AddressSanitizer: null-deref on address 0x...
#     #0 0x... in png_set_text_2 pngset.c:953
#     #1 0x... in png_read_end pngread.c:572
#     #2 0x... in main harness.c:78
```

### 情况 B：没有 crash（准备 synthetic bug）

```bash
# 复制 harness 并注入一个故意的越界写入
cp src/harness.c src/harness_synth.c
```

在 `src/harness_synth.c` 的 `png_read_info` 之后加：

```c
    /* SYNTHETIC BUG: intentional off-by-one for setup validation */
    png_byte ct = png_get_color_type(png, info);
    if (ct == PNG_COLOR_TYPE_PALETTE) {
        char small_buf[4];
        small_buf[10] = 'X';  /* heap-buffer-overflow */
    }
```

```bash
afl-clang-fast src/harness_synth.c \
    -I./libpng-1.2.56/install/include \
    -L./libpng-1.2.56/install/lib \
    -lpng12 -lz -lm -fsanitize=address -g -O1 \
    -o png_fuzz_synth

timeout 60 afl-fuzz -i seeds -o findings_synth -x png.dict \
    -- ./png_fuzz_synth @@

ls findings_synth/default/crashes/  # 应该有 crash
```

**Commit：** `git commit -m "data: crash triage results"`

## B-4：写 Q1 — Harness Design

### 内容框架

```
Entry point: png_read_image()
理由：
  1. 解码器处理不可信外部输入（来自网络的 PNG）
  2. 历史 CVE 集中在解码路径（CVE-2015-8126, CVE-2016-10087,
     CVE-2011-3048）
  3. 解析流水线最复杂：签名→chunk→zlib→滤波→变换→像素输出

考虑过的替代入口：
  - png_write_image（编码器）：输入是结构化内存数据，不是文件，
    harness 难以生成有意义的变异
  - png_image_begin_read_from_memory（简化 API）：只有 1.6.0+，
    1.2.56 没有此函数
  - png_process_data（progressive/incremental API）：有独立的
    状态机（pngpread.c），值得 fuzz 但 harness 更复杂
  - png_read_chunk_header（低层 chunk API）：太底层，真实应用
    不直接调用

Data flow:
  AFL++ 生成变异文件 → argv[1] → fopen("rb")
    → png_init_io(png, fp)
    → png_read_info(png, info)  [解析 IHDR→PLTE→ancillary]
    → png_set_expand/strip_16/gray_to_rgb  [启用变换]
    → png_read_update_info(png, info)
    → png_read_image(png, row_pointers)  [IDAT 解压→反滤波→变换]
    → png_read_end(png, NULL)  [读 post-IDAT chunks]
    → 释放所有内存 → return 0

Guards:
  setjmp(png_jmpbuf(png)) — 防止 longjmp 导致的假阳性 crash
  if (width > MAX_DIM)    — 防止 OOM（fuzzer 可能生成 65535×65535）
  if (!row_pointers)      — ASan 下内存更紧，malloc 可能失败
  return 0 in all paths   — 告诉 AFL++ 这不是 crash
```

## B-5：写 Q5 — Crash Triage

### 内容框架

```
如果有 crash：
  Our campaign found [N] crashes in [time] minutes.

  Triage of crash id:000000:
    1. Reproduction: ./png_fuzz <crash_file> → [输出]
    2. Minimization: afl-tmin → original [X] bytes → minimized [Y] bytes
    3. ASan trace:   [贴完整输出]
    4. Root cause:   Bug type = [heap-buffer-overflow / null-deref / ...]
                     Location = [file:line, e.g. pngset.c:953]
                     CVE = [如果匹配, e.g. CVE-2016-10087]
                     Description = [解释 bug 是什么]

如果没 crash：
  No crashes in 30 minutes. To validate setup, we injected a synthetic
  off-by-one write in the palette handling path. AFL++ detected it in
  [X] seconds (see Figure A.X). This confirms the instrumentation and
  ASan are working correctly. No real bugs were found likely because
  [论证: 已知 CVE 在此版本已修复 / 需要更长时间 / 等等].
```

---

# 成员 C — Seeds/Dictionary + Q3 + Q7

## C 的职责

你负责 fuzzing 的"弹药"（种子+字典）和 QEMU 黑盒 campaign。
撰写 Q3（seeds & dictionary）和 Q7（QEMU 对比）。

---

## C-1：生成多样化种子

### 目的

AFL++ 从种子出发做变异。种子越多样，fuzzer 一开始就能覆盖更多代码路径。

### PNG 的关键维度

| 维度 | 值 | 走哪些代码 |
|------|----|-----------|
| Color type | 0=灰度 2=RGB 3=palette 4=灰度+α 6=RGBA | 解码器根据此分支 |
| Bit depth | 1,2,4,8,16 | bit-shifting 逻辑 |
| Interlace | 0=无 1=Adam7 | 完全独立的解码循环 |

### 操作

```bash
# 在 Docker 容器内
make seeds
# 或手动：
convert -size 8x8 xc:red               png24:seeds/rgb_8x8.png
convert -size 8x8 -colorspace Gray xc:gray     seeds/gray_8x8.png
convert -size 8x8 -type Palette xc:red          seeds/palette_8x8.png
convert -size 8x8 xc:'rgba(255,0,0,0.5)'       seeds/alpha_8x8.png
convert -size 8x8 -colorspace Gray xc:'graya(50%,0.5)' seeds/grayalpha_8x8.png
convert -size 4x4 -depth 16 xc:blue             seeds/16bit_4x4.png
convert -size 8x8 -interlace PNG xc:green        seeds/interlaced_8x8.png
convert -size 8x8 xc:white -set Comment "fuzz"   seeds/text_8x8.png

# 精简（去掉覆盖率冗余的种子）
afl-cmin -i seeds/ -o seeds_min/ -- ./png_fuzz @@
rm -rf seeds_bak && mv seeds seeds_bak && mv seeds_min seeds
```

**种子要小的原因：** AFL++ 每次迭代要复制+变异+写入种子。100KB 种子 vs 200B 种子
在 I/O 上差 500 倍。经验法则：每个 < 1KB。

**Commit：** `git commit -m "feat: diverse seed corpus (8 variants)"`

## C-2：准备字典

```bash
cp /AFLplusplus/dictionaries/png.dict /work/png.dict
cat png.dict
```

字典内容（每行是一个 token）：

```
header_png="\x89PNG\x0d\x0a\x1a\x0a"    ← PNG 签名
section_IHDR="IHDR"                       ← chunk 类型名
section_PLTE="PLTE"
section_IDAT="IDAT"
section_IEND="IEND"
section_gAMA="gAMA"
section_tEXt="tEXt"
...
```

### 形式语言理论解释（Q3 必须包含）

PNG 格式可用上下文无关文法描述：

```
PNG   → Signature ChunkList IEND
Chunk → Length Type Data CRC
Type  → "IHDR" | "PLTE" | "IDAT" | "gAMA" | "tEXt" | ...
```

字典条目 = 文法的**终结符（terminals）**。没有字典，fuzzer 靠随机 bit flip
发现 `PLTE`（4 字节）的概率 ≈ 1/2³²。有了字典，AFL++ 的 dictionary 变异
策略直接插入这些 token，极大加速 chunk-handling 路径的发现。

## C-3：跑 QEMU Campaign

```bash
make fuzz-qemu
# 即: afl-fuzz -Q -i seeds -o findings-qemu -x png.dict -- ./png_fuzz_qemu @@
```

跑≥30min。截图 status screen，记录 exec speed / edges / corpus / stability。

```bash
afl-plot findings-qemu/default/ plot_output_qemu/
```

**Commit：** `git commit -m "data: QEMU campaign results (30min)"`

## C-4：写 Q3 — Seeds and Dictionary

### 内容框架

```
Seeds:
  [N] diverse seeds covering color types 0/2/3/4/6, bit depths 8/16,
  interlace none/Adam7. After afl-cmin: [M] retained.

  Diversity rationale: each seed exercises different decode branches
  (palette seed → png_do_expand palette→RGB conversion;
   interlaced seed → png_do_read_interlace Adam7 loop).

Dictionary:
  [N] entries = PNG chunk type names + file signature.
  In CFG terms, these are terminal symbols of the PNG grammar.
  Without dictionary: P(discovering "PLTE" by bit flip) ≈ 2^{-32}.

Quantitative (from A's status screen, Figure A.1):
  | Strategy   | New paths | Total execs |
  |------------|-----------|-------------|
  | dictionary | [数据]    | [数据]      |
  | havoc      | [数据]    | [数据]      |
  | splice     | [数据]    | [数据]      |

  Dictionary contributed [X] paths — format-aware tokens dramatically
  accelerate path discovery vs. random havoc mutations.
```

## C-5：写 Q7 — QEMU Comparison

### 内容框架

```
Side-by-side (at 30 min wall-clock):
  | Metric          | Instrumented (A) | QEMU -Q (C) | Ratio |
  |-----------------|------------------|-------------|-------|
  | Exec speed      | [A的数据]/sec    | [你的]/sec  | [X]x  |
  | Edges discovered| [A的数据]        | [你的]      | [X]x  |
  | Corpus count    | [A的数据]        | [你的]      | [X]x  |
  | Crashes found   | [A的数据]        | [你的]      | —     |

Why slower:
  QEMU user-mode emulation translates each basic block via JIT at
  first encounter. Coverage tracking is injected into translated
  blocks at the block level (vs. edge level for compile-time).

  Three sources of overhead:
  1. First-encounter translation cost for each basic block
  2. Translated code quality inferior to native compilation
  3. No ASan — only signal-based crash detection (SIGSEGV, SIGABRT).
     Silent memory corruption goes unnoticed.

Why fewer edges:
  Lower exec speed → fewer total iterations in same wall-clock time
  → smaller mutation search space explored.
```

---

# 成员 D — 性能分析 + Q6 + Q8

## D 的职责

你是新加入的第4位成员。你负责性能维度（Q8）和安全分析维度（Q6），
以及报告框架和附录整理。

---

## D-1：Phase 1 准备工作

Day 1–2 环境还没搭好时，你做以下准备：

### 1. 研究 libpng CVE 历史（Q6 需要）

关键 CVE：
- **CVE-2015-8126**: `png_set_PLTE()` 和 `png_get_PLTE()` 中的
  buffer overflow。当 IHDR 声明小 bit_depth 但 PLTE 条目超出时，
  palette 展开写越界。影响 libpng < 1.2.54。
- **CVE-2016-10087**: `png_set_text_2()` 中的 NULL 指针解引用。
  存在于 libpng 1.2.56。
- **CVE-2011-3048**: 整数溢出导致堆溢出。

### 2. 研究真实应用（Q6 需要）

使用 libpng 的应用：
- **Chromium/Chrome**: 浏览器内置 PNG 解码
- **GIMP**: 图像编辑器的 PNG 读写
- **Firefox**: 浏览器 PNG 解码
- **ImageMagick**: 命令行图像处理

### 3. 准备 LaTeX 模板

`report.tex` 已经写好，你的任务是确保它能编译：

```bash
# 需要 usenix-2020-09.sty 文件
# 可以从 https://www.usenix.org/conferences/author-resources/paper-templates 下载
pdflatex report.tex
```

**Commit：** `git commit -m "docs: report.tex template with all Q sections"`

## D-2：跑性能对比实验

每种配置跑 2–3 分钟，等 exec speed 稳定后记录数值。

```bash
# 配置 1: No ASan + fork mode
afl-fuzz -i seeds -o /tmp/bench_nosan -x png.dict -- ./png_fuzz_nosan @@
# 等稳定后记录 exec speed: ____/sec → Ctrl+C

# 配置 2: ASan + fork mode
afl-fuzz -i seeds -o /tmp/bench_asan -x png.dict -- ./png_fuzz @@
# 记录: ____/sec → Ctrl+C

# 配置 3: ASan + persistent mode
afl-fuzz -i seeds -o /tmp/bench_persist -x png.dict -- ./png_fuzz_persistent @@
# 记录: ____/sec → Ctrl+C
```

### 记录 Edge Count

```bash
# 方法: 启动 afl-fuzz 时看 status screen 中的 "map size"
# 这就是 harness binary 的总 instrumented edges

# 对比 library-only: 用 AFL_DEBUG=1 编译 libpng 源文件
AFL_DEBUG=1 afl-clang-fast -c \
    libpng-1.2.56/png.c libpng-1.2.56/pngread.c \
    libpng-1.2.56/pngrutil.c libpng-1.2.56/pngget.c \
    libpng-1.2.56/pngset.c libpng-1.2.56/pngtrans.c \
    libpng-1.2.56/pngmem.c libpng-1.2.56/pngerror.c \
    libpng-1.2.56/pngpread.c libpng-1.2.56/pngrtran.c \
    -I./libpng-1.2.56/install/include -g -O1 2>&1 | grep -i instrum
# 输出类似: [+] Instrumented 2847 locations
```

**Commit：** `git commit -m "data: performance benchmarks (3 configs + edge counts)"`

## D-3：写 Q6 — Attack Surface Analysis

### 内容框架

```
Real-world applications:

1. Chromium / Google Chrome
   Uses libpng to decode PNG images in web pages. Every <img> tag
   pointing to a PNG triggers the decode pipeline.

2. GIMP (GNU Image Manipulation Program)
   Uses libpng for both reading and writing PNG files.

Attack scenario:
  A user visits a malicious web page containing a crafted PNG image.
  The browser's image decoder (backed by libpng) processes the
  image, triggering a heap-buffer-overflow in the palette expansion
  code (similar to CVE-2015-8126). The attacker achieves an
  arbitrary write primitive, escalating to remote code execution
  within the browser's renderer process sandbox.

Code paths NOT exercised by our harness:

1. Progressive/incremental decoding
   (png_process_data() in pngpread.c)
   Browsers load images progressively over the network using
   libpng's push API. This has its own state machine with
   independent parsing logic. Functions: png_push_read_chunk(),
   png_push_have_row() — never called by our harness.

2. Encoding path
   (png_write_info/image/end() in pngwrite.c, pngwutil.c)
   GIMP's "Export as PNG" uses the encoder. If an application passes
   untrusted dimensions to the encoder, integer overflows in
   png_write_row() could cause buffer overflows. Our harness only
   fuzzes decoding.
```

## D-4：写 Q8 — Instrumentation Depth and Performance

### 内容框架

```
Part 1 — Edge counts:
  | Target                | Instrumented edges |
  |-----------------------|-------------------|
  | libpng library alone  | [你的数据]        |
  | Final harness binary  | [你的数据]        |

  Difference ([差值] edges) comes from:
  1. harness.c itself: main(), error-handling branches (~[估计])
  2. zlib decompression (statically linked): (~[估计])
  3. libc functions pulled in by static linking: (~[估计])

  Map density: campaign reached [A的数据]%, meaning [计算] out of
  [total] edges were hit. Remaining [100-X]% unreached because:
  - Rare code paths (e.g. 16-bit gray + Adam7 + specific filter)
  - Error-handling branches for very specific malformed inputs
  - Unreachable code in statically-linked zlib/libc

Part 2 — Performance comparison:
  | Configuration         | Exec speed | Relative |
  |-----------------------|-----------|----------|
  | No ASan + fork mode   | [数据]/s  | 1.00x    |
  | ASan + fork mode      | [数据]/s  | [计算]x  |
  | ASan + persistent     | [数据]/s  | [计算]x  |

  No ASan → ASan slowdown:
  ASan instruments every memory load/store with shadow memory check:
    shadow = *(addr >> 3 + offset)
    if (shadow != 0) → report
  This adds ~2x CPU overhead + ~2x memory (shadow + redzones).

  Fork → persistent speedup:
  Fork mode: each iteration = fork() + exec + exit.
  fork() copies page tables, duplicates file descriptors, triggers
  kernel context switch.
  Persistent mode: loops N=10000 times inside same process, reads
  input from shared memory (__AFL_FUZZ_TESTCASE_BUF) instead of
  file. Fork cost amortised over 10000 iterations.
  Additional speedup: CPU instruction/data caches stay "hot".
```

## D-5：整理附录

收集所有人的截图和图表，统一编号：

| Figure | 内容 | 来源 |
|--------|------|------|
| A.1 | Instrumented status screen | A 提供 |
| A.2 | Instrumented edges.png | A 的 plot_output/ |
| A.3 | QEMU status screen | C 提供 |
| A.4 | QEMU edges.png | C 的 plot_output_qemu/ |
| A.5 | ASan stack trace | B 提供 |

确保正文中的引用（如 "see Figure A.1"）和附录编号一致。

---

# 共同任务

## Git Commit 规范

作业明确说 "repository history is part of the evaluation"。每完成一步就 commit：

```bash
# 好的 commit message
git commit -m "feat: Dockerfile with 4 harness variants"
git commit -m "feat: standard harness with full decode pipeline"
git commit -m "feat: persistent mode harness with memory reader"
git commit -m "feat: diverse seed corpus (8 PNG variants)"
git commit -m "data: instrumented campaign (32min, 287 corpus)"
git commit -m "data: QEMU campaign (30min)"
git commit -m "data: performance benchmarks"
git commit -m "data: crash triage (CVE-2016-10087)"
git commit -m "docs: Q1 harness design section"
git commit -m "docs: Q2 instrumentation flags"
git commit -m "docs: merge all Q sections into report"
git commit -m "fix: add malloc failure check in harness"

# 坏的（会被扣分）
git commit -m "update"
git commit -m "final"
git commit -m "asdf"
```

## 交叉审阅（Day 7）

| 审阅人 | 审阅 | 重点 |
|--------|------|------|
| A 审 C | Q3, Q7 | Q3 的 yields 数字是否和 A 的 status screen 一致 |
| B 审 D | Q6, Q8 | Q6 的代码路径引用是否准确 |
| C 审 A | Q2, Q4 | Q2 的 flag 解释是否有具体 impact |
| D 审 B | Q1, Q5 | Q1 的 data flow 是否完整 |

## 每日进度表

| 天 | A | B | C | D |
|---|---|---|---|---|
| 1 | Dockerfile | 研究 API, 理解 harness.c | 研究 PNG 格式 | CVE 研究, LaTeX 模板 |
| 2 | Makefile, 验证 4 binaries | 测试 harness, 写 persistent | make seeds, 准备 dict | 帮 A 调试, 完善模板 |
| 3 | 启动 instrumented campaign | 等 crash → 开始 triage | 启动 QEMU campaign | 跑 3 种性能对比 |
| 4 | 继续跑, 截图 | 完成 triage 流程 | 继续跑 QEMU, 截图 | 记录 edge count |
| 5 | 写 Q2 + Q4 | 写 Q1 + Q5 | 写 Q3 + Q7 | 写 Q6 + Q8 |
| 6 | 完成 Q2 + Q4 | 完成 Q1 + Q5 | 完成 Q3 + Q7 | 完成 Q6 + Q8 + 附录 |
| 7 | 审阅 C → 合并 | 审阅 D → 合并 | 审阅 A → 合并 | 审阅 B → 编译 PDF |
