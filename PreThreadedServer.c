/*
Autores:
Brandon Ledezma Fernández
Walter Morales Vásquez

Servidor capaz de atender a usuarios utilizando la técnica de Pre-Thread. Actualmente acepta solicitudes
de GET y POST. Además, es capaz de ejecutar archivos binarios que dan como resultado texto html.

Para compilar el programa:
gcc -o PreThreadedServer PreThreadedServer.c -pthread

Para ejecutar el programa:
./PreThreadedServer -p puerto -w dirección-carpeta-raíz -n cantidad-hilos
*/

#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define SERVER_BACKLOG 10       // Cantidad de conexiones que pueden estar pendientes.
#define MAXLINE 1024            // Largo utilizado en el buffer.
#define HEADERSPACE 450         // Espacio para los encabezados de las respuestas.
#define METHODLINE 10           // Espacio para almacenar el nombre del método utilizado por el cliente.
#define URLLINE 255             // Largo máximo para la URL dada por el cliente.
#define PATHLINE 255            // Largo máximo para ls dirección del recurso solicitado.

// Texto que identifica al servidor en las respuestas.
#define SERVER_STRING "Server: PreThreadedServer\r\n"

// Respuesta HTTP de código 400
#define BAD_REQUEST "HTTP/1.1 400 Bad Request\r\n"\
                    SERVER_STRING\
                    "Content-type: text/html\r\n"\
                    "\r\n"\
                    "<P>Your browser sent a bad request, "\
                    "such as a POST without a Content-Length.\r\n"

// Respuesta HTTP de código 404
#define NOT_FOUND "HTTP/1.1 404 Not Found\r\n"\
                  SERVER_STRING\
                  "Content-Type: text/html\r\n"\
                  "\r\n"\
                  "<HTML><TITLE>Not Found</TITLE>\r\n"\
                  "<BODY><P>The server could not fulfill "\
                  "your request because the resource specified "\
                  "is unavailable or nonexistent."\
                  "</BODY></HTML>\r\n"

// Respuesta HTTP de código 501
#define UNIMPLEMENTED "HTTP/1.1 501 Method Not Implemented\r\n"\
                      SERVER_STRING\
                      "Content-Type: text/html\r\n"\
                      "\r\n"\
                      "<HTML><HEAD><TITLE>Method Not Implemented\r\n"\
                      "</TITLE></HEAD>\r\n"\
                      "<BODY><P>HTTP request method not supported.\r\n"\
                      "</BODY></HTML>\r\n"

// Respuesta HTTP de código 500
#define CANNOT_EXECUTE "HTTP/1.1 500 Internal Server Error\r\n"\
                        SERVER_STRING\
                       "Content-type: text/html\r\n\r\n"\
                       "<P>Error prohibited execution.\r\n"

// Respuesta HTTP de código 503
#define FULL_QUEUE "HTTP/1.1 503 Service Unavailable\r\n"\
                    SERVER_STRING\
                   "Content-Type: text/html\r\n\r\n"\
	               "<HTML><TITLE>Service Unavailable</TITLE>\r\n"\
                   "<HEAD><meta http-equiv=\"refresh\" content=\"15\"></HEAD>"\
                   "<BODY><P>Server currently unavailable due to overload."\
                   "</BODY></HTML>\r\n"

// Estructura de datos que le indica al servidor cual tipo "mime" pertenece a cierta extensión.
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

// Dirección de la carpeta donde se comenzarán a recuperar los recursos.
char *root_path = "\0";

// Lista enlazada FIFO que almacena los clientes a atender.
typedef struct node {
	struct node* next;
	int *client;
}node_thread;

node_thread *node_head = NULL;      // Nodo cabeza de la lista enlazada.
node_thread *node_tail = NULL;      // Nodo cola de la lista enlazada.
int queue_quantity = 1;             // Cantidad de elementos de la lista.

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;          // Mutex que se utilizará para bloquear y
                                                            // desbloquear la zona crítica para los hilos.
