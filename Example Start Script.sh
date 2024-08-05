#!/bin/bash

python3 testing.py TerminalA 4001 4002 localhost:4004 localhost:4006 &
./test JunctionB 4003 4004 localhost:4002 &
./test BusportC 4005 4006 localhost:4002 localhost:4008 &
python3 testing.py StationD 4007 4008 localhost:4006 &
