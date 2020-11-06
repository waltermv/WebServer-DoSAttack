/*
Autores:
Brandon Ledezma Fernández
Walter Morales Vásquez

Servidor capaz de atender a usuarios utilizando la técnica de Pre-Fork. Actualmente acepta solicitudes
de GET, POST, DELETE y PUT. Además, es capaz de ejecutar archivos binarios que dan como resultado texto html.

Para compilar el programa:
gcc -o PreForkedServer PreForkedServer.c

Para ejecutar el programa:
./PreForkedServer -p puerto -w dirección-carpeta-raíz -n cantidad-hilos
*/

#include <arpa/inet.h>
#include <ctype.h>
#include <fcntl.h>
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

// Respuesta HTTP de código 500
#define CANNOT_EXECUTE "HTTP/1.1 500 Internal Server Error\r\n"\
                        SERVER_STRING\
                       "Content-type: text/html\r\n\r\n"\
                       "<P>Error prohibited execution.\r\n"

// Respuesta HTTP de código 409
#define CANNOT_ALTER "HTTP/1.1 409 Conflict\r\n"\
                     SERVER_STRING\
                     "Content-type: text/html\r\n\r\n"\
                     "<P>The request could not be completed"\
                     "due to conflict with the current state"\
                     "of the target resource.\r\n"

// Respuesta HTTP de código 503
#define FULL_QUEUE "HTTP/1.1 503 Service Unavailable\r\n"\
                    SERVER_STRING\
                   "Content-Type: text/html\r\n\r\n"\
	               "<HTML><TITLE>Service Unavailable</TITLE>\r\n"\
                   "<HEAD><meta http-equiv=\"refresh\" content=\"15\"></HEAD>"\
                   "<BODY><P>Server currently unavailable due to overload."\
                   "</BODY></HTML>\r\n"

// Con lo que se comprobará si el usuario tiene los permisos para actualizar un archivo.
#define FILE_RIGHTS S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH

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

int queue_quantity = 1;             // Cantidad de clientes atendiendo.

int get_line(int sock, char *buf, int size);                    // Función para obtener una línea de un socket.
void headers(int client, int length, const char *fileType);     // Método que da un "header" de respuesta al cliente.
void send_file(int client, const char *path, int length);       // Función para enviarle un archivo al cliente.
void execute_file(int client, const char *path,                 // Utilizada para ejecutar un binario y enviarle
         const char *method, const char *query_string);         // la respuesta al cliente.
void handle_connection(int client);                          // Función que maneja la conexión con el cliente.

// Función para obtener una línea de un socket. Para hasta que encuentra una nueva línea, retorno de carro o
// avance de línea. Retorna el número de bytes almacenados.
int get_line(int sock, char *buff, int size) {
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
            buff[i] = c;     // Se almacena el caracter en el buffer.
            i++;
        } else {
            c = '\n';       // Se le asigna el valor de la nueva línea para terminar las iteraciones.
        }
    }
    buff[i] = '\0';          // Se añade el valor nulo a la cadena.

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

    int resource = open(path, O_RDONLY);
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
                fprintf(stdout, "%c", c);
                write(cgi_input[1], &c, 1);
            }
        }

        sprintf(buff, "HTTP/1.1 200 OK\r\n%sContent-Type: text/html\r\n\r\n",   // Se escribe el encabezado de la
                SERVER_STRING);                                                 // respuesta

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

