//Written by Max Bonifacio
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define IP_LENGTH 16
#define NUM_INPUTS 3
#define IP_INDEX 1
#define PORT_INDEX 2
#define MAX_PLAYERS 2
#define CLIENT_ADDRESS_STRING_SIZE 128
#define MAX_KEYWORDS_PER_PLAYER 100
#define MAX_KEYWORD_SIZE 512
#define BUFFER_SIZE 2049
#define MAX_REQUEST_TYPE 128
#define MAX_REQUESTED_FILE 512

//HTTP header constants, from 'http-server.c' sample code.
static char const* const HTTP_200_FORMAT = "HTTP/1.1 200 OK\r\n\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n\r\n";
static char const* const HTTP_200_FORMAT_C = "HTTP/1.1 200 OK\r\n\
Content-Type: text/html\r\n\
Content-Length: %ld\r\n";
static char const * const HTTP_400 = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_400_LENGTH = 47;
static char const * const HTTP_404 = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
static int const HTTP_404_LENGTH = 45;
static char const* const COOKIES = "cookies";
static char const* const COOKIE = "Set-Cookie: id=";

//Game flow functions
void handle_request(int stage, char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* username, char* request_type, char* cookies[]);
void handle_stage_zero(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* username, char* cookies[]);
void handle_stage_one(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* username, char* cookies[]);
void handle_stage_two(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* request_type);
void handle_stage_three(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* request_type);
void handle_stage_four(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player);
void handle_stage_five(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player);
void handle_stage_six(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* request_type);

//Game flow helper functions
int getServerAddress(int *port, char IP[], int argc, char *argv[]);
void kill_player(int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player);
int send_file(char filename[], int receiversockfd, char buff[]);
void send_accepted(int cur_player, int nkwords[], char*** kwords, int playersstage[], int fd);
void send_404(int fd);
void send_400(int fd);
void player_quit(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player);
int num_players(int players[]);
int get_player(int players[], int playerfd);
void remove_player(int players[], int playerfd);
int add_player(int players[], int newplayerfd);
int other_player(int this_player);
void send_to_stage(char *stage, char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player);

//String reading and manipulation functions
void determine_request(char *curr_request, char *request_type);
int get_cookie(char* request);
int check_victory(char ***kwords, int nkwords[]);
void reset_kword_of_player(char ***kwords, int nkwords[], int to_reset);
void reset_kwords(char ***kwords, int nkwords[]);
void add_keyword(char ***kwords, int player, int nkwords[], char* keyword);
void insert_text(char* main, char* inserted, int where);

//Functions for rotating the image
int cycle(int i);
void change_image();
void change_image_of_file(char* filename, int index);

