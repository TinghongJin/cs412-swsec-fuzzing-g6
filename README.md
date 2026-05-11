# Software Security (CS-412) - Fuzzing Lab

This repository contains the source code, experimental data, and final report for the fuzzing lab.

## Repository Structure

```text
swsec-fuzzing/
├── Dockerfile
├── Makefile
│
├── changes.patch
├── convert_seeds.sh
├── png.dict
│
├── report.tex
├── report.pdf
│
├── src/
│   ├── harness.c
│   ├── harness_persistent.c
│
├── seeds/
├── seeds_with_config/
│
├── findings/
│   └── default/
│       └── plot_data
│
├── findings-qemu/
│   └── default/
│       └── plot_data
│
├── plot_output/
│   ├── index.html
│   ├── edges.png
│   ├── exec_speed.png
│   ├── high_freq.png
│   └── low_freq.png
│
└── plot_output_qemu/
├── index.html
├── edges.png
├── exec_speed.png
├── high_freq.png
└── low_freq.png
