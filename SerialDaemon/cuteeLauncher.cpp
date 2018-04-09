#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <csignal>

#include <sys/types.h>
#include <sys/stat.h>

#include "definition.h"
#include "DServer.h"
#include "AClient.h"

static void usage();
static void version();

int main(int argc, char** argv)
{
	pid_t pid;
	int opt;
	std::string line;
	std::string speed;
	bool monitoring = false;
	bool onePerUser = false;

	while ((opt = getopt(argc, argv, "s:l:muv?")) != -1)
	{
		switch (opt)
		{
			case 'l':
				line = std::string(optarg);
				break;
			case 's':
				speed = std::string(optarg);
				break;
			case 'm':
				monitoring = true;
				break;
			case 'u':
				onePerUser = true;
				break;
			case 'v':
				version();
				return EXIT_SUCCESS;
				break;
			case '?':
				usage();
				return EXIT_SUCCESS;
				break;
		}
	}

	if (!monitoring && (line.empty() || speed.empty()))
	{
		std::cout << "Invalid parameters" << std::endl << std::endl;
		usage();
		exit(EXIT_FAILURE);
	}

	/* Creation du repertoire de travail: mandatory */
	/* on rajoute le "/" pour etre sur que c'est un repertoire */
	if (access(WORKING_DIRECTORY "/", R_OK | W_OK | X_OK) == -1)
	{
		if (errno != ENOENT)
		{
			std::cerr << "Unable to access working directory: " << WORKING_DIRECTORY << std::endl;
			exit(EXIT_FAILURE);
		}
		else if (mkdir(WORKING_DIRECTORY, R_OK | W_OK | X_OK) == -1)
		{
			std::cerr << "Unable to create working directory: " << WORKING_DIRECTORY << std::endl;
			exit(EXIT_FAILURE);
		}
		/* A cause du umask, on est oblige de rajouter les droits en deux temps */
		else if (chmod(WORKING_DIRECTORY, S_IRWXU | S_IRWXG | S_IRWXO))
		{
			std::cerr << "Unable to configure working directory: " << WORKING_DIRECTORY << std::endl;
			exit(EXIT_FAILURE);
		}
	}

	/* Pour ne pas garder un processus zombie lors de la fin du processus intermediaire */
	signal(SIGCHLD, SIG_IGN);

	pid = fork();
	
	if (pid == 0)
	{
		/* process fils */
		/* Creation du daemon si necessaire */
		DServer::CreateAndStartDaemonServer(DAEMON_PORT, argc, argv);
	}
	else if (pid > 0)
	{
		/* process pere */
		/* connexion au daemon */
		AClient::CreateAndConnecteClient(DAEMON_PORT, line, speed, monitoring, onePerUser);
	}
	else
	{
		std::cerr << "Unable to fork" << std::endl;
		exit(EXIT_FAILURE);
	}
	return EXIT_SUCCESS;
}

static void usage()
{
	std::cout << "Usage:" << std::endl;
	std::cout << " cutee [args]" << std::endl;
	std::cout << std::endl;
	std::cout << "Arguments:" << std::endl;
	std::cout << "  -l line    Name the line to use by giving a device name." << std::endl;
	std::cout << "  -m         Open cutee daemon monitoring connexion. Other option will be ignored." << std::endl;
	std::cout << "  -s speed   The speed (baud rate) to use." << std::endl;
	std::cout << "  -u         If current user has already open a connection to the line, return immediatly." << std::endl;
	std::cout << "  -v         Report version information and exit." << std::endl;
	std::cout << "  -?         Print a help message and exit." << std::endl;
}

static void version()
{
	std::cout << "cutee, version" << VERSION << std::endl;
	std::cout << " Immutable parameters: " << std::endl;
	std::cout << "   - client/server port: " << DAEMON_PORT << std::endl;
	std::cout << "   - working directory: " << WORKING_DIRECTORY << std::endl;
}

