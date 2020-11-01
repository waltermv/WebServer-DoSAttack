//gcc -o PreThreadedServer PreThreadedServer.c -pthread -Wall -g

//https://www.youtube.com/watch?v=BIJGSQEipEE
//https://www.youtube.com/watch?v=bdIiTxtMaKA
//https://github.com/jonhoo/pthread_pool
//https://stackoverflow.com/questions/28631767/sending-images-over-http-to-browser-in-c imagenes
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_BACKLOG 10
#define MAXLINE 1024
#define HEADERSPACE 450
#define METHODLINE 10
#define URLLINE 255
#define PATHLINE 255

#define SERVER_STRING "Server: PreThreadedServer\r\n"

#define BAD_REQUEST "HTTP/1.1 400 Bad Request\r\n"\
                    SERVER_STRING\
                    "Content-type: text/html\r\n"\
                    "\r\n"\
                    "<P>Your browser sent a bad request, "\
                    "such as a POST without a Content-Length.\r\n"

#define NOT_FOUND "HTTP/1.1 404 Not Found\r\n"\
                  SERVER_STRING\
                  "Content-Type: text/html\r\n"\
                  "\r\n"\
                  "<HTML><TITLE>Not Found</TITLE>\r\n"\
                  "<BODY><P>The server could not fulfill "\
                  "your request because the resource specified "\
                  "is unavailable or nonexistent."\
                  "</BODY></HTML>\r\n"

#define UNIMPLEMENTED "HTTP/1.1 501 Method Not Implemented\r\n"\
                      SERVER_STRING\
                      "Content-Type: text/html\r\n"\
                      "\r\n"\
                      "<HTML><HEAD><TITLE>Method Not Implemented\r\n"\
                      "</TITLE></HEAD>\r\n"\
                      "<BODY><P>HTTP request method not supported.\r\n"\
                      "</BODY></HTML>\r\n"

#define CANNOT_EXECUTE "HTTP/1.1 500 Internal Server Error\r\n"\
                        SERVER_STRING\
                       "Content-type: text/html\r\n\r\n"\
                       "<P>Error prohibited execution.\r\n"

#define FULL_QUEUE "HTTP/1.1 503 Service Unavailable\r\n"\
                    SERVER_STRING\
                   "Content-Type: text/html\r\n\r\n"\
	               "<HTML><TITLE>Service Unavailable</TITLE>\r\n"\
                   "<HEAD><meta http-equiv=\"refresh\" content=\"15\"></HEAD>"\
                   "<BODY><P>Server currently unavailable due to overload."\
                   "</BODY></HTML>\r\n"

//https://people.cs.nctu.edu.tw/~cjtsai/courses/ics/homework/nweb.c
struct {
	char   *ext;
	char   *mime;
}
extensions[] = {
	{"gif", "image/gif"},
	{"jpg", "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{"mp3", "audio/mpeg"},
	{"mp4", "audio/mpeg"},
	{"avi", "video/x-msvideo"},
	{"ico", "image/vnd.microsoft.icon"},
	{"zip", "application/zip"},
	{"gz", "application/gzip"},
	{"tar", "application/x-tar"},
	{"txt", "text/plain"},
	{"json", "application/json"},
	{"doc", "application/msword"},
	{"docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{0, 0}
};

typedef struct node {
	struct node* next;
	int *client;
}node_thread;

node_thread *node_head = NULL;
node_thread *node_tail = NULL;
int queue_quantity = 1;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;

void enqueue(int *client_socket);
int *dequeue();
void *thread_function(void *arg);
int get_line(int sock, char *buf, int size);
void headers(int client, int length, const char *fileType);
void send_queue_position(int client, int position);
void send_file(int client, const char *path, int length);
void execute_file(int client, const char *path,
         const char *method, const char *query_string);
void *handle_connection(int *pclient);

void enqueue(int *client_socket) {
	node_thread *new_node = malloc(sizeof(node_thread));
	new_node->client = client_socket;
	new_node->next = NULL;
	if(node_tail == NULL) node_head = new_node;
	else node_tail->next = new_node;
	node_tail = new_node;//mutex?
	queue_quantity++;
}

int *dequeue() {
    if(node_head == NULL) {
        return NULL;
    }
    else {
        int *result = node_head->client;
        node_thread *temp = node_head;
        node_head = node_head->next;
        if(node_head == NULL) node_tail = NULL;
        free(temp);
        return result;
    }
}

void *thread_function(void *arg) {
    while(1) {
        int *pclient;
        pthread_mutex_lock(&mutex);
        if((pclient = dequeue()) == NULL) { //Comprobar si no hay trabajo pendiente
            pthread_cond_wait(&condition_var, &mutex);//Se pasa el mutex para que libere el Log
            pclient = dequeue(); //Hacer trabajo si se recibió el signal
        }
        pthread_mutex_unlock(&mutex);

        handle_connection(pclient); //Se atiende la conexión
        queue_quantity--;
    }
}

/**********************************************************************/
/* Get a line from a socket, whether the line ends in a newline,
 * carriage return, or a CRLF combination.  Terminates the string read
 * with a null character.  If no newline indicator is found before the
 * end of the buffer, the string is terminated with a null.  If any of
 * the above three line terminators is read, the last character of the
 * string will be a linefeed and the string will be terminated with a
 * null character.
 * Parameters: the socket descriptor
 *           the buffer to save the data in
 *           the size of the buffer
 * Returns: the number of bytes stored (excluding null) */
/**********************************************************************/
int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {
        n = recv(sock, &c, 1, 0);
        /* DEBUG printf("%02X\n", c); */
        if (n > 0) {
            if (c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK);//Solo una ojeada. El siguiente recv va a tomar lo mismo
                /* DEBUG printf("%02X\n", c); */
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);
                else
                    c = '\n';
            }
            buf[i] = c;
            i++;
        } else {
            c = '\n';
        }
    }
    buf[i] = '\0';

    return i;
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *           the name of the file */
/**********************************************************************/
void headers(int client, int length, const char *fileType) {
	char response_header[HEADERSPACE]; //Darle un espacio más razonable xd
	sprintf(response_header, "HTTP/1.1 200 OK\r\n%sAccept-Ranges: bytes\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n",
			SERVER_STRING, length, fileType);
	fprintf(stdout, "Encabezado de la respuesta:\n%s\n", response_header);

	send(client, response_header, strlen(response_header), 0);
}

