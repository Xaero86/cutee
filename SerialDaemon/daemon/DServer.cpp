#include "DServer.h"

#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

#include <iostream>
#include <sstream>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <netinet/in.h>

#include <errno.h>

#include "DClient.h"

void DServer::CreateAndStartDaemonServer(uint16_t p_port, int argc, char** argv)
{
	if (setsid() < 0)
	{
		std::cerr << "Failed to setsid" << std::endl;
		exit(EXIT_FAILURE);
	}

	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Deuxieme fork pour creer un daemon */
	pid_t pid = fork();

	if (pid < 0)
	{
		std::cerr << "Failed to fork" << std::endl;
		exit(EXIT_FAILURE);
	}
	if (pid > 0)
	{
		/* le process pere retourne et stop */
		return;
	}

	/* Set new file permissions */
	umask(0);

	/* Change the working directory to the root directory */
	chdir("/");

	/* Close all open file descriptors */
	for (int x = sysconf(_SC_OPEN_MAX); x>=0; x--)
	{
		close(x);
	}

	/* Change le nom du processus pour ps */
	const char* daemonProcName = "serialDaemon";
	int commandLength = strlen(argv[0]);
	strncpy(argv[0], daemonProcName, commandLength);
	/* Suppression de tous les parametres */
	for (int i = 1; i < argc; i++)
	{
		memset(argv[i], '\0', strlen(argv[i]));
	}
	/* Change le nom du processus pour top */
	prctl(PR_SET_NAME, daemonProcName, nullptr, nullptr, nullptr);

	DServer daemonServer(p_port);
	if (daemonServer.startServer())
	{
		daemonServer.eventLoop();
	}
}

DServer::DServer(uint16_t p_port)
	: _port(p_port), _serverSocketFD(-1)
{
	_logFile.open("/tmp/serialDaemon.log", std::fstream::out | std::fstream::app);
	if (!_logFile.is_open())
	{
		exit(-1);
	}
}

DServer::~DServer()
{
	if (_serverSocketFD != -1)
	{
		std::list<DClient*>::iterator it = _clients.begin();
		while(it != _clients.end())
		{
			it = _clients.erase(it);
		}

		close(_serverSocketFD);
	}
	if (_logFile.is_open())
	{
		_logFile.close();
	}
}

bool DServer::startServer()
{
	int readyCheck = -1;
	if (_serverSocketFD == -1)
	{
		_serverSocketFD = socket(AF_INET, SOCK_STREAM, 0);
		if (_serverSocketFD < 0)
		{
			_logFile << "Unable to create server socket" << std::endl;
		}
		else
		{
			struct sockaddr_in socketProp;
			socketProp.sin_family = AF_INET;
			socketProp.sin_port = htons(_port);
			socketProp.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			readyCheck = bind(_serverSocketFD, (struct sockaddr *) &socketProp, sizeof(socketProp));
		}
	}
	return (_serverSocketFD != -1 && readyCheck == 0);
}

void DServer::eventLoop()
{
	struct sockaddr_in clientSocket;
	int socketStructSize = sizeof(struct sockaddr_in);
	int clientSocketFD;

	if (_serverSocketFD < 0)
	{
		return;
	}

	listen(_serverSocketFD, 3);

	while ((clientSocketFD = accept(_serverSocketFD, (struct sockaddr *)&clientSocket, (socklen_t*)&socketStructSize)) != -1)
	{
		DClient* newClient = new DClient(clientSocketFD, this);
		if (newClient->isValid())
		{
			_clients.push_back(newClient);
		}
		else
		{
			delete newClient;
		}
	}
}

void DServer::handleDisconnect()
{
	std::list<DClient*>::iterator it = _clients.begin();
	while(it != _clients.end())
	{
		if ((*it)->isValid())
		{
			it++;
		}
		else
		{
			it = _clients.erase(it);
		}
	}
	if (_clients.size() == 0)
	{
		shutdown(_serverSocketFD, SHUT_RDWR);
		_logFile << "No more client. Stopping daemon" << std::endl;		
	}
}

bool DServer::halt(std::string &p_cause)
{
	std::string message = "";

	if (p_cause.empty())
	{
		message = "Rupture. Server shut down.";
	}
	else
	{
		std::stringstream streamMessage;
		streamMessage << "Rupture. Server shut down by " << p_cause;
		message = streamMessage.str();
	}

	std::list<DClient*>::iterator it = _clients.begin();
	while(it != _clients.end())
	{
		if ((*it)->isValid())
		{
			(*it)->sendFatal(message);
		}
		it++;
	}
}

