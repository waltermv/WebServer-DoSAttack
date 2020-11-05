/*
Autores:
Brandon Ledezma Fernández
Walter Morales Vásquez

Cliente HTTP capaz de realizar colsultas a un servidor, mediante los metodos de GET, POST, DELETE y PUT.

Para compilar el programa es necesario un ambiente GNU/Linux:
    gcc httpclient.c  -o httpclient

Para ejecutar el programa:
    ./PreThreadedServer -p puerto -w dirección-carpeta-raíz -n cantidad-hilos
*/

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>

#define CRLF "\r\n"
#define CR '\r'
#define LF '\n'
#define BUFFER_LEN 2048

char buffer[BUFFER_LEN];
char header[BUFFER_LEN];

/***
 * Funcion para obtener la direccion o ruta del recurso que se solicita
 * @param url Cadena completa, tiene la direccion, puerto y la direccion
 * @param path Donde se va a almacenar el resultado
 * @return  la direccion DNS o la IP
 */
char *getPath(char *url, char **path) {
    char *slash = strchr(url, '/');
    if(slash == NULL) { // Sino se encuentra el caracter se estable index.html por defecto
        char *host = (char*) malloc(strlen(url) + 1);
        char *resPath = (char*) malloc(12);
        strncpy(host, resPath, strlen(url));
        strncpy(resPath, "/index.html", 11);
        *path = resPath;
        return host; // Se establece como host la URL pasada por parametro
    }
    int lenHost = slash - url;
    int pathLen = strlen(url) - (url - url);
    char *addr = (char*) malloc(lenHost + 1);
    char *file = (char*) malloc(pathLen + 1);
    strncpy(addr, url, lenHost);
    addr[lenHost] = '\0';
    strcpy(file, slash);
    *path = file; // Se establece como ruta el valor presente desde la posicion de / en adelante
    return addr; // Se establece como host la URL pasada por parametro antes del slash
}


/***
 * Funcion utilizada para obtener el puerto mediante el que se realiza la conexion, se obtiene apartir del host o bien se establece 80 por defecto
 * @param host  Direccion con la que se conectara al servidor
 * @return puerto mediante el cual se conectara al host
 */
int getPort(char *host) {
    char *element = strrchr(host, ':'); // Se busca el caracter de dos puntos para recortar el string de host
    if (element == NULL) return 80; // Si no se encuentra el caracter, se deja por defecto el puerto 80
    int port = atoi(element + 1);
    *element = '\0';
    return port; // Sino se devuelve el puerto
}


/***
 * Funcion utilizada para obtener informacion para la conexion con el servidor
 * @param hostname Nombre o IP del servidor
 * @param port Puerto mediante el que se realizara la conexion
 * @return estructura de tipo addrinfo
 */
struct addrinfo *getAddressInformation(char *hostname, int port) {
    struct addrinfo hints, *getInformation;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portC[6];
    sprintf(portC,"%d", port);
    int result = getaddrinfo(hostname, portC, &hints, &getInformation);
    if (result) exit(1); // En caso que no se pueda realizar la conexion, se termina el proceso
    return getInformation;
}


/***
 * Funcion que realiza la conexion con el servidor para realizar la consulta HTTP
 * @param hostname IP o DNS del servidor que se desea conectar
 * @param port Puerto mediante el cual se realizara la conexion con el servidor
 * @return valor obtenido del socket de la conexcion
 */
int getConnection(char *hostname, int port) {
    struct addrinfo *hostInformation = getAddressInformation(hostname, port); // Se optiene informacion sobre el host
    if (hostInformation == NULL) return -1; // Si no se obtuvo informacion se retorna -1
    int clientfd;
    for (;hostInformation != NULL; hostInformation = hostInformation->ai_next) {
        if ((clientfd = socket(hostInformation->ai_family, hostInformation->ai_socktype, hostInformation->ai_protocol)) < 0) {
            perror("[Error getting connection]"); // Se muestra un error en caso de no poder conectarse
            continue;
        }
        if (connect(clientfd, hostInformation->ai_addr, hostInformation->ai_addrlen) < 0) {
            close(clientfd);
            perror("[Error establishing connection]"); // Se muestra un error en caso de no poder conectarse
            continue;
        }
        freeaddrinfo(hostInformation);
        return clientfd;  // Se retorna el valor obtenido de la conexcion
    }
    freeaddrinfo(hostInformation);
    return -1; // Sino logra conectarse con el servidor
}


