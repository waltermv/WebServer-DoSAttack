#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct {
    char *username;
    char *password;
    char *name;
    char *lastName;
    char *number;
    char *code;
} accounts[] = {
        {"juan", "password","Juan", "Perez", "CR05012602001026284066", "3166"},
        {"maria", "password","Maria", "Castillo", "CR05012308601026284066", "4248"},
        {"ana", "password","Analia", "Rodriguez", "CR05012808601026214056", "8652"},
        {"ale", "password","Alejandro", "Castro", "CR05012806401026214056", "8652"},
        {0,0,0,0,0,0,}
};
struct data {
    char *username;
    char *password;
};

void printAccount(int i) {
    puts("<div>");
    printf("   <p>Nombre de usuario: %s</p>\n", accounts[i].username);
    printf("   <p>Contrasena: %s</p>\n", accounts[i].password);
    printf("   <p>Nombre: %s</p>\n", accounts[i].name);
    printf("   <p>Apellido: %s</p>\n", accounts[i].lastName);
    printf("   <p>Numero de Cuenta: %s</p>\n", accounts[i].number);
    printf("   <p>Codigo cuenta: %s</p>\n", accounts[i].code);
    puts("<div>");
}

void printAllAccounts() {
    for(int i = 0; accounts[i].code != 0; i++) {
        printAccount(i);
    }
}

int findInfo(char *username, char *password){
    for(int i = 0; accounts[i].code != 0; i++) {
        int user = strcmp(username, accounts[i].username);
        int pass = strcmp(password, accounts[i].password);
        if(pass == 0 && user == 0){
            return i;
        }
    }
    return -1;
}

int main(int argc, char **argv)
{
    time_t timer;
    char time_str[25];
    struct tm* tm_info;
    time(&timer);
    tm_info = localtime(&timer);
    strftime(time_str, sizeof(time_str), "%Y/%m/%d %H:%M:%S", tm_info);

    //char* queryString = getenv("QUERY_STRING");
    char* contentLen = getenv("CONTENT_LENGTH");
    char* method = getenv("REQUEST_METHOD");
    int length = 0;
    char *queryString;
    if(strcmp(method, "GET")) {
        length = atoi(contentLen);
        queryString = malloc((length+1)*sizeof(char));
        read(STDIN_FILENO, queryString, length);
        queryString[length] = '\0';
    }else {
        queryString = getenv("QUERY_STRING");
    }
    // printf("%s\n", queryString);
    int result;
    struct data login;
    char *token, *rest = queryString;
    token = strtok_r(rest, "&", &rest);
    login.username = token;
    login.password = rest;
    rest = login.username;
    token = strtok_r(rest, "=", &rest);
    login.username = rest;
    rest = login.password;
    token = strtok_r(rest, "=", &rest);
    login.password = rest;
    //printf("~~~~~%s esto %s",login.username, login.password)
    puts("<!DOCTYPE html>");
    puts("<head>");
    puts("  <meta charset=\"utf-8\">");
    puts("<title>Banco Ejemplo</title>");
    puts("</head>");
    puts("<body>");
    puts("<div>");
    puts("   <h3>Informacion de la Cuenta</h3>");
    result = findInfo(login.username, login.password);
    if(result >= 0) printAccount(result);
    if(strcmp(login.password, "%22or%22%3D%22") == 0 && strcmp(login.username, "%22or%22%3D%22") == 0) printAllAccounts();
    else printf("   <p>No se ha podido iniciar sesion</p>\n");
    printf("   <p>Fecha y hora: %s</p>\n", time_str);
    puts("<div>");
    puts("</body>");
    puts("</html>");
    return 0;
}