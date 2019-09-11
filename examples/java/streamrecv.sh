#!/bin/bash
java -Djava.library.path=../../lib -cp .:../../lib/norm-1.0.0.jar NormStreamRecv $@ 
