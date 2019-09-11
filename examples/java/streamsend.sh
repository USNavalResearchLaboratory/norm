#!/bin/bash
# Enable TCP-friendly congestion control with:
# -DNorm.CC=on
java -Djava.library.path=../../lib -cp .:../../lib/norm-1.0.0.jar NormStreamSend $@ 
