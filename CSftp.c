#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h> // for close
#include <arpa/inet.h>
#include "dir.h"
#include "usage.h"

#define BUFFER_SIZE 256
int sockfd, new_fd, pasvsock_fd, port, new_pasv_fd;  // listen on sockfd, new connection on new_fd
int inPASV = 0, BACKLOG = 20; // Most system silently limit this to 20 but we can probably even get away with 5 or 10

/** 
 *  Parse for FTP commands from client
 */
int userLoggedIn = 0; // User is not logged in

int ftpCommand(char *str, int rec, char *rootDir) {
    char dup[BUFFER_SIZE];
    char *command;
    char *argument;

    
    if ((command = strtok(str, " ")) != NULL || (command = strtok(str, "\n")) != NULL || (command = strtok(str, "\r")) != NULL) {
        printf("Command from client is '%s'\n", command);
        // Check if client command also includes an argument
        if ((argument = strtok(NULL, " ")) != NULL) {
            printf("Argument from client is %s\n", argument);
        }
    }

    // strip \n and \r from buffer on input
    while (rec >= 1 && (str[rec - 1] == '\n' || str[rec - 1] == '\r')) {
        str[rec-1] = 0;
        rec--;
    }

    // USER
    if (strcasecmp(command, "USER") == 0) {
        printf("IN USER\n");
        if (argument == NULL) {
            if (send(new_fd, "501 Please check user info.\n", 28, 0) < 0) {
                printf("Error | server: USER login\n");
            }
            return 0;
        }

        if (userLoggedIn == 0) {
            if (strcasecmp(argument, "CS317") == 0) {
                if (send(new_fd, "220 User logged in successful.\n", 31, 0) < 0) {
                    printf("Error | server: USER login\n");
                }
                userLoggedIn = 1;
                printf("User is logged in and userLoggedIn is %d\n", userLoggedIn);
            } else {
                if (send(new_fd, "430 Please check user info.\n", 28, 0) < 0) {
                    printf("Error | server: USER login\n");
                }
            }
        } else {
            if (send(new_fd, "Already logged in.\n", 24, 0) < 0) {
                printf("Error | server: USER login\n");
            }
        }
        return 0;
    }

    // Password is not required
    if (strcasecmp(command, "PASS") == 0) {
        if (userLoggedIn == 0) {
            if (send(new_fd, "530 Please login with username first.\n", 38, 0) < 0) {
                printf("Error | server: PASS without login\n");
            }
            return 0;
        } else {
            if (send(new_fd, "Password is not required.\n", 26, 0) < 0) {
                printf("Error | server: PASS without login\n");
            }
        }
        return 0;
    }

    if (strcasecmp(command, "QUIT") == 0) {
        if (send(new_fd, "Goodbye.\n", 9, 0) < 0) {
            printf("Error | Server: Quit\n");
        }
		char cwd[256];
	    if (getcwd(cwd, sizeof(cwd)) != NULL) {
		    printf("Current working dir: %s\n", cwd);
	    } else {
		    perror("getcwd() error");
		    return 1;
	    }
		while (strcmp(rootDir, cwd) != 0) {
      memset(&cwd, 0, sizeof(cwd));
      if (getcwd(cwd, sizeof(cwd)) != NULL) {
          printf("Current working dir: %s\n", cwd);
      } else {
          perror("getcwd() error");
          return 1;
      }
			chdir("../");
		}

        chdir("./a4_k4d9_n4v8");

        userLoggedIn = 0;
        close(new_fd);
    }

    if (userLoggedIn == 0) {
        if (send(new_fd, "530 Please login with username first.\n", 38, 0) < 0) {
            printf("Error | server: login first\n");
        }
        return 0;
    }

	if (strcasecmp(command, "CWD") == 0) {
		// do not let client navigate to path with ./ or ../
		if (strstr(argument, "../") != NULL || strstr(argument, "./") != NULL) {
			printf("../ or ./ encountered\n");
			if (send(new_fd, "550 cannot navigate path with ../ or ./\n", 40, 0) < 0) {
				printf("Error | server: cannot navigate path with ../ or ./");
			}
		} else {
			printf("navigating to %s\n", argument);
			if (chdir(argument) < 0) {
				if (send(new_fd, "550 could not navigate to directory\n", 36, 0) == 0) {
					printf("Error | server: CWD to %s\n", argument);
				}
			} else {
				if (send(new_fd, "250 successfully changed working directory\n", 43, 0) < 0) {
					printf("Error | server: CWD");
				}		
			}	
		}
		return 0;
	}

	if (strcasecmp(command, "CDUP") == 0) {
		char cwd[256];
	    if (getcwd(cwd, sizeof(cwd)) != NULL) {
		    printf("Current working dir: %s\n", cwd);
	    } else {
		    perror("getcwd() error");
		    return 1;
	    }
      printf("RootDir is %s\n", rootDir);
      printf("cwd is %s\n", cwd);
		if (strcmp(rootDir, cwd) == 0) {
			if (send(new_fd, "500 already at root directory\n", 31, 0) < 0) {
				printf("Error | server: CDUP\n");
			}
			return 0;
		} else if (chdir("../") < 0) {
			if (send(new_fd, "500 could not navigate to directory\n", 36, 0) < 0) {
				printf("Error | server: CDUP\n");
			}
      return 0;
		} else {
			if (send(new_fd, "200 successfully changed to parent directory\n", 45, 0) < 0) {
				printf("Error | server: CDUP\n");
			}
		}
		return 0;
	}

	if (strcasecmp(command, "TYPE") == 0) {
        if (argument == NULL) {
            if (send(new_fd, "501 Incorrect syntax.\n", 22, 0) < 0) {
                printf("Error | server: TYPE argument\n");
            }
            return 0;
        } 

        if (strcasecmp(argument, "I") == 0) {
            if (send(new_fd, "200 Switched to binary mode.\n", 30, 0) < 0) {
                printf("Error | server: TYPE binary\n");
            }
          } else if (strcasecmp(argument, "A") == 0) {
              if (send(new_fd, "200 Switched to ASCII mode.\n", 29, 0) < 0) {
                  printf("Error | server: TYPE binary\n");
              }
          } else {
              if (send(new_fd, "501 Incorrect syntax.\n", 22, 0) < 0) {
                  printf("Error | server: TYPE default\n");
              }
          }
		return 0;
	}

	if (strcasecmp(command, "STRU") == 0) {
        if (argument == NULL) {
            if (send(new_fd, "501 Please provide file structure.\n", 42, 0) < 0) {
                printf("Error | server: STRU argument\n");
            }
            return 0;
        }

        if (strcasecmp(argument, "F") == 0) {
            if (send(new_fd, "200 Structure set to file.\n", 27, 0) < 0) {
                printf("Error | server: STRU argument\n");
            }
        } else if (strcasecmp(argument, "R") == 0) {
            if (send(new_fd, "504 Record structure not supported.\n", 36, 0) < 0) {
                printf("Error | server: STRU argument\n");
            }
        } else if (strcasecmp(argument, "P") == 0) {
            if (send(new_fd, "504 Page structure not supported.\n", 34, 0) < 0) {
                printf("Error | server: STRU argument\n");
            }
        } else {
            if (send(new_fd, "504 Argument not recognized.\n", 31, 0) < 0) {
                printf("Error | server: STRU argument\n");
            }
        }

		return 0;
	}

	if (strcasecmp(command, "RETR") == 0) {
      if (argument == NULL) {
          if (send(new_fd, "550 Please provide file name.\n", 27, 0) < 0) {
              printf("Error | server: RETR file name\n");
          }
          return 0;
      } 

      if (inPASV == 0) {
          if (send(new_fd, "425 Please enter passive mode first.\n", 33, 0) < 0) {
              printf("Error | server: RETR passive mode\n");
          }
      }

  		struct sockaddr_storage their_conn_addr; 
  		socklen_t new_sin_size;
  		new_sin_size = sizeof(their_conn_addr);
  		printf("middle of RETR: pasvsock_fd: %d\n", pasvsock_fd);
  		new_pasv_fd = accept(pasvsock_fd, (struct sockaddr *)&their_conn_addr, &new_sin_size);
  		if (new_pasv_fd < 0) {
  		   printf("error | server: accept\n");
  		}

  		if (send(new_fd, "125 Transferring data.\n", 23, 0) < 0) {
  		  printf("Error | server: Transferring data\n");
  		  return 0;
  		}

  		FILE* input_file;
  		input_file = fopen(argument, "r");
  		char ch;
  		if (input_file != NULL) {
  			 while ((ch = getc(input_file)) != EOF) {
  				  send(new_pasv_fd, &ch, 1, 0);
  		}
		}
		
		inPASV = 0;
		close(new_pasv_fd);
		new_pasv_fd = -1;
		close(pasvsock_fd);
		pasvsock_fd = -1;

		if (send(new_fd, "226 Closing data connection. Requested file action successful.\n", 63, 0) < 0) {
		  printf("Error | server: closing connection\n");
		  return 0;
		}
	}

	if (strcasecmp(command, "PASV") == 0) {
            printf("Info | server: entering PASV mode\n");
            char ipstr[INET6_ADDRSTRLEN];
            char addr[BUFFER_SIZE];
            char *ipAddr;

            struct sockaddr_in pasvserv_addr;
            memset(&pasvserv_addr, 0, sizeof pasvserv_addr); // make sure the struct is empty

            pasvserv_addr.sin_family = AF_INET;
            pasvserv_addr.sin_port = 0;
            pasvserv_addr.sin_addr.s_addr = INADDR_ANY;

            // HOST-PORT specification for the data port to be used in data connection
            // begins with the higher order 8 bits of the internet host address
            int h1, h2, h3, h4, p1, p2;

          if ((pasvsock_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            printf("Error: PASV open socket\n");
            exit(1);
          }

            printf("info | server: (PASV) binding to random port\n");
            if (bind(pasvsock_fd, (struct sockaddr *) &pasvserv_addr, sizeof(pasvserv_addr)) == -1) {
              printf("Error: PASV bind\n");
              exit(1);
            }

            printf("hi\n");

            struct sockaddr_in sa;
            socklen_t sa_length;
            sa_length = sizeof(sa);
            getsockname(pasvsock_fd, (struct sockaddr *) &sa, &sa_length);
            port = (int) ntohs(sa.sin_port);

            struct hostent *he;
            struct in_addr **addr_list;
            gethostname(ipstr, sizeof ipstr);
            if ((he = gethostbyname(ipstr)) == NULL) {  // get the host info
                  herror("gethostbyname");
                  return 2;
            }

            
            // print information about this host:
            printf("info | server: (PASV) official server name is: %s\n", he->h_name);
            addr_list = (struct in_addr **)he->h_addr_list;
            int i;
            for(i = 0; addr_list[i] != NULL; i++) {
                strcpy(addr, inet_ntoa(*addr_list[i]));
            }

            printf("Info: PASV bound to port %d\n", port);
            printf("Info: PASV bound to IP address: %s\n", addr);
          	printf("after getaddrinfo\n");

                int port = (int) ntohs(sa.sin_port);
				uint32_t sin_addr = sa.sin_addr.s_addr;

                printf("Port is: %d\n", port);
				
				p1 = port / 256;
				p2 = port % 256;

                ipAddr = strtok(inet_ntoa(*addr_list[0]), ".\r\n");
                if (ipAddr != NULL) {
                  h1 = atoi(ipAddr);
                  printf("h1 is %d\n", h1);
                  ipAddr = strtok(NULL, ".\r\n");
                }
                if (ipAddr != NULL) {
                  h2 = atoi(ipAddr);
                  printf("h2 is %d\n", h2);
                  ipAddr = strtok(NULL, ".\r\n");
                }
                if (ipAddr != NULL) {
                  h3 = atoi(ipAddr);
                  printf("h3 is %d\n", h3);
                  ipAddr = strtok(NULL, ".\r\n");
                }
                if (ipAddr != NULL) {
                  h4 = atoi(ipAddr);
                  printf("h4 is %d\n", h4);
                  ipAddr = strtok(NULL, ".\r\n");
                }

			
                // Set to listening mode
                if (listen(pasvsock_fd, BACKLOG) < 0) {
                  perror("Listen");
                  inPASV = 0;
                  exit(1);
                } else {
					printf("Server | listening on ip: %d.%d.%d.%d and port: %d*256 + %d\n", h1, h2, h3, h4, p1, p2);
				}
				
				// send port as a response to client
				char portString[80];
				char respString[256];
				 
				sprintf(respString, "227 entering passive mode (");
        		sprintf(portString, "%d,%d,%d,%d,%d,%d).\n", h1, h2, h3, h4, p1, p2);
        		strcat(respString, portString);
        
				if (send(new_fd, respString, strlen(respString)+1, 0) < 0) {
					printf("Error | server: entering PASV mode\n");
				}

                inPASV = 1;

		return 0; 
	}

	if (strcasecmp(command, "NLST") == 0) {
     if (inPASV == 0) {
          if (send(new_fd, "425 Enter passive mode first.\n", 30, 0) < 0) {
              printf("Error | server: NLST\n");
          }
          return 0;
      } else if (argument != NULL) {
          if (send(new_fd, "501 Do not include parameters.\n", 31, 0) < 0) {
              printf("Error | server: NLST\n");
          }
          return 0;
      }

      struct sockaddr_storage their_conn_addr; 
		  socklen_t new_sin_size;
		  new_sin_size = sizeof(their_conn_addr);
		  printf("middle of NLST: pasvsock_fd: %d\n", pasvsock_fd);
		  new_pasv_fd = accept(pasvsock_fd, (struct sockaddr *)&their_conn_addr, &new_sin_size);
		  if (new_pasv_fd < 0) {
			   printf("error | server: accept\n");
		  }

      if (send(new_fd, "125 Transferring data.\n", 23, 0) < 0) {
          printf("Error | server: Transferring data\n");
          return 0;
      }
      
      char cwd[BUFFER_SIZE];
      getcwd(cwd, BUFFER_SIZE);
      listFiles(new_pasv_fd, cwd);
      inPASV = 0;
      close(new_pasv_fd);
      new_pasv_fd = -1;
      close(pasvsock_fd);
      pasvsock_fd = -1;

      if (send(new_fd, "226 Closing data connection. Requested file action successful.\n", 63, 0) < 0) {
          printf("Error | server: closing connection\n");
          return 0;
      }

		  return 0; 
	}
 
    if (strcasecmp(command, "MODE") == 0) {
        if (argument == NULL) {
            if (send(new_fd, "501 Missing MODE parameter.\n", 28, 0) < 0) {
                printf("Error | server: MODE\n");
            }
            return 0;
        }

        if (strcasecmp(argument, "S") == 0) {
            if (send(new_fd, "200 Stream mode is supported and changed to.\n", 45, 0) < 0) {
                printf("Error | server: Stream mode\n");
            }
        } else {
            if (send(new_fd, "504 Modes other than Stream are not supported.\n", 47, 0) < 0) {
                printf("Error | server: Other mode\n");
            }
        }
        return 0;
    }

	if (strcasecmp(command, "QUIT") == 0) {
        if (send(new_fd, "Goodbye.\n", 9, 0) < 0) {
            printf("Error | Server: Quit\n");
        }
		char cwd[256];
	    if (getcwd(cwd, sizeof(cwd)) != NULL) {
		    printf("Current working dir: %s\n", cwd);
	    } else {
		    perror("getcwd() error");
		    return 1;
	    }
		while (strcmp(rootDir, cwd) != 0) {
      chdir("../");

      memset(&cwd, 0, sizeof(cwd));

      if (getcwd(cwd, sizeof(cwd)) != NULL) {
          printf("Current working dir: %s\n", cwd);
      } else {
          perror("getcwd() error");
          return 1;
      }
			
		}

        userLoggedIn = 0;
        close(new_fd);
    }

    if (send(new_fd, "500 command not supported.\n", 27, 0) < 0) {
        printf("Error | server: Unsupported command\n");
    }
    return 0;

}

// Here is an example of how to use the above function. It also shows
// one how to get the arguments passed on the command line.

int main(int argc, char *argv[]) {

    // This is some sample code feel free to delete it
    // This is the main program for the thread version of nc
    
    // Check the command line arguments
    if (argc != 2) {
      usage(argv[0]);
      return -1;
      printf("Number of arguments %d\n", argc);
    }

    port = atoi(argv[1]);
    printf("Port number is %d\n", port);
	char rootDir[256];
    if (getcwd(rootDir, sizeof(rootDir)) != NULL) {
        printf("Current working dir: %s\n", rootDir);
    } else {
        perror("getcwd() error");
        return 1;
    }

    // This is how to call the function in dir.c to get a listing of a directory.
    // It requires a file descriptor, so in your code you would pass in the file descriptor 
    // returned for the ftp server's data connection
    
    printf("Printed %d directory entries\n", listFiles(1, "."));

    // 1. Create a TCP socket
    // 2. Assign a port to socket (bind)
    // 3. Set socket to listen
    // 4. Repeatedly: a) Accept new connection, b) communicate, and c) close the connection

    struct addrinfo hints, *servinfo, *p; // http://beej.us/guide/bgnet/html/single/bgnet.html#getaddrinfo
    struct sockaddr_storage their_addr; // connector's address information

    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family   = AF_UNSPEC;   // use IPv4 or IPv6, whichever is available
    hints.ai_socktype = SOCK_STREAM; // create a stream (TCP) socket server
    hints.ai_flags    = AI_PASSIVE;  // use any available connection - assign the address of my local host to the socket structures
    socklen_t sin_size;
    int yes = 1;
    int status;

    // Gets information about available socket types and protocols
    if ((status = getaddrinfo(NULL, argv[1], &hints, &servinfo)) != 0) {
        fprintf(stderr, "Getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }

    // servinfo now points to a linked list of 1 or more struct addrinfos
    // loop through all the results and bind to the first we can
  for(p = servinfo; p != NULL; p = p->ai_next) {
    
    // create socket object
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("Server: socket");
      continue;
    }
    
    // specify that, once the program finishes, the port can be reused by other processes
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
      perror("Setsockopt");
      exit(1);
    }
    
    // bind to the specified port number
    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("Server: bind");
      continue;
    }
    
    // if the code reaches this point, the socket was properly created and bound
    break;
  }

	// print servers ip
	char initStr[256];
	printf("after getaddrinfo\n");
	gethostname(initStr, sizeof(initStr));
	printf("hostname: %s", initStr);		
	struct hostent *he;
	struct in_addr **addr_list;
	if ((he = gethostbyname(initStr)) == NULL) { // get the host info
		 herror("gethostbyname");
		 return 2;
	 }
	 // print information about this host:
	 printf("Official name is: %s\n", he->h_name);
	 printf(" IP addresses: ");
	 addr_list = (struct in_addr **)he->h_addr_list;
	 for(int i = 0; addr_list[i] != NULL; i++) {
		printf("%s ", inet_ntoa(*addr_list[i]));
	 }
	 printf("\n");

    freeaddrinfo(servinfo); // free the linked-list

    printf("Server: waiting for connections...\n");

    char buffer[BUFFER_SIZE];

    while(1) {
        // wait for new client to connect

        printf("Info | server: started listening\n");
        listen(sockfd, 5);

        printf("Info | server: accepting connections\n");
        sin_size = sizeof(their_addr);
        new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
        if (new_fd < 0) {
            printf("error | server: accept\n");
            continue;
        }

        printf("Info | server: sending 220 FTP server ready.\n");
        printf("rootDir after connection %s\n", rootDir);
        if(send(new_fd, "220 Service ready for new user. Please log in.\n", 47, 0) < 0) {
            printf("Error | server: sending 220\n");
            continue;
        }
	
        while(1) {
            printf("Reading input from client\n");
            memset(buffer, 0, BUFFER_SIZE);
	        int rec = recv(new_fd, &buffer, BUFFER_SIZE-1, 0);

		if (rec < 0) {
			printf("Error | server: reading from socket\n");
			close(new_fd);
			break;
		}
		printf("%d\n", rec);

		if (ftpCommand(buffer, rec, rootDir) == -1) {
			printf("Goodbye.\n");
			break;
		}
	   }

    }

    return 0;

}