/**********************************************************************/
/* Return the informational HTTP headers about a file. */
/* Parameters: the socket to print the headers on
 *           the name of the file */
/**********************************************************************/
void send_queue_position(int client, int position) {
	char response_header[HEADERSPACE];//Buscar el códigazo
	sprintf(response_header, "HTTP/1.1 503 Service Unavailable\r\n%sContent-Type: text/html\r\n\r\n"\
	              "<HTML><TITLE>Service Unavailable</TITLE>\r\n"\
                  "<HEAD><meta http-equiv=\"refresh\" content=\"15\"></HEAD>"\
                  "<BODY><P>You are in the queue position: %d."\
                  "</BODY></HTML>\r\n",
	        SERVER_STRING, position);
	fprintf(stdout, "Encabezado de la respuesta:\n%s\n", response_header);

	send(client, response_header, strlen(response_header), 0);
}

/**********************************************************************/
/* Send a regular file to the client.  Use headers, and report
 * errors to client if they occur.
 * Parameters: a pointer to a file structure produced from the socket
 *            file descriptor
 *           the name of the file to serve */
/**********************************************************************/
void send_file(int client, const char *path, int length) {
    int resource;
    int numChars = 1;
    char buff[MAXLINE];
	char *fileType;
	int pathLength;
	int extensionLength;

    buff[0] = 'A'; buff[1] = '\0';
    while ((numChars > 0) && strcmp("\n", buff)) /* read & discard headers */
        numChars = get_line(client, buff, MAXLINE);

    resource = open(path, O_RDONLY);
    if (resource == -1) { //Ya esto lo comprobé
        send(client, NOT_FOUND, strlen(NOT_FOUND), 0); //No encontrado
    }
    else {
	    pathLength = strlen(path);
	    fileType = (char *)"application/octet-stream"; //Mime por defecto
	    for(int i=0; extensions[i].ext != 0; ++i) {
		    extensionLength = strlen(extensions[i].ext);
		    if(!strncmp(&path[pathLength-extensionLength], extensions[i].ext, extensionLength)) {
			    fileType = extensions[i].mime;
			    break;
	    	}
	    }

	    headers(client, length, fileType);
		sendfile(client, resource, 0, length);
    }
    close(resource);
}

/**********************************************************************/
/* Execute a CGI script.  Will need to set environment variables as/////////////////////////////////
 * appropriate.
 * Parameters: client socket descriptor
 *           path to the CGI script */
