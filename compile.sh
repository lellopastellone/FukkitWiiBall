#!/bin/bash
gcc src/CarControl.c -o bin/CarControl -lwiiuse -lpigpio -lm -lbluetooth -lrt -lpthread