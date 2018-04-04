#ifndef DEFINITION_H
#define DEFINITION_H

/* Elements pour la comm client/serveur */
#define DAEMON_PORT 10963

#define VERSION "1.0"

#define SERVER_NAME "SerialServer"
#define CLIENT_NAME "SerialClient"

#define CLI_SER_BUFFER_SIZE 2048

#define KEY_VERSION "version"
#define KEY_LINE    "line"
#define KEY_SPEED   "speed"
#define KEY_OUTPATH "outputPath"
#define KEY_INPATH  "inputPath"
#define KEY_INFO    "info"
#define KEY_ERROR   "error"
#define KEY_FATAL   "fatal"
#define KEY_HALTSER "serverHalt"

/* General */
#define WORKING_DIRECTORY "/tmp/serialDaemon/"
#define SERVER_LOG_FILE   "serialDaemon.log"

/* Element pour le serveur */
#define INPUT_BUFFER 1024

#endif // DEFINITION_H