/**********************************************************************/
void execute_file(int client, const char *path,
         const char *method, const char *query_string) {
    char buff[MAXLINE];
    int cgi_output[2];
    int cgi_input[2];
    pid_t pid;
    int status;
    int i;
    char c;
    int numChars = 1;
    int content_length = -1;

    buff[0] = 'A'; buff[1] = '\0';
    if(!strcasecmp(method, "GET")) {
        while ((numChars > 0) && strcmp("\n", buff))     /* read & discard headers */
            numChars = get_line(client, buff, sizeof(buff));
    } else if(!strcasecmp(method, "POST")) {  //POST 
        numChars = get_line(client, buff, sizeof(buff));
        while ((numChars > 0) && strcmp("\n", buff)) {
            buff[15] = '\0';
            if (strcasecmp(buff, "Content-Length:") == 0)
                content_length = atoi(&(buff[16]));
            numChars = get_line(client, buff, sizeof(buff));
        }
        if (content_length == -1) {
            send(client, BAD_REQUEST, sizeof(BAD_REQUEST), 0);
            return;
        }
    }

    if (pipe(cgi_output) < 0) {
        send(client, CANNOT_EXECUTE, strlen(CANNOT_EXECUTE), 0);
        return;
    }
    if (pipe(cgi_input) < 0) {
        send(client, CANNOT_EXECUTE, strlen(CANNOT_EXECUTE), 0);
        return;
    }

    if ((pid = fork()) < 0) {
        send(client, CANNOT_EXECUTE, strlen(CANNOT_EXECUTE), 0);
        return;
    }
    if (pid == 0) { /* child: CGI script */
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);
        dup2(cgi_input[0], 0);
        close(cgi_output[0]);
        close(cgi_input[1]);
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);
        if (strcasecmp(method, "GET") == 0) {
            sprintf(query_env, "QUERY_STRING=%s", query_string);
            putenv(query_env);
        } else { /* POST */
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);
            putenv(length_env);
        }
        execl(path, path, NULL);
        exit(0);
    } else { /* parent lo hace a la vez porque supongo que tarda*/
        close(cgi_output[1]);
        close(cgi_input[0]);
        if (strcasecmp(method, "POST") == 0) {
            for (i = 0; i < content_length; i++) {//<color=blue>
                recv(client, &c, 1, 0);
                write(cgi_input[1], &c, 1);
            }
        }

        sprintf(buff, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");
        fprintf(stdout, "Encabezado de la respuesta:\n%s\n", buff);

        send(client, buff, strlen(buff), 0);

        while (read(cgi_output[0], &c, 1) > 0)
            send(client, &c, 1, 0);

        close(cgi_output[0]);
        close(cgi_input[1]);
        waitpid(pid, &status, 0);
    }
}

void *handle_connection(int *pclient) {//handle
	int client = *pclient;
	free(pclient);
	char buff[MAXLINE]; //salida, entrada
	int numChars;
	char method[METHODLINE];
	char URL[URLLINE];
	char path[PATHLINE];
	size_t i=0, j=0; //Enteros sin signo para iterar
	char *queryString; //Puntero para iterar
	int execute = 0;
	struct stat st; //Información del archivo

	fprintf(stdout, "\nAtendiendo al cliente: %d\n", client);

	//Recuperamos el método a utilizar
	numChars = get_line(client, buff, MAXLINE);

	if(!strcmp(buff, "\0")) {
		fprintf(stdout, "\nCerrando la conexión con: %d\n", client);
		close(client);
		return NULL;
	}

	fprintf(stdout, "Solicitud del cliente %d:\n %s\n", client, buff); //Imprimimos el buffer

	while (!isspace(buff[i]) && (i < sizeof(method) - 1))
		method[i++] = buff[j++];
	method[i] = '\0';

	//No implementado
	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        	send(client, UNIMPLEMENTED, strlen(UNIMPLEMENTED), 0);
        	return NULL; //NULL
    	}

	if (strcasecmp(method, "POST") == 0)
        	execute = 1;

	//Recuperamos el URL
	i = 0;
    	while (isspace(buff[j]) && (j < MAXLINE))
        	j++;

    	while (!isspace(buff[j]) && (i < URLLINE - 1) && (j < MAXLINE))
        	URL[i++] = buff[j++];
    	URL[i] = '\0';

	//Comprobamos si tiene parámetros en la URL
	if (strcmp(method, "GET") == 0) {
		queryString = URL;
        	while ((*queryString != '?') && (*queryString != '\0'))
			queryString++;
        	if (*queryString == '?') {
			execute = 1;
			*queryString = '\0';//Se cambia el ? por vacio. Que genio xd
			queryString++;
        	}
    	}

	sprintf(path, "resources%s", URL);

	// Si nos piden un directorio le damos el index.html
	if (path[strlen(path) - 1] == '/')
        	strcat(path, "index.html");
	
	if (stat(path, &st) == -1) { //Si no lo encontramos
        	while ((numChars > 0) && strcmp("\n", buff)) /* read & discard headers */
        	    numChars = get_line(client, buff, sizeof(buff));
		fprintf(stderr, "Archivo no encontrado.\n");
        	send(client, NOT_FOUND, strlen(NOT_FOUND), 0);
	} 
	else { //Si lo encontramos
		if (S_ISDIR(st.st_mode)){//buscar esto xd
			sprintf(path, "index.html");
			stat(path, &st);}
            		//strcat(path, "/index.html"); ///Ya no se ocupa
        	else if ((st.st_mode & S_IXUSR) ||//Ejecutable como usr, grp y other
            	    (st.st_mode & S_IXGRP) ||//cgi-bin/
            	    (st.st_mode & S_IXOTH))
            		execute = 1;//cambiar a execute
        	if (!execute){
            		send_file(client, path, st.st_size);}
        	else{
        		execute_file(client, path, method, queryString);//binarios
		}
	}
	fprintf(stdout, "Cerrando la conexión con: %d\n", client);
	close(client);
	return NULL;
}

