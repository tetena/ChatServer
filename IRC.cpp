#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <strings.h>
#include <regex>
#include <getopt.h>

//Max values
const int MAX_NAME_LENGTH = 21;
const int MAX_BUFFER_LENGTH = 1024;

//User and Channel structs to hold our data
struct User {
	char userName[MAX_NAME_LENGTH];
	int userNameLength;	//Easier to send messages to clients that involve the user's name
	bool isOperator;
	int userFD;
};

struct Channel {
	char channelName[MAX_NAME_LENGTH];
	int channelNameLength;	//Easier to send messages to clients that involve the channel's name
	std::vector<User> usersInChannel;
};

//Server information
char password[MAX_NAME_LENGTH];
std::vector<User> allUsers;
std::vector<Channel> allChannels;
std::vector<int> allClients;

//This function returns the number of digits in num
int numDigits(int num) {
	int digits = 1;
	while(1) {
		num /= 10;
		if(num == 0) {
			return digits;
		}
		digits ++;
	}
}

//This function returns true if there is a registered user with file descriptor checkFD, otherwise returns false
bool userExists(int checkFD) {
	for(int i = 0; i < allUsers.size(); i ++) {
		if(allUsers[i].userFD == checkFD) {
			return true;
		}
	}
	return false;
}

//This function removes all instances of a disconnected user with the given FD
void removeInstances(int removedFD) {

	//Remove from allUsers...we can break as soon as we find one instance since there
	//can only be 1 user per FD
	for(int i = 0; i < allUsers.size(); i ++) {
		if(allUsers[i].userFD == removedFD) {
			allUsers.erase(allUsers.begin() + i);
			i = allUsers.size() - 1;	//Breaks out of the loop
		}
	}

	//For each channel, remove the user if they are there
	//We can break in the inner loop because each channel will have at most one instance of the disconnected user
	for(int i = 0; i < allChannels.size(); i ++) {
		for(int j = 0; j < allChannels[i].usersInChannel.size(); j ++) {
			if(allChannels[i].usersInChannel[j].userFD == removedFD) {

				//Notify other members of the channel that the user has left the channel
				int mesgLen = 25 + allChannels[i].channelNameLength + allChannels[i].usersInChannel[j].userNameLength;
				char mesg[mesgLen];
				strcpy(mesg, allChannels[i].channelName);
				strcat(mesg, "> ");
				strcat(mesg, allChannels[i].usersInChannel[j].userName);
				strcat(mesg, " has left the channel.\n");

				for(int k = 0; k < allChannels[i].usersInChannel.size(); k ++) {

					if(k != j) {	//Don't send the message to the disconnecting user
						write(allChannels[i].usersInChannel[k].userFD, mesg, mesgLen);
					}
				}

				//Remove the user
				allChannels[i].usersInChannel.erase(allChannels[i].usersInChannel.begin() + j);

				j = allChannels[i].usersInChannel.size() - 1;	//Breaks out of the inner loop
			}
		}
	}
}

