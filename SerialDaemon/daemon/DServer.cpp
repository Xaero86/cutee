#include "DServer.h"

#include <unistd.h>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <cerrno>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <netinet/in.h>


#include "definition.h"
#include "DClient.h"
#include "DConnexion.h"

DServer *DServer::G_ServerInstance = NULL;

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
	prctl(PR_SET_NAME, daemonProcName, NULL, NULL, NULL);

	DServer daemonServer(p_port);
	if (daemonServer.startServer())
	{
		G_ServerInstance = &daemonServer;
		/* Nettoyage des ressources sur kill */
		signal(SIGINT,  SignalHandler);
		/* SIGPIPE traite sur read et write, pas sur signal */
		signal(SIGPIPE, SIG_IGN);

		daemonServer.openLog();
		daemonServer.eventLoop();
		G_ServerInstance = NULL;
	}
}

void DServer::SignalHandler(int p_signo)
{
	if (G_ServerInstance == NULL)
	{
		exit(-1);
	}
	if (p_signo == SIGINT)
	{
		/* En cas de kill sur le serveur, on essaye de l'arreter proprement: nettoyage des ressources */
		/* Si ca bloque faire un kill -9 */
		G_ServerInstance->halt();
	}
}

DServer::DServer(uint16_t p_port)
	: _port(p_port), _serverSocketFD(-1), _logFile()
{
	pthread_mutex_init(&_clientMutex, NULL);
	pthread_mutex_init(&_connexionMutex, NULL);
}

DServer::~DServer()
{
	/* Pas de protection par mutex ici: pour ne pas bloquer l'arret si probleme */
	std::list<DClient*>::iterator itClient = _clients.begin();
	while(itClient != _clients.end())
	{
		delete *itClient;
		itClient = _clients.erase(itClient);
	}
	std::list<DConnexion*>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		delete *itConnexion;
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
		/* On peut pas logger ici: stderr deja ferme et le fichier de log pas encore ouvert. */
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
	
        _logFile.open(logFileName.c_str(), std::fstream::out);
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

	listen(_serverSocketFD, 3);

	while ((clientSocketFD = accept(_serverSocketFD, (struct sockaddr *)&clientSocket, (socklen_t*)&socketStructSize)) != -1)
	{
		DClient* newClient = new DClient(clientSocketFD, this);
		if (newClient->isValid())
		{
			pthread_mutex_lock(&_clientMutex);
			_clients.push_back(newClient);
			pthread_mutex_unlock(&_clientMutex);
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
	int clientAdded = DCON_CLIENT_NOT_ADDED;
	pthread_mutex_lock(&_connexionMutex);
	std::list<DConnexion*>::iterator itConnexion = _connexions.begin();
	while((itConnexion != _connexions.end()) && (clientAdded == DCON_CLIENT_NOT_ADDED))
	{
		clientAdded = ((*itConnexion)->tryAddClient(p_client));
		itConnexion++;
	}
	pthread_mutex_unlock(&_connexionMutex);
	if (clientAdded == DCON_INCOMPAT_CONF)
	{
		/* La connexion est deja ouverte vers la cible mais avec d'autres parametres */
		std::stringstream streamMessage;
		streamMessage << "Connection to " << p_client->getLine() << " already opened with uncompatible parameters";
		std::string message = streamMessage.str();
		p_client->sendFatal(message);
	}
	else if (clientAdded == DCON_INVALID_RESS)
	{
		/* La ressource n'existe pas, ou les droits sont pas suffisants */
		std::stringstream streamMessage;
		streamMessage << "Unable to connect to " << p_client->getLine();
		std::string message = streamMessage.str();
		p_client->sendFatal(message);
	}
	else if (clientAdded == DCON_USER_CONNECTED)
	{
		/* Connexion limitee a 1 par utilisateur et l'utilisateur deja connecte */
		std::stringstream streamMessage;
		streamMessage << "User " << p_client->getUser() << " already to connect to " << p_client->getLine();
		std::string message = streamMessage.str();
		p_client->sendFatal(message);
	}
	else if (clientAdded == DCON_CLIENT_NOT_ADDED)
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
			pthread_mutex_lock(&_connexionMutex);
			_connexions.push_back(newConnexion);
			pthread_mutex_unlock(&_connexionMutex);
			newConnexion->tryAddClient(p_client);
		}
		else
		{
			/* Impossible de se connecter */
			delete newConnexion;
			std::stringstream streamMessage;
			streamMessage << "Unable to connect to " << p_client->getLine();
			std::string message = streamMessage.str();
			p_client->sendFatal(message);
		}
	}
}

void DServer::handleDisconnect()
{
	/* Parcours de la liste des connexions pour supprimer les clients invalides */
	pthread_mutex_lock(&_connexionMutex);
	std::list<DConnexion*>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		(*itConnexion)->handleDisconnect();
		itConnexion++;
	}
	pthread_mutex_unlock(&_connexionMutex);
	/* Parcours de la liste des clients pour supprimer ceux invalides */
	pthread_mutex_lock(&_clientMutex);
	std::list<DClient*>::iterator itClient = _clients.begin();
	while(itClient != _clients.end())
	{
		if ((*itClient)->isValid())
		{
			itClient++;
		}
		else
		{
			delete *itClient;
			itClient = _clients.erase(itClient);
		}
	}
	pthread_mutex_unlock(&_clientMutex);
}

void DServer::handleConnexionClosed()
{
	/* Parcours de la liste des connexions pour supprimer celles invalides */
	pthread_mutex_lock(&_connexionMutex);
	std::list<DConnexion*>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		if ((*itConnexion)->isAlive())
		{
			itConnexion++;
		}
		else
		{
			delete *itConnexion;
			itConnexion = _connexions.erase(itConnexion);
		}
	}
	if (_connexions.size() == 0)
	{
		_logFile << "No more connexion. Stopping daemon" << std::endl;
		shutdown(_serverSocketFD, SHUT_RDWR);
	}
	pthread_mutex_unlock(&_connexionMutex);
}

bool DServer::halt(std::string p_cause)
{
	/* Fermeture forcee du serveur: on demande a chaque client de se deconnecter */
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

	pthread_mutex_lock(&_clientMutex);
	std::list<DClient*>::iterator it = _clients.begin();
	while(it != _clients.end())
	{
		if ((*it)->isValid())
		{
			(*it)->sendFatal(message);
		}
		it++;
	}
	pthread_mutex_unlock(&_clientMutex);
}

std::string DServer::getStatus()
{
	std::stringstream streamStatus;
	int nbClientsConnected = 0;

	streamStatus << "Serial daemon: " << _connexions.size() << " connexion(s) opened" << std::endl;

	pthread_mutex_lock(&_connexionMutex);
	std::list<DConnexion*>::iterator itConnexion = _connexions.begin();
	while(itConnexion != _connexions.end())
	{
		streamStatus << " - " << (*itConnexion)->getStatus();
		nbClientsConnected += (*itConnexion)->getNbClients();
		itConnexion++;
	}
	pthread_mutex_unlock(&_connexionMutex);

	streamStatus << "Total client(s): " << nbClientsConnected << " / " << _clients.size() << " connected" << std::endl;

	return streamStatus.str();
}