// Función para eliminar el archivo especificado en el path.
void delete_file(int client, const char *path) {
    char buff[MAXLINE];
    int length = strlen(path);
    if(length >= 10 && !strcmp(path+length-10, "index.html")) { // Se comprueba si se desea eliminar un index.html.
        fprintf(stderr, "El archivo no se puede borrar\n");
        send(client, CANNOT_ALTER, strlen(CANNOT_ALTER), 0);    // Si el archivo no se puede borrar se le indica
        return;                                                 // al cliente.
    }
    if(length >= 11 && !strcmp(path+length-11, "favicon.ico")) {// Se comprueba si el archivo es el favicon.ico.
        fprintf(stderr, "El archivo no se puede borrar\n");
       	send(client, CANNOT_ALTER, strlen(CANNOT_ALTER), 0);
        return;
    }

    if(remove(path) < 0) {                                      // Se utiliza el comando remove.
        fprintf(stderr, "El archivo no se pudo borrar\n");
        send(client, CANNOT_ALTER, strlen(CANNOT_ALTER), 0);    // Si no se pudo borrar se le indica al usuario.
    }
    else {                                                      // Si se logró borrar.
        sprintf(buff, "HTTP/1.1 204 No Content\r\n%s\r\n", SERVER_STRING);  // Se escribe el encabezado de la
                                                                            // respuesta.
        fprintf(stdout, "Encabezado de la respuesta:\n%s\n", buff);
        send(client, buff, strlen(buff), 0);        // Se envía el encabezado al cliente.
    }
}

// Función para copiar el contenido en la solicitud PUT en el servidor.
void copy_content(int client, char *path) {
    char buff[MAXLINE];
    int numChars = 1;
    int content_length = -1;        // Largo indicado en el header de la solicitud.
    int buff_len;
    int nb_elements_read;           // Cantidad de elementos a leer.
    int nb_elements_write;          // Cantidad de elementos a escribir.
    int fd_out;                     // Referencia al archivo.

    if(path[strlen(path) - 1] == '/') {                                     // Se comprueba si lo que se quiere
        send(client, CANNOT_ALTER, strlen(CANNOT_ALTER), 0);                // modificar es un directorio.
        fprintf(stderr, "El archivo especificado es un directorio\n");
        return;
    }

    if((fd_out = open(path, O_EXCL | O_WRONLY | O_CREAT | O_TRUNC, FILE_RIGHTS)) == -1) {   // Se comprueba
        fprintf(stdout, "El archivo ya existe\n");                                          // si ya existe.
        if ((fd_out = creat(path, FILE_RIGHTS)) == -1) {                // Si no se tienen los permisos
            send(client, CANNOT_ALTER, strlen(CANNOT_ALTER), 0);            // necesarios para realizar la
            fprintf(stderr, "El archivo no se puede sobreescribir\n");      // actualización se le indica al
            return;                                                         // cliente.
        }
    }

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

    buff_len = MAXLINE;         // Cantidad por la que se leerá en cada iteración.

    while (content_length) {
        if (content_length < MAXLINE)
	        buff_len = content_length;                  // Si el restante es menor a la cantidad máxima del buffer
        nb_elements_read = read(client, buff, buff_len);// se utiliza la cantidad restante a leer para el espacio.
        if (nb_elements_read == -1) {
            send(client, BAD_REQUEST, strlen(BAD_REQUEST), 0);    // Si sucede algún error al leer los datos
            fprintf(stderr, "Error al leer el cuerpo de la solicitud\n");           // se le indica al usuario.
            return;
        }

        nb_elements_write = write (fd_out, buff, nb_elements_read);
        if (nb_elements_write == -1 || nb_elements_write != nb_elements_read) { // Si sucede un error al crear
            send(client, CANNOT_ALTER, strlen(CANNOT_ALTER), 0);          // el archivo se le indica al cliente.
            fprintf(stderr, "El archivo no se puede crear\n");
            return;
        }
        content_length -= nb_elements_read;         // Se le resta la cantidad leída a la restante.
    }

    if (close (fd_out) == -1) {
        send(client, CANNOT_ALTER, strlen(CANNOT_ALTER), 0);            // El archivo no se pudo cerrar.
        fprintf(stderr, "Ocurrió un error con el archivo\n");
        return;
    }

    sprintf(buff, "HTTP/1.1 204 No Content\r\n%s\r\n", SERVER_STRING);  // Se escribe el encabezado de la
                                                                        // respuesta satisfactoria.
    fprintf(stdout, "Encabezado de la respuesta:\n%s\n", buff);
    send(client, buff, strlen(buff), 0);                                // Se envía el encabezado al cliente.
}

