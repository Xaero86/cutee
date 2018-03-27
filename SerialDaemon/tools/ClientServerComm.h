#ifndef CLIENT_SERVER_COMM_H
#define CLIENT_SERVER_COMM_H

#include <map>
#include <string>

int createMessage(char* p_msg, int p_msgMaxSize, const char* p_source, std::map<std::string, std::string> &p_data);
bool readMessage(char* p_msg, int p_msgSize, const char* p_expectSource, std::map<std::string, std::string> &p_data);

#endif // CLIENT_SERVER_COMM_H