void main(int argc, char *argv[]) {
  //Variables needed for networking stuff.
  int sockfd, newsockfd, maxfd;
  struct sockaddr_in servaddr, cliaddr;
  socklen_t cliaddr_len=sizeof(cliaddr);
  fd_set masterfds, readfds;
  FD_ZERO(&masterfds);
  int const reuse=1;
  char IP[IP_LENGTH];
  int port;

  //Variables needed for game logic.
  int players[MAX_PLAYERS]={-1,-1};
  int nkwords[MAX_PLAYERS]={0,0};
  int playersstage[MAX_PLAYERS]={0,0};
  char*** kwords;
  kwords=(char***)malloc(sizeof(char***)*2);
  int cur_player, cur_stage;
  char request_type[MAX_REQUEST_TYPE], buffer[BUFFER_SIZE], *curr_request, requested_file[MAX_REQUESTED_FILE];
  int n;
  char* username;
  char* cookies[50];
  cookies[0]=(char*)malloc(sizeof(char*));
  cookies[0]="\0";


  //Fill IP and port from command line input.
  if (!getServerAddress(&port, IP, argc, argv)) {
    perror("error on address input");
    exit(EXIT_FAILURE);
  }

  //Open an internet, TCP socket.
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    perror("error on socket creation");
    exit(EXIT_FAILURE);
  }

  //Try and reuse previous socket to prevent errors on start up.
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
    perror("error on reuse");
    exit(EXIT_FAILURE);
  }

  //Create the server address and bind it to the socket.
  memset(&servaddr, 0, sizeof(servaddr));
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = inet_addr(IP);
  servaddr.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
    perror("error on bind");
    exit(EXIT_FAILURE);
  }

  //Listen for connections.
  listen(sockfd, 2);
  maxfd=sockfd;
  FD_SET(sockfd, &masterfds);


  //Main server loop.
  while(1) {
    //Determine if something is ready to happen
    readfds=masterfds;

    if (select(FD_SETSIZE, &readfds, NULL, NULL, NULL) < 0) {
      perror("error on select");
      exit(EXIT_FAILURE);
    }
    //Look through every possible file descriptor between 0 and the maximum.
    for (int i = 0; i<=maxfd; ++i) {
      //If the current file descriptor is actually being used...
      if (FD_ISSET(i, &readfds)) {
        printf("\n%d %d\n", players[0], players[1]);
        //If we're looking at the file descriptor which is listening for connections...
        if (i==sockfd) {
          //If we're not max out in terms of players, accept it.
          if (num_players(players) < MAX_PLAYERS) {
            newsockfd=accept(sockfd, (struct sockaddr*)&cliaddr, &cliaddr_len);

            if (newsockfd < 0) {
              perror("error on accept");
            }

            else if (add_player(players, newsockfd)>0) {
              FD_SET(newsockfd, &masterfds);

              //Update max FD tracker if necessary
              if (newsockfd > maxfd) {
                maxfd = newsockfd;
              }
              char newip[INET_ADDRSTRLEN];
              printf("connection received from %s on socket %d\n", inet_ntop(cliaddr.sin_family, &cliaddr.sin_addr, newip, INET_ADDRSTRLEN), newsockfd);
            }
          }
          else {
            printf("player tried to join but game was full\n");
          }
        }
        //We're lookng at a file descriptor which has a request.
        else {
          int cur_fd=i;
          cur_player=get_player(players, cur_fd);
          cur_stage=playersstage[cur_player];

          n = read(cur_fd, buffer, 2049);
          if (n <= 0) {
            if (n < 0) {
              perror("error on read");
            } else {
              printf("socket %d closed the connection\n", cur_fd);
              kill_player(players, cur_fd, nkwords, kwords, playersstage, &masterfds, cur_player);
            }
          }

          buffer[n] = 0;
          curr_request = buffer;
          memset(request_type, 0, MAX_REQUEST_TYPE);
          determine_request(curr_request, request_type);
          printf("\n%s\n", curr_request);





          //Handle the request.
          if (strstr(curr_request, "favicon.ico")) {
            send_404(cur_fd);
          } else {
            handle_request(cur_stage, buffer, players, cur_fd, nkwords, kwords, playersstage, &masterfds, cur_player, username, request_type, cookies);
          }

        }
      }
    }
  }
}