pthread_cond_t condition_var = PTHREAD_COND_INITIALIZER;    // Variable condicional que permite el
                                                            // paso de señales a los hilos.

void enqueue(int *client_socket);   // Método para colocar un elemento en la lista.
int *dequeue();                     // Función para obtener un elemento de la lista.
void *thread_function(void *arg);   // Función en la que se encontrarán los hilos a lo largo de la ejecución.
int get_line(int sock, char *buf, int size);                    // Función para obtener una línea de un socket.
void headers(int client, int length, const char *fileType);     // Método que da un "header" de respuesta al cliente.
void send_file(int client, const char *path, int length);       // Función para enviarle un archivo al cliente.
void execute_file(int client, const char *path,                 // Utilizada para ejecutar un binario y enviarle
         const char *method, const char *query_string);         // la respuesta al cliente.
void *handle_connection(int *pclient);                          // Función que maneja la conexión con el cliente.

// Método para colocar un elemento en la lista.
void enqueue(int *client_socket) {
	node_thread *new_node = malloc(sizeof(node_thread));    // Se reserva un espacio para el nuevo nodo.
	new_node->client = client_socket;                       // Se almacena el cliente.
	new_node->next = NULL;
	if(node_tail == NULL) node_head = new_node;             // Se inserta al nodo en la cola.
	else node_tail->next = new_node;
	node_tail = new_node;
	++queue_quantity;                                       // Se incrementa el número que indica la cantidad
}                                                           // de nodos actualmente.

// Función para obtener un elemento de la lista.
int *dequeue() {
    if(node_head == NULL) {                                 // Si el nodo cabeza es nulo se retorna.
        return NULL;
    }
    else {                                                  // Si no se realizan operaciones para hacer que
        int *result = node_head->client;                    // el siguiente nodo sea la cabeza y se retorna
        node_thread *temp = node_head;                      // la cabeza original.
        node_head = node_head->next;
        if(node_head == NULL) node_tail = NULL;
        free(temp);
        return result;
    }
}

// Función en la que se encontrarán los hilos a lo largo de la ejecución.
void *thread_function(void *arg) {
    while(1) {
        int *pclient;
        pthread_mutex_lock(&mutex);                     // Se bloquea el hilo con el mutex.
        if((pclient = dequeue()) == NULL) {             // Si no hay trabajo pendiente.
            pthread_cond_wait(&condition_var, &mutex);  // Se bloquea hasta que reciba una señal.
            pclient = dequeue();                        // Una vez se recibe la señal se saca un nodo para
        }                                               // trabajar.
        pthread_mutex_unlock(&mutex);                   // Se desbloquea el mutex.

        handle_connection(pclient);                     // Se atiende la conexión.
        queue_quantity--;                               // Se disminuye en la cantidad de la cola.
    }
}

// Función para obtener una línea de un socket. Para hasta que encuentra una nueva línea, retorno de carro o
// avance de línea. Retorna el número de bytes almacenados.
int get_line(int sock, char *buf, int size) {
    int i = 0;
    char c = '\0';
    int n;

    while ((i < size - 1) && (c != '\n')) {             // Se recorre el largo del mensaje o hasta que se encuentre
        n = recv(sock, &c, 1, 0);                       // una nueva línea, retorno de carro o cambio de línea.
        if (n > 0) {
            if (c == '\r') {
                n = recv(sock, &c, 1, MSG_PEEK);        // Esto es solo una ojeada en el mensaje.
                if ((n > 0) && (c == '\n'))
                    recv(sock, &c, 1, 0);               // Se recibe el nuevo caracter.
                else
                    c = '\n';
            }
            buf[i] = c;     // Se almacena el caracter en el buffer.
            i++;
        } else {
            c = '\n';       // Se le asigna el valor de la nueva línea para terminar las iteraciones.
        }
    }
    buf[i] = '\0';          // Se añade el valor nulo a la cadena.

    return i;
}

