../src/station-server.py StationA 4001 4002 localhost:4004 &
../src/station-server.py TerminalB 4003 4004 localhost:4002 localhost:4006 localhost:4008 localhost:4010 &
../src/station-server.py JunctionC 4005 4006 localhost:4004 &
../src/station-server.py BusportD 4007 4008 localhost:4004 &
../src/station-server.py StationE 4009 4010 localhost:4004 &