/***
 * Funcion para leer lineas, lee caracteres hasta encontrar un salto de linea
 * @param client valor obtenido del socket
 * @param out puntero donde se almacenara los valores leidos
 * @return largo de la linea
 */
int readLine(int client, char *out) {
    int bufSize = 0;
    int inBuf = 0;
    int ret;
    char ch;
    char *temp = NULL;
    char * newBuffer;
    do {
        ret = read(client, &ch, 1);
        if (ret < 1) {
            free(temp);
            return -1;
        }
        if (ch == LF) break;
        if ((bufSize == 0) || (inBuf == bufSize)) {
            bufSize += 128;
            newBuffer = realloc(temp, bufSize);
            if (!newBuffer) {
                free(temp);
                return -1;
            }
            temp = newBuffer;
        }
        temp[inBuf] = ch;
        ++inBuf;
    } while (1);
    if ((inBuf > 0) && (temp[inBuf - 1] == CR)) --inBuf;
    if ((bufSize == 0) || (inBuf == bufSize)){
        ++bufSize;
        newBuffer = realloc(temp, bufSize);
        if (!newBuffer){
            free(temp);
            return -1;
        }
        temp = newBuffer;
    }
    temp[inBuf] = '\0';
    printf("%s \n" ,temp);
    *out =  *temp;
    return inBuf;
}


/***
 * Funcion para leer el encabezado obtenido del servidor
 * @param client entero del valor de la conexion con el socket
 */
void readResponse(int client) {
    int ret, flag = 1;
    do {
        ret = readLine(client, header); // Lee linear del response
        if (ret < 0) flag = 0; // Se detiene en caso de encontrar un valor incorrecto
        if (ret == 0) flag = 0; // O si llego a la division del encabezado y el cuerpo
    } while (flag);
}


/***
 * Funcion para leer los datos del cuerpo del response
 * @param client entero con el valor obtenido del socket
 * @param path ruta con el nombre del recurso solicitado
 */
void getData(int client, char *path) {
    char *name = basename(path); // Funcion de Unix para obtener la base de la ruta
    FILE *file;
    file = fopen(name, "ab"); // Se abre un archivo
    if (file == NULL) perror("[Error creating file]"); // En caso de no poder crearlo se termina la ejecucion
    memset(buffer, 0x00, BUFFER_LEN);
    while (recv(client, buffer, BUFFER_LEN, 0) > 0) { // Se itera mientras se lean datos del socket
        fwrite(buffer, BUFFER_LEN, 1, file);
        memset(buffer, 0, BUFFER_LEN);
    }
    if (fclose(file)!=0) perror("[The file could not be closed]"); // Se cierra el archivo, informa en caso de ocurrir un inconveniente
}


/***
 * Funcion para realizar un consulta GET a un servidor
 * @param client entero del valor obtenido del socket de la conexion
 * @param url recurso al que se desea consultar
 */
void getMethod(int client, char *url) {
    char requestBuffer[1080]; // buffer para almacenar el request
    sprintf(requestBuffer,"GET %s HTTP/1.1 %s%s",url, CRLF, CRLF); // se da formato al request
    send(client, requestBuffer, strlen(requestBuffer), 0); // se enviar la consulta al cliente
    readResponse(client); // se lee los encabezados del response
    getData(client, url); // se lee los recursos obtenidos, para almacenarlos
}


/***
 * Funcion para realizar un consulta POST a un servidor
 * @param client entero del valor obtenido del socket de la conexion
 * @param url recurso al que se desea consultar
 * @param host direccion DNS o IP del servidor a consultar
 * @param parameters parametros del metodo post que se incluyen en la consulta
 */
void postMethod(int client, char *url, char *host, char *parameters) {
    char requestBuffer[1080]; // buffer para almacenar el request
    sprintf(requestBuffer,"POST %s HTTP/1.1 %sHost: %s%sContent-Length: %lu%s%s%s",url, CRLF, host, CRLF, sizeof(parameters), CRLF, CRLF, parameters);
    send(client, requestBuffer, strlen(requestBuffer), 0); // se enviar la consulta al cliente
    readResponse(client); // se lee los encabezados del response
    getData(client, url); // se lee los recursos obtenidos, para almacenarlos
}


