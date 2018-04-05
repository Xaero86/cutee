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

#include "definition.h"
#include "DClient.h"
#include "DConnexion.h"

void DServer::CreateAndStartDaemonServer(uint16_t p_port, int argc, char** argv)
{
	if (setsid() < 0)
	{
		std::cerr << "Unable to setsid" << std::endl;
		exit(EXIT_FAILURE);
	}

	signal(SIGCHLD, SIG_IGN);
	signal(SIGHUP, SIG_IGN);

	/* Deuxieme fork pour creer un daemon */
	pid_t pid = fork();

	if (pid < 0)
	{
		std::cerr << "Unable to fork" << std::endl;
		exit(EXIT_FAILURE);
	}
	if (pid > 0)
	{
		/* le process pere retourne et stop */
		return;
	}

	signal(SIGPIPE, SIG_IGN);

	/* Modification du umask */
	umask(0);

	/* Modification du repertoire de travail */
	chdir(WORKING_DIRECTORY);

	/* Fermeture des io standard */
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
		daemonServer.openLog();
		daemonServer.eventLoop();
	}
}

DServer::DServer(uint16_t p_port)
	: _port(p_port), _serverSocketFD(-1),
	  _clientMutex(), _connexionMutex(), _logFile()
{
}

DServer::~DServer()
{
	std::list<std::unique_ptr<DClient>>::iterator itClient = _clients.begin();
	while(itClient != _clients.end())
	{
		itClient = _clients.erase(itClient);
	}
	std::list<std::unique_ptr<DConnexion>>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		itConnexion = _connexions.erase(itConnexion);
	}
	if (_serverSocketFD != -1)
	{
		close(_serverSocketFD);
	}
	if (_logFile.is_open())
	{
		_logFile.close();
	}
}

bool DServer::startServer()
{
	_serverSocketFD = socket(AF_INET, SOCK_STREAM, 0);
	if (_serverSocketFD == -1)
	{
		_logFile << "Unable to create server socket" << std::endl;
		return false;
	}
	else
	{
		struct sockaddr_in socketProp;
		socketProp.sin_family = AF_INET;
		socketProp.sin_port = htons(_port);
		socketProp.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		return (0 == bind(_serverSocketFD, (struct sockaddr *) &socketProp, sizeof(socketProp)));
	}
}

void DServer::openLog()
{
	std::stringstream streamLogFileName;
	streamLogFileName << WORKING_DIRECTORY << "/" << SERVER_LOG_FILE;
	std::string logFileName = streamLogFileName.str();
	
        _logFile.open(logFileName, std::fstream::out);
	if (!_logFile.is_open())
	{
		/* Invalidation du fichier de log: on peut toujours ecrire dessus, mais sans effet */
		_logFile.setstate(std::ios_base::badbit);
	}
}

void DServer::eventLoop()
{
	struct sockaddr_in clientSocket;
	int socketStructSize = sizeof(struct sockaddr_in);
	int clientSocketFD;

	if (_serverSocketFD == -1)
	{
		return;
	}

	listen(_serverSocketFD, 3);

	while ((clientSocketFD = accept(_serverSocketFD, (struct sockaddr *)&clientSocket, (socklen_t*)&socketStructSize)) != -1)
	{
		DClient* newClient = new DClient(clientSocketFD, this);
		if (newClient->isValid())
		{
			_clientMutex.lock();
			_clients.push_back(std::unique_ptr<DClient>(newClient));
			_clientMutex.unlock();
		}
		else
		{
			delete newClient;
		}
	}
}

