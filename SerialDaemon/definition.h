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
#define KEY_MONITOR "monitoring"
#define KEY_USER    "user"
#define KEY_OUTPATH "outputPath"
#define KEY_INPATH  "inputPath"
#define KEY_INFO    "info"
#define KEY_ERROR   "error"
#define KEY_FATAL   "fatal"

/* General */
#define WORKING_DIRECTORY "/tmp/serialDaemon/"
#define SERVER_LOG_FILE   "serialDaemon.log"

#define FIFO_BUFFER 2048

/* Liste des versions de serveur incompatible avec ce client */
#define NB_INCOMPATIBLE_SERVER 0
extern const char *G_IncompatibleServer[NB_INCOMPATIBLE_SERVER];
/* Liste des versions de client incompatible avec ce serveur */
#define NB_INCOMPATIBLE_CLIENT 0
extern const char *G_IncompatibleClient[NB_INCOMPATIBLE_CLIENT];

#endif // DEFINITION_H