// Método que da un "header" de respuesta al cliente.
void headers(int client, int length, const char *fileType) {
	char response_header[HEADERSPACE];              // Donde se almacenará el encabezado de la respuesta.

	sprintf(response_header, "HTTP/1.1 200 OK\r\n%sAccept-Ranges: bytes\r\nContent-Length: %d\r\nContent-Type: %s\r\n\r\n",
			SERVER_STRING, length, fileType);       // Se especifica la línea del servidor, el largo del archivo
			                                        // y el tipo de archivo.
	fprintf(stdout, "Encabezado de la respuesta:\n%s\n", response_header);

	send(client, response_header, strlen(response_header), 0);      // Se le envía el mensaje al cliente.
}

// Función para enviarle un archivo al cliente.
void send_file(int client, const char *path, int length) {
    int resource;
    int numChars = 1;
    char buff[MAXLINE];
	char *fileType;
	int pathLength;
	int extensionLength;

    buff[0] = 'A'; buff[1] = '\0';
    while ((numChars > 0) && strcmp("\n", buff))        // Se leen y se descartan los encabezados de la solicitud.
        numChars = get_line(client, buff, MAXLINE);

    resource = open(path, O_RDONLY);
    if (resource == -1) {
        send(client, NOT_FOUND, strlen(NOT_FOUND), 0);  // Si no se encuentra se le envía un error 404 al cliente.
    }
    else {                                              // Se busca el tipo mime del archivo.
	    pathLength = strlen(path);
	    fileType = (char *)"application/octet-stream";  // Tipo mime por defecto.
	    for(int i=0; extensions[i].ext != 0; ++i) {
		    extensionLength = strlen(extensions[i].ext);
		    if(!strncmp(&path[pathLength-extensionLength], extensions[i].ext, extensionLength)) {
			    fileType = extensions[i].mime;
			    break;
	    	}
	    }

	    headers(client, length, fileType);              // Se le envía el encabezado al cliente.
		sendfile(client, resource, 0, length);          // Se le envía el archivo al cliente.
    }
    close(resource);    // Se cierra el archivo.
}

