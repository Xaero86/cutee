#ifndef DEFINITION_H
#define DEFINITION_H

#define DAEMON_PORT 10963

#define VERSION "1.0"

#define SERVER_NAME "SerialServer"
#define CLIENT_NAME "SerialClient"

#define BUFFER_SIZE 2048

#define KEY_VERSION "version"
#define KEY_PARAM   "parameters"
#define KEY_OUTPATH "outputPath"
#define KEY_INPATH  "inputPath"
#define KEY_INFO    "info"
#define KEY_ERROR   "error"
#define KEY_FATAL   "fatal"
#define KEY_HALTSER "serverHalt"

#define WORKING_DIRECTORY "/tmp/serialDaemon/"
#define SEVER_LOG_FILE    "serialDaemon.log"

#define DUMMY_CONNEC_MSG "Connected to dummy connexion..."

#endif // DEFINITION_H
