# Software Security (CS-412) - Fuzzing Lab

This repository contains the source code, experimental data, and final report for the fuzzing lab.

## Repository Structure

```text
swsec-fuzzing/
├── Dockerfile   
├── Makefile     
├── report.tex
├── report.pdf
│
├── src/
│   └── harness.c
│
├── patches/
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
```

## Trigger crash


```sh
docker build -t png-fuzz .
docker run -it --rm png-fuzz
make png_fuzz_crash
./png_fuzz_crash crash/id\:000000\,sig\:06\,src\:000001\,time\:2413\,execs\:840\,op\:\(null\)\,pos\:0 
```

## Usual run 

```sh
docker build -t png-fuzz .
docker run -it --rm png-fuzz
make name_of_fuzzer [fuzz,fuzz-qemu,fuzz-nosan,fuzz-persistent]
```