// Utilizada para ejecutar un binario y enviarle la respuesta al cliente.
void execute_file(int client, const char *path,
         const char *method, const char *query_string) {
    char buff[MAXLINE];
    int cgi_output[2];          // "Pipe" de entrada.
    int cgi_input[2];           // "Pipe" de salida.
    pid_t pid;
    int status;
    int i;
    char c;
    int numChars = 1;
    int content_length = -1;

    buff[0] = 'A'; buff[1] = '\0';
    if(!strcasecmp(method, "GET")) {                            // Si la operación es un "Get".
        while ((numChars > 0) && strcmp("\n", buff))            // Se leen y se descartan los encabezados.
            numChars = get_line(client, buff, sizeof(buff));
    } else if(!strcasecmp(method, "POST")) {                    // Si la operación es un "Post".
        numChars = get_line(client, buff, sizeof(buff));
        while ((numChars > 0) && strcmp("\n", buff)) {
            buff[15] = '\0';
            if (strcasecmp(buff, "Content-Length:") == 0)       // Se obtiene el largo del cuerpo de la solicitud.
                content_length = atoi(&(buff[16]));
            numChars = get_line(client, buff, sizeof(buff));    // Se descartan datos innecesarios.
        }
        if (content_length == -1) {                             // Si el largo del contenido es -1 se le envía
            send(client, BAD_REQUEST, sizeof(BAD_REQUEST), 0);  // un mensaje de error al cliente.
            return;
        }
    }

    if (pipe(cgi_output) < 0) {                                     // Se abre el Pipe para su uso.
        send(client, CANNOT_EXECUTE, strlen(CANNOT_EXECUTE), 0);    // Si no se logra abrir se le envía un
        return;                                                     // mensaje de error al cliente.
    }
    if (pipe(cgi_input) < 0) {
        send(client, CANNOT_EXECUTE, strlen(CANNOT_EXECUTE), 0);
        return;
    }

    if ((pid = fork()) < 0) {                                       // Se crea un proceso hijo.
        send(client, CANNOT_EXECUTE, strlen(CANNOT_EXECUTE), 0);    // Si no se puede crear se le envía un
        return;                                                     // mensaje de error al cliente.
    }
    if (pid == 0) {                 // Trabajo del hijo.
        char meth_env[255];
        char query_env[255];
        char length_env[255];

        dup2(cgi_output[1], 1);     // Se enlaza la tubería de salida a la salida estándar del proceso.
        dup2(cgi_input[0], 0);      // Se enlaza la tubería de salida a la entrada estándar del proceso.
        close(cgi_output[0]);       // Se cierra la tubería "output" para lectura.
        close(cgi_input[1]);        // Se cierra la tubería "input" para escritura.
        sprintf(meth_env, "REQUEST_METHOD=%s", method);
        putenv(meth_env);           // Se almacena el tipo de método como variable de ambiente.
        if (strcasecmp(method, "GET") == 0) {                       // Si el método es un "Get".
            sprintf(query_env, "QUERY_STRING=%s", query_string);    // Se almacenan los parámetros de la
            putenv(query_env);                                      // solicitud en las variables de ambiente
                                                                    // del programa a ejecutar.
        } else {                                                        // Si el método es un "Post".
            sprintf(length_env, "CONTENT_LENGTH=%d", content_length);   // Se almacena el largo de los parámetro
            putenv(length_env);                                         // concatenados como variable de ambiente.
        }
        execl(path, path, NULL);    // Se ejecuta el programa en la dirección anteriormente especificada.
        exit(0);
    } else {                        // Trabajo del padre.
        close(cgi_output[1]);       // Se cierra la tubería "output" para escritura.
        close(cgi_input[0]);        // Se cierra la tubería "input" para lectura.
        if (strcasecmp(method, "POST") == 0) {          // Si el método es un "Post".
            for (i = 0; i < content_length; i++) {      // Se coloca en la tubería "input" los parámetros
                recv(client, &c, 1, 0);                 // del programa a ejecutar.
                write(cgi_input[1], &c, 1);
            }
        }

        sprintf(buff, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n");    // Se escribe el encabezado de la
                                                                                // respuesta.
        fprintf(stdout, "Encabezado de la respuesta:\n%s\n", buff);

        send(client, buff, strlen(buff), 0);        // Se envía el encabezado al cliente.

        // Esto no comenzará hasta que se haya terminado de escribir en la tubería "output".
        while (read(cgi_output[0], &c, 1) > 0)      // Se envía el resultado del programa al cliente
            send(client, &c, 1, 0);                 // esto deberá ser un html.

        close(cgi_output[0]);           // Se cierra la tubería "output" para lectura.
        close(cgi_input[1]);            // Se cierra la tubería "input" para escritura.
        waitpid(pid, &status, 0);       // Se espera al proceso hijo.
    }
}

// Función que maneja la conexión con el cliente.
void *handle_connection(int *pclient) {
	int client = *pclient;
	free(pclient);              // Se libera el espacio ocupado por el puntero porque ya no se necesitará.
	char buff[MAXLINE];
	int numChars;
	char method[METHODLINE];    // El método utilizado por el cliente.
	char URL[URLLINE];          // La URL especificada por el cliente.
	char path[PATHLINE];        // La dirección de un archivo.
	size_t i=0, j=0;            // Enteros sin signo para iterar.
	char *queryString;          // Almacenará los parámetros para ejecutar un binario.
	int execute = 0;            // Indica si el archivo es ejecutable.
	struct stat st; //Información del archivo

	fprintf(stdout, "\nAtendiendo al cliente: %d\n", client);

	numChars = get_line(client, buff, MAXLINE);     // Se obtiene el método de la solicitud.

	if(!strcmp(buff, "\0")) {   // Si la solicitud es vacía se cierra la conexión.
		fprintf(stdout, "\nCerrando la conexión con: %d\n", client);
		close(client);
		return NULL;
	}

	fprintf(stdout, "Solicitud del cliente %d:\n %s\n", client, buff);  // Se imprime en consola la solicitud.

	while (!isspace(buff[i]) && (i < sizeof(method) - 1))           // Se itera hasta encontrar algo que no sea
		method[i++] = buff[j++];                                    // espacios.
	method[i] = '\0';

	// Se comprueba si la solicitud corresponde a métodos implementados.
	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
        	send(client, UNIMPLEMENTED, strlen(UNIMPLEMENTED), 0);  // Si no han sido implementados se envía
        	return NULL;                                            // un mensaje de error al cliente.
    	}

	if (strcasecmp(method, "POST") == 0)        // Si es un "Post" es porque se ejecutará un binario.
        	execute = 1;

	//Recuperamos la URL especificada por el usuario.
	i = 0;
    while (isspace(buff[j]) && (j < MAXLINE))   // Se identifica algo que no sea espacio.
       	j++;

   	while (!isspace(buff[j]) && (i < URLLINE - 1) && (j < MAXLINE))     // Se itera hasta que se encuentre un espacio.
       	URL[i++] = buff[j++];
   	URL[i] = '\0';

	//Comprobamos si tiene parámetros en la URL
	if (strcmp(method, "GET") == 0) {       // Esto solo se encontrará en un "Get".
		queryString = URL;
        	while ((*queryString != '?') && (*queryString != '\0'))     // Se busca un "?".
			    queryString++;
        	if (*queryString == '?') {
			    execute = 1;
			    *queryString = '\0';        // Se cambia el "?" de la URL por un caracter nulo.
			    queryString++;              // Lo que le sigue serán los parámetros del binario.
        	}
    	}

	sprintf(path, "%s%s", root_path, URL);  // Se le añade a la URL la ruta a donde buscar.

	// Si nos piden un directorio le damos el index.html del directorio.
	if (path[strlen(path) - 1] == '/')
        	strcat(path, "index.html");
	
	if (stat(path, &st) == -1) {    // Si no encontramos el archivo.
        while ((numChars > 0) && strcmp("\n", buff))        // Se leen y se descartan los datos del encabezado.
            numChars = get_line(client, buff, sizeof(buff));
		fprintf(stderr, "Archivo no encontrado.\n");
        send(client, NOT_FOUND, strlen(NOT_FOUND), 0);      // Se envía al cliente un error.
	} 
	else {                          // Si lo encontramos
        if ((st.st_mode & S_IXUSR) ||       // Se comprueba si el archivo es ejecutable para cualquier usuario.
        	    (st.st_mode & S_IXGRP) ||
          	    (st.st_mode & S_IXOTH))
           	execute = 1;            // Se marca el archivo como ejecutable.
        if (!execute)               // Si no es ejecutable se envía el archivo normalmente.
        		send_file(client, path, st.st_size);
        else                        // Si es ejecutable se envía a la función para procesarlo.
        	execute_file(client, path, method, queryString);
	}
	fprintf(stdout, "Cerrando la conexión con: %d\n", client);
	close(client);                  // Se cierra el socket del cliente.
	return NULL;
}