/*void sigint_handler(int signo) {
    printf("Signal handler called\n");
    for(int i=0; i < PREFORK_CHILDREN; i++)
        kill(pids[i], SIGTERM);
    while (wait(NULL) > 0);
    print_stats();
}*/

int main(int argc, char **argv) {
	int server_socket, client_socket; //server, conexión
	int port = 0;
	struct sockaddr_in servAddr, clientAddr;
	int addr_size = sizeof(clientAddr);

	int max_threads = 0;
	pthread_t *thread_pool;

    //signal(SIGINT, sigint_handler);

	if (argc !=  5) {
		fprintf(stderr, "Uso: %s -p puerto -n hilos\n", argv[0]);
		return (1);
	}

    	for(int i=1; i<argc; i+=2) { //Aumentar de 2 en 2
		if(!strcmp("-p", argv[i]))
			port = atoi(argv[i+1]);

		else if(!strcmp("-n", argv[i]))
			max_threads = atoi(argv[i+1]);
	}

	if(!port) {
		fprintf(stderr, "Debe especificar un puerto.\n");
		exit(1);
	}

	if(!max_threads) {
		fprintf(stderr, "Debe crear al menos 1 hilo.\n");
		exit(1);
	}

    thread_pool = malloc(max_threads*sizeof(pthread_t));

	for(int i=0; i<max_threads; ++i) {//Crearlo al final?
	    pthread_create(&thread_pool[i], NULL, thread_function, NULL);
	}

	if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "Error al crear el socket.\n");
		exit(1);
	}

	bzero(&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY); //Ponerle el localhost o dejarlo así?
	servAddr.sin_port = htons(port);

	if((bind(server_socket, (struct sockaddr*)&servAddr, sizeof(servAddr))) < 0) {
		fprintf(stderr, "Error al enlazar.\n");
		close(server_socket);
		exit(1);
	}

	if((listen(server_socket, SERVER_BACKLOG)) < 0) {//número de conexiones escuchando simultaneamente?
		fprintf(stderr, "Error al escuchar.\n");
		close(server_socket);
		exit(1);
	}

	sigaction(SIGPIPE, &(struct sigaction){SIG_IGN}, NULL); //Debido a que los request de chrome me joden

	while(1) {
		fprintf(stdout, "Esperando una conexión en el puerto %d\n", port);
		client_socket = accept(server_socket, (struct sockaddr*)&clientAddr, (socklen_t*)&addr_size);//Cerrar server_socket

		if(client_socket < 0){
			fprintf(stderr, "Error al conectarse.\n");
			continue; //Se continua con la ejecución del servidor.
		}
		fprintf(stdout, "Conexión aceptada de %s:%d\n Cliente: %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), client_socket);

        fprintf(stdout, "Cliente %d encuentra en la posición de la cola: %d\n", client_socket, queue_quantity);
        if(queue_quantity > max_threads){
            send(client_socket, FULL_QUEUE, strlen(FULL_QUEUE), 0);
            close(client_socket);
            continue;
        }

		int *pclient = malloc(sizeof(int));
		pthread_mutex_lock(&mutex);
		*pclient = client_socket;
		enqueue(pclient);
		pthread_cond_signal(&condition_var);
		pthread_mutex_unlock(&mutex);
	}

	pthread_mutex_destroy(&mutex);
	return 0;
}