// Función que maneja la conexión con el cliente.
void handle_connection(int client) {//handle
	char buff[MAXLINE];
	int numChars;
	char method[METHODLINE];    // El método utilizado por el cliente.
	char URL[URLLINE];          // La URL especificada por el cliente.
	char path[PATHLINE];        // La dirección de un archivo.
	size_t i=0, j=0;            // Enteros sin signo para iterar.
	char *queryString;          // Almacenará los parámetros para ejecutar un binario.
	int execute = 0;            // Indica si el archivo es ejecutable.
	struct stat st;             // Información del archivo.

	fprintf(stdout, "\nAtendiendo al cliente: %d\n", client);

	numChars = get_line(client, buff, MAXLINE);     // Se obtiene el método de la solicitud.

	if(!strcmp(buff, "\0")) {   // Si la solicitud es vacía se cierra la conexión.
		fprintf(stdout, "Cerrando la conexión con: %d\n", client);
	    close(client);                  // Se cierra el socket del cliente.
		return;
	}

	fprintf(stdout, "Solicitud del cliente %d:\n %s\n", client, buff);  // Se imprime en consola la solicitud.

	while (!isspace(buff[i]) && (i < sizeof(method) - 1))           // Se itera hasta encontrar algo que no sea
		method[i++] = buff[j++];                                    // espacios.
	method[i] = '\0';

	if (!strcasecmp(method, "POST"))        // Si es un "Post" es porque se ejecutará un binario.
        execute = 1;

	//Recuperamos la URL especificada por el usuario.
	i = 0;
    while (isspace(buff[j]) && (j < MAXLINE))   // Se identifica algo que no sea espacio.
       	j++;

   	while (!isspace(buff[j]) && (i < URLLINE - 1) && (j < MAXLINE))     // Se itera hasta que se encuentre un espacio.
       	URL[i++] = buff[j++];
   	URL[i] = '\0';

	//Comprobamos si tiene parámetros en la URL
	if (!strcmp(method, "GET")) {       // Esto solo se encontrará en un "Get".
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
	if (strcmp(method, "PUT") && path[strlen(path) - 1] == '/')
        	strcat(path, "index.html");

	if (strcmp(method, "PUT") && stat(path, &st) == -1) {   // Si no encontramos el archivo.
        while ((numChars > 0) && strcmp("\n", buff))        // Se leen y se descartan los datos del encabezado.
            numChars = get_line(client, buff, sizeof(buff));

		fprintf(stderr, "Archivo no encontrado.\n");
        send(client, NOT_FOUND, strlen(NOT_FOUND), 0);      // Se envía al cliente un error.
	}
	else {                                  // Si lo encontramos
        if (strcmp(method, "PUT") &&
                (st.st_mode & S_IXUSR) ||       // Se comprueba si el archivo es ejecutable para cualquier usuario.
        	    (st.st_mode & S_IXGRP) ||
          	    (st.st_mode & S_IXOTH)) {
           	execute = 1;                    // Se marca el archivo como ejecutable.
        }
        if(!strcmp(method, "PUT")) {
            copy_content(client, path);
        }
        else if (!strcmp(method, "DELETE")) {
            delete_file(client, path);
        }
        else if (!execute)                  // Si no es ejecutable se envía el archivo normalmente.
        	send_file(client, path, st.st_size);
        else                                // Si es ejecutable se envía a la función para procesarlo.
        	execute_file(client, path, method, queryString);
	}
	fprintf(stdout, "Cerrando la conexión con: %d\n", client);
	close(client);                          // Se cierra el socket del cliente.
	return;
}

// Función por la que pasarán las conexiones de los protocolos que no sean HTTP una vez muestren su mensaje.
void doNothing(int client) {
	char c;
	int valueAux;
	do {
		valueAux = recv(client, &c, 1, 0);          // Se recibirán datos hasta que en algún momento se
	}while(valueAux != 0);                          // pierda la conexión.
}

// Función para atender una conexión FTP.
void attendIncomingFtpRequest(int client, int pActiveProcess, int pProcessMax) {
	char activeProcessStr[21];
	char maxProcessStr[21];
	fprintf(stdout, "Se ha detectado una conexión FTP.\n");
	sprintf(activeProcessStr, "%d", pActiveProcess);
	sprintf(maxProcessStr, "%d", pProcessMax);
	char ftpResponse[85] = "220 FTP server ready, ";
	strcat(ftpResponse, activeProcessStr);
	strcat(ftpResponse, " active clients of ");
	strcat(ftpResponse, maxProcessStr);
	strcat(ftpResponse, " simultaneous clients allowed.\r\n");
	send(client, ftpResponse, strlen(ftpResponse), 0);
	doNothing(client);
}

// Función para atender una conexión SNMP.
void attendIncomingSNMPRequest(int client) {
	char *versionSNMP = "Version: 1\r\n";
	char *community = "Community: public \r\n";
	char *pdu = "PDU type: GET\r\n";
	char *request = "Request Id: 0\r\n";
	char *error = "Error Status: NO ERROR\r\n";
	char *errorIndex = "Error Index: 0\r\n";
	fprintf(stdout, "Se ha detectado una conexión SNMP.\n");
	send(client, versionSNMP, strlen(versionSNMP), 0);
	send(client, community, strlen(community), 0);
	send(client, pdu, strlen(pdu), 0);
	send(client, request, strlen(request), 0);
	send(client, error, strlen(error), 0);
	send(client, errorIndex, strlen(errorIndex), 0);
	doNothing(client);
}

// Función para atender una conexión Telnet.
void attendIncomingTelnetRequest(int client) {
    char buff[MAXLINE] = {0};
    fprintf(stdout, "Se ha detectado una conexión Telnet.\n");
	char *welcomeTelnet = "Welcome to FOO Telnet Service\r\n";
	char *loginMsg = "Login: ";
	char *passwordMsg = "\r\nPassword: ";
	send(client, welcomeTelnet, strlen(welcomeTelnet), 0);
	send(client, loginMsg, strlen(loginMsg), 0);
	get_line(client, buff, MAXLINE);
	fprintf(stdout, "Cliente: %s.\n", buff);
	send(client, passwordMsg, strlen(passwordMsg), 0);
	get_line(client, buff, MAXLINE);
	send(client, "\r\n", strlen("\r\n"), 0);
	fprintf(stdout, "Contraseña: %s.\n", buff);
	doNothing(client);
}

// Función para atender una conexión SMTP.
void attendIncomingSMTPRequest(int client) {
	char *welcomeSMTP = "220 Server SMTP\r\n";
	fprintf(stdout, "Se ha detectado una conexión SMTP.\n");
	send(client, welcomeSMTP, strlen(welcomeSMTP), 0);
	doNothing(client);
}

// Función para atender una conexión DNS.
void attendIncomingDNSRequest(int client) {
	char *welcomeDNS = "NOT IMPLEMENTED.\r\n";
	fprintf(stdout, "Se ha detectado una conexión DNS.\n");
    send(client, welcomeDNS, strlen(welcomeDNS), 0);
	doNothing(client);
}

// Función para atender una conexión SSH.
void attendIncomingSSHRequest(int client) {
	char *welcomeSSH = "The authenticity of host 'FOO Server' can't be established.\r\n";
	char *rsa = "RSA key fingerprint is 97:4f:66:f5:96:ba:6d:b2:ef:65:35:45:18:0d:cc:29 \r\n";
	char *continueServer = "Are you sure you want to continue connecting (yes/no)?\r\n";*/
	fprintf(stdout, "Se ha detectado una conexión SSH.\n");
	send(client, welcomeSSH, strlen(welcomeSSH), 0);
	send(client, rsa, strlen(rsa), 0);
	send(client, continueServer, strlen(continueServer), 0);
	doNothing(client);
}

// Función "main" del servidor.
int main(int argc, char **argv) {
	int server_socket, client_socket;           // Socket del servidor y del cliente.
	int port = 0;                               // Donde se almacena el puerto a utilizar.
	struct sockaddr_in servAddr, clientAddr;    // Donde se almacenan los datos para manejar las conexiones.
	int addr_size = sizeof(clientAddr);         // Tamaño de la estructura recién declarada.

	int max_processes = 0;          // Maxima cantidad de procesos a manejar.
	pid_t *process_pool;            // "Pool" de procesos.

	if (argc !=  7) {               // Si la cantidad de argumentos no es la correcta se le notifica al usuario.
		fprintf(stderr, "Uso: %s -p puerto -w root-path -n procesos\n", argv[0]);
		return (1);
	}

    for(int i=1; i<argc; i+=2) {    // Se itera buscando los argumentos.
		if(!strcmp("-p", argv[i]))
			port = atoi(argv[i+1]);

		else if(!strcmp("-n", argv[i]))
			max_processes = atoi(argv[i+1]);

		else if(!strcmp("-w", argv[i]))
		    root_path = argv[i+1];
	}

	if(!port) {
		fprintf(stderr, "Debe especificar un puerto.\n");
		exit(1);
	}

	if(!max_processes) {
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

    fprintf(stdout, "Esperando una conexión en el puerto %d\n", port);

    process_pool = malloc(max_processes*sizeof(pthread_t));     // Se solicita espacio para almacenar los
                                                                // identificador de los procesos.
	for(int i=0; i<max_processes-1; ++i) {
	    process_pool[i] = fork();
	    if(process_pool[i] < 0 ){           // Si hay un error al crear un hilo se informa y se detiene la ejcución
            fprintf(stderr, "Error al realizar un fork.\n");
            exit(1);
        }
        if(process_pool[i] == 0){           // Si es un proceso hijo este termina de iterar.
            break;
        }
	}

	while(1) {          // Todos los procesos atienden a las conexiones.
	                    // El que se encarga de dirigir cual proceso atiende la siguiente conexión es el kernel.
		// Se espera una solicitud para el socket del servidor.
		client_socket = accept(server_socket, (struct sockaddr*)&clientAddr, (socklen_t*)&addr_size);

		if(client_socket < 0){      // Si la conexión del servidor resulta ser negativa se retorna un error.
			fprintf(stderr, "Error al conectarse.\n");
			continue;               //Se continua con la ejecución del servidor.
		}
		fprintf(stdout, "\nConexión aceptada de %s:%d\nCliente: %d\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), client_socket);

        fprintf(stdout, "Cliente %d encuentra en la posición de la cola: %d\n", client_socket, queue_quantity);
        // Si todos los hilos se encuentran ocupados se le envía un mensaje al cliente para que espere.
        if(queue_quantity > max_processes){
            send(client_socket, FULL_QUEUE, strlen(FULL_QUEUE), 0);
            close(client_socket);
            continue;
        }

        ++queue_quantity;                       // Se aumenta en 1 la cantidad de clientes que se están procesando.

        // Se atiende la conexión con el protocolo FTP.
        if(port == 21) attendIncomingFtpRequest(client_socket, queue_quantity, max_processes);

        else if(port == 22) attendIncomingSSHRequest(client_socket);    // Se atiende la conexión con el protocolo SSH.

        else if(port == 23) attendIncomingTelnetRequest(client_socket); // Se atiende la conexión con el protocolo Telnet.

        else if(port == 25) attendIncomingSMTPRequest(client_socket);   // Se atiende la conexión con el protocolo SMTP.

        else if(port == 53) attendIncomingDNSRequest(client_socket);    // Se atiende la conexión con el protocolo DNS.

        else if(port == 161) attendIncomingSNMPRequest(client_socket);  // Se atiende la conexión con el protocolo SNMP.

        else handle_connection(client_socket);      // Se maneja la conexión como HTTP.

        --queue_quantity;                           // Se disminuye en 1 la cantidad de clientes procesando.
        printf("\nCantidad en la cola: %d\n", queue_quantity);
	}

	return 0;
}