//Wrapper function which splits up the game flow into multiple functions.
void handle_request(int stage, char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* username, char* request_type, char* cookies[]) {
  if (stage == 0) {
    handle_stage_zero(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player, username, cookies);
  }
  else if (stage == 1) {
    handle_stage_one(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player, username, cookies);
  }
  else if (stage == 2) {
    handle_stage_two(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player, request_type);
  }
  else if (stage == 3) {
    handle_stage_three(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player, request_type);
  }
  else if (stage == 4) {
    handle_stage_four(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
  else if (stage == 5) {
    handle_stage_five(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
  else if (stage == 6) {
    handle_stage_six(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player, request_type);
  }
}


//Case where a player hasn't been sent anything yet. Simply send them to the intro page.
void handle_stage_zero(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* username, char* cookies[]) {
  int cookie = get_cookie(buffer);
  if (cookie==-1) {
    send_to_stage("1_intro.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
  else {
    username = malloc(sizeof(cookies[cookie]));
    strcpy(username, cookies[cookie]);
    char welcome_line[100] = "<p>Welcome, ";
    strcat(welcome_line, username);
    strcat(welcome_line, "!</p>\n\n");
    int null_loc = strlen("<p>Welcome, ")+strlen(username)+strlen("!</p>\n\n");
    welcome_line[null_loc]='\0';

    //Update the player's stage.
    playersstage[cur_player]=2;

    //Send the header and body.
    int htmlfd=open("2_start.html", O_RDONLY);
    char html[10000];
    int n=read(htmlfd, html, 10000);
    html[n]='\0';

    insert_text(html, welcome_line, 239);
    struct stat st;
    stat("2_start.html", &st);
    char buff[10000];

    int k=sprintf(buff, HTTP_200_FORMAT, st.st_size+strlen(welcome_line));
    write(fd, buff, k);
    write(fd, html, n+strlen(welcome_line));
  }
}

//Player entered their username.
void handle_stage_one(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* username, char* cookies[]) {
  //Read the username and construct the welcome line to insert into the response body.
  username = strstr(buffer, "user=") + 5;
  int i=0;
  while (strcmp(cookies[i], "\0")!=0) {
    i++;
  }

  cookies[i]=(char*)malloc(sizeof(username));
  strcpy(cookies[i], username);
  strcat(cookies[i], "\0");
  cookies[i+1]="\0";

  char welcome_line[100] = "<p>Welcome, ";
  strcat(welcome_line, username);
  strcat(welcome_line, "!</p>\n\n");
  int null_loc = strlen("<p>Welcome, ")+strlen(username)+strlen("!</p>\n\n");
  welcome_line[null_loc]='\0';

  char cookie_line[100];
  strcpy(cookie_line, COOKIE);
  char cookie_str[100];
  sprintf(cookie_str, "%d", i);
  strcat(cookie_line, cookie_str);
  strcat(cookie_line, "\r\n\r\n");

  //Update the player's stage.
  playersstage[cur_player]=2;

  //Send the header and body.
  int htmlfd=open("2_start.html", O_RDONLY);
  char html[10000];
  int n=read(htmlfd, html, 10000);
  html[n]='\0';

  insert_text(html, welcome_line, 239);
  struct stat st;
  stat("2_start.html", &st);
  char buff[10000];
  int k=sprintf(buff, HTTP_200_FORMAT_C, st.st_size+strlen(welcome_line));
  strcat(buff, cookie_line);
  write(fd, buff, k+strlen(cookie_line));
  write(fd, html, n+strlen(welcome_line));
}

//Player has option to start the game or leave.
void handle_stage_two(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* request_type) {
  //If the player wants to start, reset the keywords for that player incase a previous round has been played, and sen them to their first turn.
  if (strcmp(request_type, "GET")==0) {
    send_to_stage("3_first_turn.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
  //If the player clicked quit, then send them to gameover and reset everything for that player.
  else if (strcmp(request_type, "POST")==0) {
    player_quit(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
}

void handle_stage_three(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* request_type) {
  char* keyword;
  if (strcmp(request_type, "POST")==0) {
    if (keyword=strstr(buffer, "keyword=")) {
      keyword=strstr(keyword, "=")+1;
      //If the other player has clicked start, then add this player's guess.
      if (playersstage[other_player(cur_player)]==3||playersstage[other_player(cur_player)]==4||playersstage[other_player(cur_player)]==5) {
        kwords[cur_player]=(char**)malloc(sizeof(char**));
        add_keyword(kwords, cur_player, nkwords, keyword);

        //Check for victory, if no one has won yet then accept this player's guess.
        if (check_victory(kwords, nkwords)==1) {
          send_to_stage("6_endgame.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
        }
        else {
          //Send HTML modified with the player's current guesses
          send_accepted(cur_player, nkwords, kwords, playersstage, fd);
        }
      }

      else {
          send_to_stage("5_discarded.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
      }
    }
    else {
      player_quit(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
    }
  }
}

//A guess was accepted, game is going.
void handle_stage_four(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player) {
  char* keyword;
  if (keyword=strstr(buffer, "keyword=")) {
    keyword=strstr(keyword, "=")+1;
    add_keyword(kwords, cur_player, nkwords, keyword);
    if (check_victory(kwords, nkwords)==1) {
      send_to_stage("6_endgame.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
    }
    else {
      //Send HTML modified with the player's current guesses
      send_accepted(cur_player, nkwords, kwords, playersstage, fd);
    }
  }
  else {
    player_quit(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
}

//The player's guess was denied.
void handle_stage_five(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player) {
  char* keyword;
  if (keyword=strstr(buffer, "keyword=")) {
    keyword=strstr(keyword, "=")+1;
    //Accept their guess if the other player is ready.
    if (playersstage[other_player(cur_player)]==3||playersstage[other_player(cur_player)]==4||playersstage[other_player(cur_player)]==5) {
      add_keyword(kwords, cur_player, nkwords, keyword);
      if (check_victory(kwords, nkwords)==1) {
        send_to_stage("6_endgame.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
      }
      else {
        send_accepted(cur_player, nkwords, kwords, playersstage, fd);
      }
    }
    //Otherwise deny them again.
    else {
      send_to_stage("5_discarded.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
    }
  }
  else {
    player_quit(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
}

//The round was won.
void handle_stage_six(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player, char* request_type) {
  //If the request is a get request, the player wants to play the game again with a different image.
  if (strcmp(request_type, "GET")==0) {
    reset_kword_of_player(kwords, nkwords, cur_player);

    //Reset the image only if the other player hasn't.
    if (playersstage[other_player(cur_player)]!=3 && playersstage[other_player(cur_player)]!=5) {
      change_image();
    }
    send_to_stage("3_first_turn.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
  //Otherwise exit.
  else if (strcmp(request_type, "POST")==0) {
    player_quit(buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
}

//--------------------- HELPER FUNCTIONS WHICH ARE NOT DIRECTLY RELATED TO GAME FLOW --------------------

//Resets everything about a player.
void kill_player(int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player) {
  remove_player(players, fd);
  FD_CLR(fd, &(*masterfds));
  close(fd);
  playersstage[cur_player]=0;
  reset_kword_of_player(kwords, nkwords, cur_player);
}

//Function inserts the list of keywords for the cur_player into the accepted HTML and then sends it to them.
void send_accepted(int cur_player, int nkwords[], char*** kwords, int playersstage[], int fd) {
  //Construct the string to insert
  char keywords_string[10000] = "<p>Guesses:";
  for (int i=0; i<nkwords[cur_player]; i++) {
    strcat(keywords_string, " ");
    strcat(keywords_string, kwords[cur_player][i]);
    if (i>=1) {
      strcat(keywords_string, ",");
    }
  }
  strcat(keywords_string, "</p>\n\n");

  struct stat st;
  stat("4_accepted.html", &st);

  //Open the HTML file.
  int htmlfd=open("4_accepted.html", O_RDONLY);
  char html[10000];
  int n=read(htmlfd, html, 10000);
  html[n]='\0';
  insert_text(html, keywords_string, 491);

  //Get the header.
  char buff[10000];
  int k=sprintf(buff, HTTP_200_FORMAT, st.st_size+strlen(keywords_string));

  //Update the players stage.
  playersstage[cur_player]=4;

  //Send the header and body directly after.
  write(fd, buff, k);
  write(fd, html, strlen(html));
}


void send_404(int fd) {
  write(fd, HTTP_404, HTTP_404_LENGTH);
}

void send_400(int fd) {
  write(fd, HTTP_400, HTTP_400_LENGTH);
}

//Sends a player a file, updates their tracker.
void send_to_stage(char *stage, char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player) {
  if (send_file(stage, fd, buffer)==1) {
    playersstage[cur_player]=stage[0]-'0';
  }

  //Remove the player upon error.
  else {
    kill_player(players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  }
}


//This is called whenever a player clicks on quit. Sends them the game_over html and clears any information stored about them.
void player_quit(char* buffer, int players[], int fd, int nkwords[], char*** kwords, int playersstage[], fd_set* masterfds, int cur_player) {
  send_to_stage("7_gameover.html", buffer, players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
  kill_player(players, fd, nkwords, kwords, playersstage, masterfds, cur_player);
}

int other_player(int this_player) {
  return 1 - this_player;
}

//Send a file to a socket. Automatically applies the header.
int send_file(char filename[], int receiversockfd, char buff[]) {
  struct stat st;
  stat(filename, &st);
  //Get the header
  int n=sprintf(buff, HTTP_200_FORMAT, st.st_size);
  //Send the header
  if (write(receiversockfd, buff, n) < 0) {
    perror("error on write");
    return -1;
  }
  //Open the file to send
  int filefd=open(filename, O_RDONLY);
  do {
    //Attempt to send the file
    n=sendfile(receiversockfd, filefd, NULL, 2048);
  }
  while (n>0);
  //Close everything up.
  if (n<0) {
    perror("error on send");
    close(filefd);
    return -1;
  }
  close(filefd);
  return 1;
}



//Get the players index from their file descriptor.
int get_player(int players[], int playerfd) {
  for (int i=0; i<MAX_PLAYERS; i++) {
    if (players[i]==playerfd) {
      return i;
    }
  }
  return -1;
}

//Remove a player by their file descriptor.
void remove_player(int players[], int playerfd) {
  for (int i=0; i<MAX_PLAYERS; i++) {
    if (players[i]==playerfd) {
      players[i]=-1;
      return;
    }
  }
  return;
}

//Add a player to the next available slot.
int add_player(int players[], int newplayerfd) {
  for (int i=0; i<MAX_PLAYERS; i++) {
    if (players[i]==-1) {
      players[i]=newplayerfd;
      return 1;
    }
  }
  return 0;
}

//Count all the players.
int num_players(int players[]) {
  int count=0;
  for (int i=0; i<MAX_PLAYERS; i++) {
    if (players[i]!=-1) {
      count++;
    }
  }
  return count;
}

int getServerAddress(int *port, char IP[], int argc, char *argv[]) {
  //Return 0 if there weren't enough arguments specified, to indicate error.
  if (argc<NUM_INPUTS) {
    return 0;
  } else {
    strcpy(IP, argv[IP_INDEX]);
    *port=atoi(argv[PORT_INDEX]);
    printf("Server running with on %s:%d\n", IP, *port);
  }
  //Return 1 to indicate all is well.
  return 1;
}

//-----------------------------------------------------------------------------------


//--------------------- FUNCTIONS USED TO CHANGE AN IMAGE ---------------------------
//Change_image() calls change_image_of_file for every relevant file.
void change_image() {
  change_image_of_file("3_first_turn.html", 181);
  change_image_of_file("4_accepted.html", 198);
  change_image_of_file("5_,discarded.html", 216);
}

//Find and change the image number, write it on the file.
void change_image_of_file(char* filename, int index) {
  int fd = open(filename, O_RDONLY);
  char buffer[2049];
  int n = read(fd, buffer, 2048);
  buffer[n]='\0';

  char buffer_copy[2049];
  strcpy(buffer_copy, buffer);
  char* cur_image_str=strstr(buffer_copy, "image-")+6;
  cur_image_str[1]='\0';
  int cur_image=atoi(cur_image_str);
  int next_image=cycle(cur_image);
  char next_image_str[10];
  sprintf(next_image_str, "%d", next_image);


  buffer[index]=next_image_str[0];

  fd = open(filename, O_WRONLY);
  write(fd, buffer, strlen(buffer));

}

int cycle(int i) {
  if (i==4) {
    return 1;
  }
  else {
    return (i+1);
  }
}

//-----------------------------------------------------------------------------------


//--------------------- FUNCTIONS USED FOR MANIPULATING AND READING STRINGS ---------
//Determine the request from an HTTP request.
void determine_request(char *curr_request, char *request_type) {
  int marker_index=0;
  char marker=curr_request[marker_index];
  while (marker!=' ') {
    request_type[marker_index]=marker;
    marker_index++;
    marker=curr_request[marker_index];
  }
  request_type[marker_index]='\0';
}

//Tries to get a cookie from a request, and returns -1 if one wasn't found.
int get_cookie(char* request) {
  char* cookie_str;
  if (!(cookie_str = strstr(request, "Cookie: "))) {
    return -1;
  } else {
    cookie_str = strstr(cookie_str, "id=")+3;
    char *marker;
    marker = strchr(cookie_str, ';');
    if (marker != NULL) {
      *marker = '\0';
    int cookie;
    sprintf(cookie_str, "%d", cookie);
    return cookie;
    }
  }
}

//Checks victory condition by searching for matches between the keyword lists.
int check_victory(char ***kwords, int nkwords[]) {
  for (int i=0; i<nkwords[0]; i++) {
    for (int j=0; j<nkwords[1]; j++) {
      if (strcmp(kwords[0][i], kwords[1][j])==0) {
        return 1;
      }
    }
  }
  return 0;
}

//Reset the tracker for both player's guesses.
void reset_kwords(char ***kwords, int nkwords[]) {
  reset_kword_of_player(kwords, nkwords, 0);
  reset_kword_of_player(kwords, nkwords, 1);

}

//Reset the track for a player's guesses.
void reset_kword_of_player(char ***kwords, int nkwords[], int to_reset) {
  for (int i=0; i<nkwords[to_reset]; i++) {
    kwords[to_reset][i][0]='\0';
    free(kwords[to_reset][i]);
  }
  free(kwords[to_reset]);
  nkwords[to_reset]=0;
}

//Inserts a string into another string at a given location, used for modifying HTML.
void insert_text(char* main, char* inserted, int where) {
  char final[1000];
  strncpy(final, main, where);
  final[where] = '\0';
  strcat(final, inserted);
  strcat(final, main+where);
  strcpy(main, final);
}

//Adds a keyword to the keyword tracker for the player.
void add_keyword(char ***kwords, int player, int nkwords[], char* keyword) {
  //Isolate just what was entered
  char* marker;
  marker = strchr(keyword, '&');
  if (marker != NULL) {
    *marker = '\0';
  }
  //Allocate space for the keyword.
  kwords[player]=realloc(kwords[player], (sizeof(char**)*(nkwords[player]+1)));
  kwords[player][nkwords[player]]=(char *)malloc(sizeof(keyword));

  //Copy it and update the counts.
  strcpy(kwords[player][nkwords[player]], keyword);
  nkwords[player]++;
}

//-----------------------------------------------------------------------------------