int main(int argc, char** argv) {
	//Check to see if a valid password was provided
	if(argc > 2) {
		printf("Too many arguments provided.\nUsage: <executable> [--opt-pass=<password>]\n");
		exit(-1);
	}
	if(argc == 2) {	//Check if the given command line argument is valid
		int option_index = 0;
		static struct option long_options[] = {
			{"opt-pass", required_argument, 0, 0}
		};

		//If the command line argument is not valid, terminate the program (getopt_long() already prints an error message)
		if(getopt_long(argc, argv, "", long_options, &option_index) == '?') {
			exit(-1);
		}

		//If a password was obtained, make sure it is a valid password
		if(!std::regex_match(optarg, std::regex("[a-zA-Z][_0-9a-zA-Z]*"))) {
			printf("Password does not match expected regular expression: [a-zA-Z][_0-9a-zA-Z]*\n");
			exit(-1);
		}
		else {	//Ensure password is of a valid length
			if(strlen(optarg) > 20) {
				printf("Password must have length from 1-20 characters.\n");
				exit(-1);
			}
			else {	//Everything is valid, set the password of the server equal to optarg
				strcpy(password, optarg);
				password[strlen(optarg)] = '\0';
			}
		}
	}


	int 		i, j, maxfd, listenfd, connfd, sockfd;
	int 		nready;
	ssize_t 	n;
	fd_set 		rset, allset;
	char 		buf[MAX_BUFFER_LENGTH];
	socklen_t 	clilen;
	struct 		sockaddr_in cliaddr, servaddr;

	if((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket() error");
		exit(-1);
	}

	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htonl(0);
	servaddr.sin_port = htons(0);

	if(bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) < 0) {
		perror("bind() error");
		exit(-1);
	}

	//Print out port number
	socklen_t len = sizeof(servaddr);
	getsockname(listenfd, (struct sockaddr*) &servaddr, &len);
	printf("%d\n", ntohs(servaddr.sin_port));

	if(listen(listenfd, 5) < 0) {
		perror("listen() error");
		exit(-1);
	}

	maxfd = listenfd; //Initialize
	FD_ZERO(&allset);
	FD_SET(listenfd, &allset);

	for( ; ; ) {
		rset = allset;	//Structure assignment
		if((nready = select(maxfd + 1, &rset, NULL, NULL, NULL)) < 0) {
			perror("select() failed");
			exit(-1);
		}

		if(FD_ISSET(listenfd, &rset)) {	//New client connection
			clilen = sizeof(cliaddr);
			if((connfd = accept(listenfd, (struct sockaddr*) &cliaddr, &clilen)) < 0) {
				perror("accept() failed");
				exit(-1);
			}

			//If any of the spots in allClients are available, fill it
			//Otherwise, add this connection to the end of allClients
			for(i = 0; i < allClients.size(); i ++) {
				if(allClients[i] == -1) {
					allClients[i] = connfd;
					break;
				}
			}
			if(i == allClients.size()) {
				allClients.push_back(connfd);
			}

			FD_SET(connfd, &allset);	//Add new descriptor to set
			if(connfd > maxfd)
				maxfd = connfd;			//For select

			if(--nready <= 0)
				continue;
		}

		for(i = 0; i < allClients.size(); i ++) {	//Check all clients for data
			if((sockfd = allClients[i]) < 0) {
				continue;
			}
			if(FD_ISSET(sockfd, &rset)) {
				if((n = read(sockfd, buf, MAX_BUFFER_LENGTH)) == 0) {	//Connection closed by client

					//Remove all instances of the disconnected user from our data
					removeInstances(sockfd);

					//Close the socket and reflect change for select loop
					close(sockfd);
					FD_CLR(sockfd, &allset);
					allClients[i] = -1;
				}
				else {
					buf[n] = '\0';

					//Determine which command the user is trying to execute, everything is CASE-SENSITIVE

					//Make sure the message is coming from either an already-existing user or a new user using the command USER
					if(!userExists(sockfd)) {
						//If the user does not yet exist, the only valid command we can receive is "USER <nickname>"
						//If the command entered is valid, create new user...otherwise, disconnect the client with an error message

						if(n < 7 || n > 26) {	//No need to check further than this, since a valid USER command will have length of at least 7 (USER (4) + space (1) + name (1) + \n(1))
												//	and no more than 26 (USER (4) + space (1) + name (20) + \n (1))
							write(sockfd, "Invalid command, please identify yourself with USER.\n", 53);
							close(sockfd);
							FD_CLR(sockfd, &allset);
							allClients[i] = -1;
						}
						else {
							if(!(buf[0] == 'U' && buf[1] == 'S' && buf[2] == 'E' && buf[3] == 'R' && buf[4] == ' ')) {	//Command given is not USER
								write(sockfd, "Invalid command, please identify yourself with USER.\n", 53);
								close(sockfd);
								FD_CLR(sockfd, &allset);
								allClients[i] = -1;								
							}
							else {	//Check is if the given name matches the required regular expression
								char givenName[MAX_NAME_LENGTH];
								for(j = 5; j < n - 1; j ++) {
									givenName[j - 5] = buf[j];
								}
								givenName[j - 5] = '\0';

								if(!std::regex_match(givenName, std::regex("[a-zA-Z][_0-9a-zA-Z]*"))) {
									write(sockfd, "Invalid nickname, try again.\n", 29);
									close(sockfd);
									FD_CLR(sockfd, &allset);
									allClients[i] = -1;
								}
								else {	//Check if no other user has the same name
									bool nameTaken = false;
									for(j = 0; j < allUsers.size(); j ++) {
										if(strcmp(allUsers[j].userName, givenName) == 0) {
											nameTaken = true;
											j = allUsers.size() - 1;	//No need to keep searching
										}
									}

									if(nameTaken) {
										write(sockfd, "Name already taken.\n", 20);
										close(sockfd);
										FD_CLR(sockfd, &allset);
										allClients[i] = -1;
									}
									else {	//We can create the new user
										struct User user;
										for(j = 0; j < MAX_NAME_LENGTH; j ++) {
											user.userName[j] = givenName[j];
										}
										user.userNameLength = n - 6;
										user.isOperator = false;
										user.userFD = sockfd;
										allUsers.push_back(user);

										//Send a welcome message to the new user
										int mesgLen = 11 + user.userNameLength;
										char mesg[mesgLen];
										strcpy(mesg, "Welcome, ");
										strcat(mesg, user.userName);
										strcat(mesg, ".\n");
										write(sockfd, mesg, mesgLen);
									}
								}
							}
						}
					}
					else {	//User already exists, parse the input, then either execute the command or send an appropriate error message
						
						//Any valid command will have length of at least 5 ("LIST\n", "PART\n", and "QUIT\n" are the shortest valid commands)
						// and no more than 542 ("PRIVMSG <20 character name> <512 character message>\n")
						if(n < 5 || n > 542) {
							write(sockfd, "Invalid command.\n", 17);
						}
						else {	//Now that we know the input is of a valid length, parse the first word and act accordingly
							char firstWord[MAX_BUFFER_LENGTH];
							for(j = 0; buf[j] != ' ' && buf[j] != '\n' && buf[j] != '\0'; j ++) {	//The final value of j will be the index after the end of the first word
								firstWord[j] = buf[j];
							}
							firstWord[j] = '\0';

							if(strcmp(firstWord, "USER") == 0) {
								write(sockfd, "You cannot change your username.\n", 33);
							}
							else if(strcmp(firstWord, "LIST") == 0) {
								if(j == n - 1) {	//If the input was simply "LIST\n", print out all available channels
									int numChannels = allChannels.size();
									char numChannelsAsString[MAX_BUFFER_LENGTH];
									sprintf(numChannelsAsString, "%d", numChannels);
									int numDigitsInChannelCount = numDigits(numChannels);
									numChannelsAsString[numDigitsInChannelCount] = '\0';

									int mesgLen = 33 + numDigitsInChannelCount;
									char mesg[MAX_BUFFER_LENGTH];
									strcpy(mesg, "There are currently ");
									strcat(mesg, numChannelsAsString);
									strcat(mesg, " channel(s):\n");

									write(sockfd, mesg, mesgLen);

									for(j = 0; j < allChannels.size(); j ++) {	//Send the name of each channel to the user
										bzero(&mesg, MAX_BUFFER_LENGTH);
										mesgLen = 3 + allChannels[j].channelNameLength;
										strcpy(mesg, "* ");
										strcat(mesg, allChannels[j].channelName);
										strcat(mesg, "\n");

										write(sockfd, mesg, mesgLen);
									}
								}
								else {	//Check if the provided channel name is valid
									if(n > 26) {	//A valid LIST command will have length no more than 26 (LIST <20 char channel name>\n)
										write(sockfd, "Channel name must have length 1-20.\n", 36);
									}
									else {	//Valid length, check that the channel exists
										char givenName[MAX_NAME_LENGTH];
										for(j = 5; j < n - 1; j ++) {
											givenName[j - 5] = buf[j];
										}
										givenName[j - 5] = '\0';

										bool channelExists = false;
										for(j = 0; j < allChannels.size(); j ++) {
											if(strcmp(allChannels[j].channelName, givenName) == 0) {	//Channel exists, list all users in the channel
												channelExists = true;

												int numUsers = allChannels[j].usersInChannel.size();
												char numUsersAsString[MAX_BUFFER_LENGTH];
												sprintf(numUsersAsString, "%d", numUsers);
												int numDigitsInUserCount = numDigits(numUsers);
												numUsersAsString[numDigitsInUserCount] = '\0';
 
												int mesgLen = 36 + numDigitsInUserCount + allChannels[j].channelNameLength;
												char mesg[mesgLen];
												strcpy(mesg, "There are currently ");
												strcat(mesg, numUsersAsString);
												strcat(mesg, " member(s) in ");
												strcat(mesg, allChannels[j].channelName);
												strcat(mesg, ":\n");

												write(sockfd, mesg, mesgLen);

												for(int k = 0; k < allChannels[j].usersInChannel.size(); k ++) {
													bzero(&mesg, mesgLen);
													mesgLen = 3 + allChannels[j].usersInChannel[k].userNameLength;
													strcpy(mesg, "* ");
													strcat(mesg, allChannels[j].usersInChannel[k].userName);
													strcat(mesg, "\n");

													write(sockfd, mesg, mesgLen);
												}

												j = allChannels.size() - 1;
											}
										}

										if(!channelExists) {
											write(sockfd, "There are no channels with the name you have given.\n", 52);
										}
									}
								}
							}
							else if(strcmp(firstWord, "JOIN") == 0) {

								//A valid JOIN command will have length between 7 ("JOIN #\n") and 26 ("JOIN <20 char channel name>\n")
								if(n < 7 || n > 26) {
									write(sockfd, "Channel name must have length 1-20.\n", 36);
								}
								else {	//Make sure a space comes after JOIN
									if(buf[j] != ' ') {
										write(sockfd, "Malformed JOIN command - Usage: JOIN <#channelname>\n", 52);
									}
									else {	//Parse given channel name and make sure that it is valid
										char givenName[MAX_NAME_LENGTH];
										for(j = 5; j < n - 1; j ++) {
											givenName[j - 5] = buf[j];
										}
										givenName[j - 5] = '\0';

										if(!std::regex_match(givenName, std::regex("#[a-zA-Z][_0-9a-zA-Z]*"))) {
											write(sockfd, "Channel name does not match expected regular expression: #[a-zA-Z][_0-9a-zA-Z]*\n", 80);
										}
										else {	//If the channel exists, join it...otherwise, create the channel and join it
											bool channelExists = false;

											//Get this user
											struct User userToAdd;
											for(j = 0; j < allUsers.size(); j ++) {
												if(allUsers[j].userFD == sockfd) {
													userToAdd = allUsers[j];
													j = allUsers.size() - 1;
												}
											}

											for(j = 0; j < allChannels.size(); j ++) {
												if(strcmp(allChannels[j].channelName, givenName) == 0) {	//Channel exists
													channelExists = true;

													//Ensure that the user is not already a member of this channel
													bool alreadyIn = false;
													for(int k = 0; k < allChannels[j].usersInChannel.size(); k ++) {
														if(strcmp(allChannels[j].usersInChannel[k].userName, userToAdd.userName) == 0) {
															alreadyIn = true;
															write(sockfd, "You are already a member of this channel.\n", 42);
														}
													}

													if(!alreadyIn) {
														int mesgLen = 27 + allChannels[j].channelNameLength + userToAdd.userNameLength;
														char mesg[mesgLen];
														strcpy(mesg, allChannels[j].channelName);
														strcat(mesg, "> ");
														strcat(mesg, userToAdd.userName);
														strcat(mesg, " has joined the channel.\n");

														//Notify all other users of the channel of the new member
														for(int k = 0; k < allChannels[j].usersInChannel.size(); k ++) {
															write(allChannels[j].usersInChannel[k].userFD, mesg, mesgLen);
														}

														//Add the user to the channel
														allChannels[j].usersInChannel.push_back(userToAdd);

														//Send confirmation message to the user
														bzero(&mesg, mesgLen);
														mesgLen = 16 + allChannels[j].channelNameLength;
														strcpy(mesg, "Joined channel ");
														strcat(mesg, allChannels[j].channelName);
														strcat(mesg, "\n");

														write(sockfd, mesg, mesgLen);

														//We can break out of the loop because there can only be 1 channel of the given name
														j = allChannels.size() - 1;
													}
												}
											}
											if(!channelExists) {	//Create a new channel with the given name
												struct Channel channel;
												for(j = 0; j < MAX_NAME_LENGTH; j ++) {
													channel.channelName[j] = givenName[j];
												}
												channel.channelNameLength = n - 6;
												channel.usersInChannel.push_back(userToAdd);
												allChannels.push_back(channel);

												//Send confirmation message to the user
												int mesgLen = 16 + channel.channelNameLength;
												char mesg[mesgLen];
												strcpy(mesg, "Joined channel ");
												strcat(mesg, channel.channelName);
												strcat(mesg, "\n");

												write(sockfd, mesg, mesgLen);
											}
										}
									}
								}
							}
							else if(strcmp(firstWord, "PART") == 0) {
								if(j == n - 1) {	//If the input was simply "PART\n" then remove the user from all channels and notify the members
													// of the channels that they have left

									//For each channel, remove the user if they are there
									//We can break in the inner loop because each channel will have at most one instance of the disconnected user
									for(int k = 0; k < allChannels.size(); k ++) {
										for(int l = 0; l < allChannels[k].usersInChannel.size(); l ++) {
											if(allChannels[k].usersInChannel[l].userFD == sockfd) {

												//Notify other members of the channel that the user has left the channel
												int mesgLen = 25 + allChannels[k].channelNameLength + allChannels[k].usersInChannel[l].userNameLength;
												char mesg[mesgLen];
												strcpy(mesg, allChannels[k].channelName);
												strcat(mesg, "> ");
												strcat(mesg, allChannels[k].usersInChannel[l].userName);
												strcat(mesg, " has left the channel.\n");

												for(int m = 0; m < allChannels[k].usersInChannel.size(); m ++) {

													if(m != l) {	//Don't send the message to the disconnecting user
														write(allChannels[k].usersInChannel[m].userFD, mesg, mesgLen);
													}
												}

												//Remove the user
												allChannels[k].usersInChannel.erase(allChannels[k].usersInChannel.begin() + l);

												l = allChannels[k].usersInChannel.size() - 1;	//Breaks out of the inner loop
											}
										}
									}
								}
								else {
									if(n > 26) {	//A valid LIST command will have length no more than 26 (LIST <20 char channel name>\n)
										write(sockfd, "Channel name must have length 1-20.\n", 36);
									}
									else {	//Valid length, check that the channel exists
										char givenName[MAX_NAME_LENGTH];
										for(j = 5; j < n - 1; j ++) {
											givenName[j - 5] = buf[j];
										}
										givenName[j - 5] = '\0';

										bool channelExists = false;

										//Get this user
										struct User userToRemove;
										for(j = 0; j < allUsers.size(); j ++) {
											if(allUsers[j].userFD == sockfd) {
												userToRemove = allUsers[j];
												j = allUsers.size() - 1;
											}
										}

										for(j = 0; j < allChannels.size(); j ++) {
											if(strcmp(allChannels[j].channelName, givenName) == 0) {	//Channel exists, remove the user from the channel
												channelExists = true;

												//Check if the user is a member of the given channel or not
												bool alreadyIn = false;
												for(int k = 0; k < allChannels[j].usersInChannel.size(); k ++) {
													if(strcmp(allChannels[j].usersInChannel[k].userName, userToRemove.userName) == 0) {
														alreadyIn = true;
													}
												}

												//If they are a member of the given channel, remove them and notify the other members...
												// otherwise, send an error message
												if(!alreadyIn) {
													write(sockfd, "You are not a member of that channel.\n", 38);
												}
												else {
													for(int k = 0; k < allChannels[j].usersInChannel.size(); k ++) {
														if(allChannels[j].usersInChannel[k].userFD == sockfd) {

															//Notify other members of the channel that the user has left the channel
															int mesgLen = 25 + allChannels[j].channelNameLength + allChannels[j].usersInChannel[k].userNameLength;
															char mesg[mesgLen];
															strcpy(mesg, allChannels[j].channelName);
															strcat(mesg, "> ");
															strcat(mesg, allChannels[j].usersInChannel[k].userName);
															strcat(mesg, " has left the channel.\n");

															for(int l = 0; l < allChannels[j].usersInChannel.size(); l ++) {

																if(l != k) {	//Don't send the message to the disconnecting user
																	write(allChannels[j].usersInChannel[l].userFD, mesg, mesgLen);
																}
															}

															//Remove the user
															allChannels[j].usersInChannel.erase(allChannels[j].usersInChannel.begin() + k);

															k = allChannels[j].usersInChannel.size() - 1;	//Breaks out of the inner loop
														}
													}	
												}
											}
										}

										if(!channelExists) {
											write(sockfd, "There are no channels with the name you have given.\n", 52);
										}											
									}																														
								}
							}
							else if(strcmp(firstWord, "OPERATOR") == 0) {
								//If the server has no password, then no user can become an operator
								if(strcmp(password, "") == 0) {
									write(sockfd, "This server has no password, no user can become an operator.\n", 61);
								}
								else {
									//If the user is already an operator, just send an error message
									bool alreadyOperator = false;
									for(int k = 0; k < allUsers.size(); k ++) {
										if(allUsers[k].userFD == sockfd) {
											if(allUsers[k].isOperator) {
												write(sockfd, "You are already an operator.\n", 29);
												alreadyOperator = true;
											}
											k = allUsers.size() - 1;
										}
									}

									if(!alreadyOperator) {
										//A valid OPERATOR command will have at least 11 characters (OPERATOR <1 char password>\n)
										//and at most 30 characters (OPERATOR <20 char password>\n)
										if(n < 11 || n > 30) {
											write(sockfd, "Password must be 1-20 characters.\n", 34);
										}
										else {	//Valid length command, extract the given password and compare it to the server password
											char givenPassword[MAX_NAME_LENGTH];
											for(j = 9; j < n - 1; j ++) {
												givenPassword[j - 9] = buf[j];
											}
											givenPassword[j - 9] = '\0';

											//If the given password is the same as the server password, give operator status to the user...
											// otherwise, send an error message
											if(strcmp(givenPassword, password) != 0) {
												write(sockfd, "Incorrect password.\n", 20); 
											}
											else {	//If the password is correct, find this user in our data and give them operator status
												for(int k = 0; k < allUsers.size(); k ++) {
													if(allUsers[k].userFD == sockfd) {
														allUsers[k].isOperator = true;
														write(sockfd, "Operator status bestowed.\n", 26);
														k = allUsers.size() - 1;
													}
												}

												//Note that we do not need to update the user data in the allChannels vector, since when a user tries
												// to use the KICK command, we can just check allUsers directly
											}
										}
									}
								}
							}
							else if(strcmp(firstWord, "KICK") == 0) {
								//If the user is not an operator, then do not allow them to use the KICK command
								bool isOperator = false;
								for(int k = 0; k < allUsers.size(); k ++) {
									if(allUsers[k].userFD == sockfd) {
										if(allUsers[k].isOperator) {
											isOperator = true;
										}
										k = allUsers.size() - 1;
									}
								}

								if(!isOperator) {
									write(sockfd, "You are not an operator of this server.\n", 40);
								}
								else {
									//Get the rest of the input to be further parsed
									char restOfInput[MAX_BUFFER_LENGTH];
									for(j = j + 1; j < n; j ++) {
										restOfInput[j - 5] = buf[j];
									}
									restOfInput[j - 5] = '\0';

									//A valid rest of input will have at least 4 characters (<1 char channel name> <1 char user name>\n)
									// and at most 42 characters (<20 character channel name> <20 character user name>\n)
									if(strlen(restOfInput) < 4 || strlen(restOfInput) > 42) {
										write(sockfd, "Invalid KICK command: channel and user names must be 1-20 characters in length.\n", 80);
									}
									else { //Parse the given channel name from restOfInput
										char givenChannel[MAX_BUFFER_LENGTH]; //Give the buffer extra room in case the user gives a channel name that 
																			  // is too long
										for(j = 0; restOfInput[j] != ' ' && restOfInput[j] != '\n' && restOfInput[j] != '\0'; j ++) {
											givenChannel[j] = restOfInput[j];
										}
										givenChannel[j] = '\0'; //j is equal to the index directly after the last letter of the given channel name

										//Check if the given channel name is valid
										bool channelExists = false;
										for(int k = 0; k < allChannels.size(); k ++) {
											if(strcmp(allChannels[k].channelName, givenChannel) == 0) {
												channelExists = true;
												k = allChannels.size() - 1;
											}
										}

										if(!channelExists) {
											write(sockfd, "There is no channel with the name you have provided.\n", 53);
										}
										else {	//If the channel exists, get the rest of restOfInput and see if it is the name of an existing user
											char givenName[MAX_BUFFER_LENGTH]; //Give the buffer extra room in case the user gives a user name that
																			   // is too long
											for(j = j + 1; j < strlen(restOfInput) - 1; j ++) {
												givenName[j - strlen(givenChannel) - 1] = restOfInput[j];
											}
											givenName[j - strlen(givenChannel) - 1] = '\0';

											//Check if the given user name is valid
											bool userExists = false;
											for(int k = 0; k < allUsers.size(); k ++) {
												if(strcmp(allUsers[k].userName, givenName) == 0) {
													userExists = true;
													k = allUsers.size() - 1;
												}
											}

											if(!userExists) {
												write(sockfd, "There is no user with the name you have provided.\n", 50);
											}
											else {	//Check to see if the given user is in the given channel
												bool userInChannel = false;
												for(int k = 0; k < allChannels.size(); k ++) {
													if(strcmp(allChannels[k].channelName, givenChannel) == 0) {
														for(int l = 0; l < allChannels[k].usersInChannel.size(); l ++) {
															if(strcmp(allChannels[k].usersInChannel[l].userName, givenName) == 0) {
																userInChannel = true;

																//The given user is in the channel...remove them from the channel and notify the other members

																//First, notify the user being kicked
																int mesgLen = 36 + allChannels[k].channelNameLength;
																char mesg[MAX_BUFFER_LENGTH];
																strcpy(mesg, "You have been kicked from channel ");
																strcat(mesg, allChannels[k].channelName);
																strcat(mesg, ".\n");

																write(allChannels[k].usersInChannel[l].userFD, mesg, mesgLen);

																//Notify everyone else in the channel
																for(int m = 0; m < allChannels[k].usersInChannel.size(); m ++) {
																	bzero(&mesg, MAX_BUFFER_LENGTH);
																	mesgLen = 37 + allChannels[k].channelNameLength + allChannels[k].usersInChannel[l].userNameLength;
																	strcpy(mesg, allChannels[k].channelName);
																	strcat(mesg, "> ");
																	strcat(mesg, allChannels[k].usersInChannel[l].userName);
																	strcat(mesg, " has been kicked from the channel.\n");

																	//Don't send this message to the user being kicked
																	if(m != l) {
																		write(allChannels[k].usersInChannel[m].userFD, mesg, mesgLen);
																	}
																}

																//Remove the user from the channel
																allChannels[k].usersInChannel.erase(allChannels[k].usersInChannel.begin() + l);
															}
														}
													}
												}
												if(!userInChannel) {
													write(sockfd, "The given user is not in the given channel.\n", 44);
												}
											}
										}
									}
								}
							}
							else if(strcmp(firstWord, "PRIVMSG") == 0) {
								//Get the rest of the input to be further parsed
								char restOfInput[MAX_BUFFER_LENGTH];
								for(j = j + 1; j < n; j ++) {
									restOfInput[j - 8] = buf[j];
								}
								restOfInput[j - 8] = '\0';

								//A valid rest of input will have at least 4 characters (<1 char channel or user name> <1 char message>\n)
								// and at most 534 characters (<20 char channel or user name> <512 char message>\n)
								if(strlen(restOfInput) < 4 || strlen(restOfInput) > 534) {
									write(sockfd, "Invalid PRIVMSG command.\n", 25);
								}
								else {	//Parse the given channel/user name from restOfInput
									char givenName[MAX_BUFFER_LENGTH]; //Give the buffer extra room in case the user gives a name that 
																		  // is too long
									for(j = 0; restOfInput[j] != ' ' && restOfInput[j] != '\n' && restOfInput[j] != '\0'; j ++) {
										givenName[j] = restOfInput[j];
									}
									givenName[j] = '\0'; //j is equal to the index directly after the last letter of the given name

									//Check to see if the given name is either a valid user name or valid channel name
									bool validUser = false;									
									bool validChannel = false;

									for(int k = 0; k < allUsers.size(); k ++) {
										if(strcmp(allUsers[k].userName, givenName) == 0) {
											validUser = true;
											k = allUsers.size() - 1;
										}
									}
									if(!validUser) {
										for(int k = 0; k < allChannels.size(); k ++) {
											if(strcmp(allChannels[k].channelName, givenName) == 0) {
												validChannel = true;
												k = allChannels.size() - 1;
											}
										}
									}

									if(!validUser && !validChannel) {
										write(sockfd, "There is no user or channel with the name you have provided.\n", 61);
									}
									else {	//If the message has a potential recipient, check to make sure the message is at least 1 char in length
										char userMesg[MAX_BUFFER_LENGTH];

										for(j = j + 1; j < strlen(restOfInput) - 1; j ++) {
											userMesg[j - strlen(givenName) - 1] = restOfInput[j];
										}
										userMesg[j - strlen(givenName) - 1] = '\0';

										if(strlen(userMesg) < 1) {
											write(sockfd, "Messages must be at least 1 character in length.\n", 49);
										}
										else {
											//Get the sending user's info
											struct User sendingUser;
											for(int k = 0; k < allUsers.size(); k ++) {
												if(allUsers[k].userFD == sockfd) {
													sendingUser = allUsers[k];
													k = allUsers.size() - 1;
												}
											}

											if(validUser) {	//If we're sending to a specific user, send the message to that user
												if(strcmp(givenName, sendingUser.userName) == 0) {	//Do not let user send message to themselves
													write(sockfd, "You cannot send a message to yourself.\n", 39);
												}
												else {
													int mesgLen = 3 + sendingUser.userNameLength + strlen(userMesg);
													char mesg[mesgLen];

													strcpy(mesg, sendingUser.userName);
													strcat(mesg, ": ");
													strcat(mesg, userMesg);
													strcat(mesg, "\n");

													//Find the user and send the message
													for(int k = 0; k < allUsers.size(); k ++) {
														if(strcmp(givenName, allUsers[k].userName) == 0) {
															write(allUsers[k].userFD, mesg, mesgLen);
															k = allUsers.size() - 1;
														}
													}

												}
											}
											else {	//We're sending this message to a whole channel
												int mesgLen = 5 + strlen(givenName) + sendingUser.userNameLength + strlen(userMesg);
												char mesg[mesgLen];

												strcpy(mesg, givenName);
												strcat(mesg, "> ");
												strcat(mesg, sendingUser.userName);
												strcat(mesg, ": ");
												strcat(mesg, userMesg);
												strcat(mesg, "\n");

												for(int k = 0; k < allChannels.size(); k ++) {
													if(strcmp(allChannels[k].channelName, givenName) == 0) {
														for(int l = 0; l < allChannels[k].usersInChannel.size(); l ++) {
															write(allChannels[k].usersInChannel[l].userFD, mesg, mesgLen);
														}
													}
												}
											}
										}									
									}
								}
							}
							else if(strcmp(firstWord, "QUIT") == 0) {
								if(j == n - 1) {	//We've received a correctly formed QUIT command ("QUIT\n")
									removeInstances(sockfd);
									close(sockfd);
									FD_CLR(sockfd, &allset);
									allClients[i] = -1;								
								}
								else {	//Malformed command, send error message
									write(sockfd, "Malformed QUIT command - Usage: QUIT\n", 37);
								}
							}
							else {	//Invalid command
								write(sockfd, "Invalid command.\n", 17);
							}
						}
					}

					bzero(&buf, MAX_BUFFER_LENGTH);
				}

				if(--nready <= 0)	//No more readable descriptors
					break;
			}
		}
	}
}