/***
 * Funcion para realizar un consulta DELETE a un servidor
 * @param client valor obtenido del socket
 * @param url recurso que se desea eliminar
 * @param host servidor donde se aloja el recurso
 */
void deleteMethod(int client, char *url, char *host) {
    char requestBuffer[1080]; // buffer para almacenar el request
    sprintf(requestBuffer,"DELETE %s HTTP/1.1 %sHost: %s%s%s",url, CRLF, host, CRLF, CRLF);
    printf("%s\n", requestBuffer);
    send(client, requestBuffer, strlen(requestBuffer), 0); // se enviar la consulta al cliente
    readResponse(client); // se lee los encabezados del response
}


/***
 * Funcion para enviar archivos al servidor
 * @param file Puntero del archivo que se va a enviar al servidor
 * @param client Valor obtenido de la conexion del socket
 */
void sendFile(FILE *file, int client){
    char bufferRequest[BUFFER_LEN];
    memset(bufferRequest, 0x00, BUFFER_LEN);
    while(fgets(bufferRequest, BUFFER_LEN, file) != NULL) {
        printf("%s", bufferRequest);
        if (send(client, bufferRequest, sizeof(bufferRequest), 0) != -1) { // Se lee los valores del archivo y se envian hasta obtener un valor -1
            perror("[Error in sending file]"); // En tal caso se informa el error
            exit(1); // Se termina la ejecucion
        }
        bzero(bufferRequest, BUFFER_LEN); // Se limpia el buffer
    }
}


/***
 * Funcion para obtener el largo de un archivo
 * @param filename Nombre del archivo que desea conocer el largo
 * @return largo del archivo
 */
long getFileSize(const char *filename){
    struct stat statbuf;
    if (stat(filename, &statbuf) == -1){
        perror("[failed to stat file]\n");
        exit(1);
    }
    return statbuf.st_size;
}


/***
 * Funcion para realizar un consulta PUT a un servidor
 * @param client valor obtenido del socket
 * @param url lugar donde se envia el recurso
 * @param host servidor donde se aloja el recurso
 * @param fileName
 */
void putMethod(int client, char *url, char *host, char *fileName){
    long size = getFileSize(fileName);
    FILE *file = fopen(fileName, "r");
    if(file == NULL) {
        perror("[Error reading file]");
        exit(1);
    }
    char requestBuffer[1080]; // buffer para almacenar el request
    sprintf(requestBuffer,"PUT %s HTTP/1.1 %sHost: %s%sContent-Length: %ld%s%s",url, CRLF, host, CRLF, size + 1, CRLF, CRLF);
    printf(requestBuffer,"PUT %s HTTP/1.1 %sHost: %s%sContent-Length: %ld%s%s",url, CRLF, host, CRLF, size + 1, CRLF, CRLF);
    send(client, requestBuffer, strlen(requestBuffer), 0); // se enviar la consulta al cliente
    sendFile(file, client);
    if (fclose(file)!=0) perror("[The file could not be closed]");
    readResponse(client); // se lee los encabezados del response
}


/***
 * Funcion principal del programa, recibe como parametros los argumentos de la terminal
 * @param argc como la cantidad de argumentos
 * @param args como los string recibidos
 **/
int main(int argc, char *args[]) {
    char *host, *path;
    int port;
    host = getPath(args[2], &path); // Se divide la URL en la direccion del host y la ruta donde encontramos el recurso
    port = getPort(host); // Obtenemos el puerto, si es que viene en la dirección
    int socket = getConnection(host, port); // Se abre un socket para la conexión con el servidor.
    if(argc == 3) getMethod(socket, path); // Se comprueba si la consulta es un GET
    else if (strcmp(args[3], "POST") == 0) postMethod(socket, path, host, args[4]); // Se comprueba si la consulta es un POST
    else if (strcmp(args[3], "DELETE") == 0) deleteMethod(socket, path, host);  // Se comprueba si la consulta es un DELETE
    else if (strcmp(args[3], "PUT") == 0) putMethod(socket, path, host, args[4]); // Se comprueba si la consulta es un PUT
    else printf("[Method not supported]\n");
    close(socket); // Se cierra el socket de la conexion
    return 0; // Indica que se termino con exito
}
