#!/bin/bash
if [ "$1" == "start" ]; then
    gcc src/CarControl.c -o bin/CarControl -lwiiuse -lpigpio -lm -lbluetooth -lrt -lpthread
    bin/CarControl
elif [ "$1" == "demo" ]; then
    gcc src/CarControlDemo.c -o bin/CarControlDemo -lwiiuse -lpigpio -lm -lbluetooth -lrt -lpthread
    bin/CarControlDemo
else
    echo "Usage: $0 {start|demo}"
    echo "  start: Compile and run the main CarControl program."
    echo "  demo: Compile and run the CarControlDemo program."
fi