// Función "main" del servidor.
int main(int argc, char **argv) {
	int server_socket, client_socket;           // Socket del servidor y del cliente.
	int port = 0;                               // Donde se almacena el puerto a utilizar.
	struct sockaddr_in servAddr, clientAddr;    // Donde se almacenan los datos para manejar las conexiones.
	int addr_size = sizeof(clientAddr);         // Tamaño de la estructura recién declarada.

	int max_threads = 0;            // Maxima cantidad de hilos a manejar.
	pthread_t *thread_pool;         // "Pool" de hilos.

	if (argc !=  7) {               // Si la cantidad de argumentos no es la correcta se le notifica al usuario.
		fprintf(stderr, "Uso: %s -p puerto -w root-path -n hilos\n", argv[0]);
		return (1);
	}

    for(int i=1; i<argc; i+=2) {    // Se itera buscando los argumentos.
		if(!strcmp("-p", argv[i]))
			port = atoi(argv[i+1]);

		else if(!strcmp("-n", argv[i]))
			max_threads = atoi(argv[i+1]);

		else if(!strcmp("-w", argv[i]))
		    root_path = argv[i+1];
	}

	if(!port) {
		fprintf(stderr, "Debe especificar un puerto.\n");
		exit(1);
	}

	if(!max_threads) {
		fprintf(stderr, "Debe crear al menos 1 hilo.\n");
		exit(1);
	}

	if((server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) { // Se crea el socket del servidor.
		fprintf(stderr, "Error al crear el socket.\n");
		exit(1);
	}

    // Se especifican datos necesarios para manejar la conexión.
	bzero(&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(port);

	if((bind(server_socket, (struct sockaddr*)&servAddr, sizeof(servAddr))) < 0) {  // Si no se puede enlazar
		fprintf(stderr, "Error al enlazar.\n");                                     // el socket del servidor
		close(server_socket);                                                       // se finaliza la ejecución
		exit(1);                                                                    // con un error.
	}

	if((listen(server_socket, SERVER_BACKLOG)) < 0) {           // Se define el "backlog" del servidor
		fprintf(stderr, "Error al escuchar.\n");
		close(server_socket);
		exit(1);
	}

	thread_pool = malloc(max_threads*sizeof(pthread_t));        // Se solicita espacio para almacenar los hilos.

	for(int i=0; i<max_threads; ++i) {
	    pthread_create(&thread_pool[i], NULL, thread_function, NULL);   // Se crean los hilos y se mandan a ejecutar
	}                                                                   // su función para esperar conexiones.

	sigaction(SIGPIPE, &(struct sigaction){SIG_IGN}, NULL);     // Se especifica que hacer si se recibe una señal
                                                                // de los pipes.
    int *pclient;
	while(1) {          // Ciclo del programa.
		fprintf(stdout, "Esperando una conexión en el puerto %d\n", port);
		// Se espera una solicitud para el socket del servidor.
		client_socket = accept(server_socket, (struct sockaddr*)&clientAddr, (socklen_t*)&addr_size);

		if(client_socket < 0){      // Si la conexión del servidor resulta ser negativa se retorna un error.
			fprintf(stderr, "Error al conectarse.\n");
			continue;               //Se continua con la ejecución del servidor.
		}
		fprintf(stdout, "\nConexión aceptada de %s:%d\nCliente: %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), client_socket);

        fprintf(stdout, "Cliente %d encuentra en la posición de la cola: %d\n", client_socket, queue_quantity);
        // Si todos los hilos se encuentran ocupados se le envía un mensaje al cliente para que espere.
        if(queue_quantity > max_threads){
            send(client_socket, FULL_QUEUE, strlen(FULL_QUEUE), 0);
            close(client_socket);
            continue;
        }

        pclient = malloc(sizeof(int));
		pthread_mutex_lock(&mutex);             // Zona crítica.
		*pclient = client_socket;               // Se le indica al puntero que apunte al nuevo socket.
		enqueue(pclient);                       // Se inserta el cliente a la cola.
		pthread_cond_signal(&condition_var);    // Se envía una señal para indicar que hay un cliente en cola.
		pthread_mutex_unlock(&mutex);           // Final de la zona crítica.
	}

	pthread_mutex_destroy(&mutex);
	return 0;
}