void DServer::connectClient(DClient *p_client)
{
	/* Parcours des connexions existantes pour ajouter le client */
	int clientAdded = 0;
	_connexionMutex.lock();
	std::list<std::unique_ptr<DConnexion>>::iterator itConnexion = _connexions.begin();
	while((itConnexion != _connexions.end()) && (clientAdded == 0))
	{
		clientAdded = ((*itConnexion)->tryAddClient(p_client));
		itConnexion++;
	}
	_connexionMutex.unlock();
	if (clientAdded == -1)
	{
		/* La connexion est deja ouverte vers la cible mais avec d'autres parametres */
		std::stringstream streamMessage;
		streamMessage << "Connection to " << p_client->getLine() << " already opened with uncompatible parameters";
		std::string message = streamMessage.str();
		p_client->sendFatal(message);
	}
	else if (clientAdded == -2)
	{
		/* La ressource n'existe pas, ou les droits sont pas suffisants */
		std::stringstream streamMessage;
		streamMessage << "Unable to connect to " << p_client->getLine();
		std::string message = streamMessage.str();
		p_client->sendFatal(message);
	}
	else if (clientAdded == 0)
	{
		/* Le client n'a pas pu etre ajoute aux connexions existantes */
		/* Creation d'une nouvelle connexion */
		DConnexion* newConnexion;
		if (p_client->isMonitoring())
		{
			newConnexion = new DConnexion(this);
		}
		else
		{
			newConnexion = new DConnexion(this, p_client->getLine(), p_client->getSpeed());
		}
		if (newConnexion->isValid())
		{
			_connexionMutex.lock();
			_connexions.push_back(std::unique_ptr<DConnexion>(newConnexion));
			_connexionMutex.unlock();
			newConnexion->tryAddClient(p_client);
		}
		else
		{
			delete newConnexion;
			std::string message("Fail to create another connexion");
			p_client->sendFatal(message);
		}
	}
}

void DServer::handleDisconnect()
{
	/* Parcours de la liste des connexions pour supprimer les clients invalides */
	_connexionMutex.lock();
	std::list<std::unique_ptr<DConnexion>>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		(*itConnexion)->handleDisconnect();
		itConnexion++;
	}
	_connexionMutex.unlock();
	/* Parcours de la liste des clients pour supprimer ceux invalides */
	_clientMutex.lock();
	std::list<std::unique_ptr<DClient>>::iterator itClient = _clients.begin();
	while(itClient != _clients.end())
	{
		if ((*itClient)->isValid())
		{
			itClient++;
		}
		else
		{
			itClient = _clients.erase(itClient);
		}
	}
	_clientMutex.unlock();
}

void DServer::handleConnexionClosed()
{
	/* Parcours de la liste des connexions pour supprimer celles invalides */
	_connexionMutex.lock();
	std::list<std::unique_ptr<DConnexion>>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		if ((*itConnexion)->isAlive())
		{
			itConnexion++;
		}
		else
		{
			itConnexion = _connexions.erase(itConnexion);
		}
	}
	if (_connexions.size() == 0)
	{
		_logFile << "No more connexion. Stopping daemon" << std::endl;
		shutdown(_serverSocketFD, SHUT_RDWR);
	}
	_connexionMutex.unlock();
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

	_clientMutex.lock();
	std::list<std::unique_ptr<DClient>>::iterator it = _clients.begin();
	while(it != _clients.end())
	{
		if ((*it)->isValid())
		{
			(*it)->sendFatal(message);
		}
		it++;
	}
	_clientMutex.unlock();
}

std::string DServer::getStatus()
{
	std::stringstream streamStatus;
	int nbClientsConnected = 0;

	streamStatus << "Serial daemon: " << _connexions.size() << " connexion(s) opened" << std::endl;

	_connexionMutex.lock();
	std::list<std::unique_ptr<DConnexion>>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		streamStatus << " - " << (*itConnexion)->getLine() << ": ";
		if ((*itConnexion)->isValid())
		{
			streamStatus << (*itConnexion)->getNbClients() << " client(s)" << std::endl;
			nbClientsConnected += (*itConnexion)->getNbClients();
		}
		else
		{
			streamStatus << " closing..." << std::endl;
		}
		itConnexion++;
	}
        _connexionMutex.unlock();

	streamStatus << "Total client(s): " << nbClientsConnected << " connected / " << _clients.size() << std::endl;

	return streamStatus.str();
}

