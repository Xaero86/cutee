#include "ClientServerComm.h"

#include <sstream>

int createMessage(char* p_msg, int p_msgMaxSize, const char* p_source, std::map<std::string, std::string> &p_data)
{
	std::stringstream streamWrite;
	std::map<std::string, std::string>::iterator iter;

	streamWrite << "[" << p_source << "]";
	for (iter = p_data.begin(); iter != p_data.end(); iter++)
	{
		streamWrite << " " << iter->first << "=[" << iter->second << "]";
	}

	return streamWrite.rdbuf()->sgetn(p_msg, p_msgMaxSize);
}

bool readMessage(char* p_msg, int p_msgSize, const char* p_expectSource, std::map<std::string, std::string> &p_data)
{
	std::string msgRead(p_msg, p_msgSize);
	std::size_t firstOpenBracket = msgRead.find('[');
	std::size_t firstCloseBracket = msgRead.find(']', firstOpenBracket+1);

	if ((firstOpenBracket == std::string::npos) ||
	    (firstCloseBracket == std::string::npos))
	{
		return false;
	}

	std::string source = msgRead.substr(firstOpenBracket+1, firstCloseBracket-firstOpenBracket-1);
	if (source.compare(p_expectSource) != 0)
	{
		return false;
	}

	std::size_t paramPos = msgRead.find(' ', firstCloseBracket);
	while (paramPos != std::string::npos)
	{
		std::size_t keyStart = msgRead.find_first_not_of(' ', paramPos);
		std::size_t keyStop = msgRead.find_first_of(" =", keyStart);

		if ((keyStart == std::string::npos) ||
		    (keyStop == std::string::npos))
		{
			/* Parametre inconsistant: stop */
			break;
		}
		std::string key = msgRead.substr(keyStart, keyStop-keyStart);

		std::size_t equalPos = msgRead.find('=', keyStop);

		if (equalPos == std::string::npos)
		{
			/* Parametre inconsistant: stop */
			break;
		}

		std::size_t valueStart = msgRead.find('[', equalPos);
		std::size_t valueStop = msgRead.find(']', valueStart+1);

		if ((valueStart == std::string::npos) ||
		    (valueStop == std::string::npos))
		{
			/* Parametre inconsistant: stop */
			break;
		}
		std::string value = msgRead.substr(valueStart+1, valueStop-valueStart-1);

		p_data[key] = value;
		paramPos = msgRead.find(' ', valueStop);
	}
	return true;
}

