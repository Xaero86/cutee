#!/bin/bash

g++ -std=c++11 -o connectSerial connectSerial.cpp daemon/DServer.cpp daemon/DClient.cpp appli/AClient.cpp tools/ClientServerComm.cpp -I. -Idaemon -Iappli -Itools -lpthread

