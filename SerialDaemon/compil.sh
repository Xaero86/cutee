#!/bin/bash

g++ -std=c++11 -o cutee cuteeLauncher.cpp daemon/DServer.cpp daemon/DClient.cpp daemon/DConnexion.cpp appli/AClient.cpp tools/ClientServerComm.cpp -I. -Idaemon -Iappli -Itools -lpthread

