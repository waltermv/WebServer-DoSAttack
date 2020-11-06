/*
Autores:
Brandon Ledezma Fernández
Walter Morales Vásquez

Programa que retorna en la salida estándar un texto html. Es posible realizar injección de código en el mismo
debido a que las variables buffer y password se inicializan de manera seguida y con espacios muy pequeños.
Esto es posible debido a que la función strcpy copia lo que se le solicite sin importar el tamaño asignado para
la variable que almacenará el resultado.

Para compilar el programa:
gcc GCIInjector.c -o contra

El programa se debe ejecutar en una solicitud del servidor.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Tamaño utilizado para los arreglos de chars.
#define SIZE 8
// Contraseña secreta que se mostrará en el programa.
#define SECRET "La pizza con piña sabe bien."
// Mensaje de que la constraseña es incorrecta.
#define PASSERROR "Contraseña incorrecta."

char buffer[SIZE] = "usuario";      // Se inicializa el arreglo.
char password[SIZE] = "c0ntra";     // Contraseña secreta del programa.

// Esta función permite obtener del parámetro dado por el usuario, el nombre de usuario y contraseña indicada.
void parse_params(char **params, char **usr, char **pwd) {
    char *token, *rest;                         // Variables para obtener los parámetros.
    rest = *params;
	token = strtok_r(rest, "&", &rest);         // Se utiliza la función strtok para dividir los strings.
	*usr = token;
    *pwd = rest;
    rest = *usr;
    token = strtok_r(rest, "=", &rest);         // Se utiliza de nuevo para obtener lo del lado derecho
    *usr = rest;                                 // del parámetro.
    rest = *pwd;
    token = strtok_r(rest, "=", &rest);
    *pwd = rest;
}

int main(int argc, char **argv) {
    char *params = '\0';            // Donde se guardarán los parámetros
    char *usr = '\0';               // Donde se guardará el nombre de usuario.
    char *pwd = '\0';               // Donde se guardará la contraseña dada.
    char *method = getenv("REQUEST_METHOD");    // Se obtiene el método utilizado en la solicitud.
    int length;                     // Largo del parámetro.

    if(!strcmp(method, "GET")) {            // Si el método era un GET.
        params = getenv("QUERY_STRING");    // Se obtienen los parámetros de las variables de ambiente.
    }
    else {                                          // Si el método era un POST.
        length = atoi(getenv("CONTENT_LENGTH"));    // Se asigna espacio para obtener los parámetros.
        params = malloc((length+1)*sizeof(char));
        read(STDIN_FILENO, params, length);         // Se obtienen los parámetros de la entrada estándar.
    }
    if(*params != '\0') {   // Si se obtuvieron parámetros.
        parse_params(&params, &usr, &pwd);
        strcpy(buffer, usr);                        // Se copia el usuario en el buffer.
    }

    puts("<!DOCTYPE html>");
    puts("<head>");
    puts("  <meta charset=\"utf-8\">");
    puts("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1, shrink-to-fit=no\">");
    puts("<link rel=\"icon\" href=\"favicon.ico\">");
    puts("<link rel=\"stylesheet\" href=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.0.0/css/bootstrap.min.css\" integrity=\"sha384-Gn5384xqQ1aoWXA+058RXPxPg6fy4IWvTNh0E263XmFcJlSAwiGgFAW/dAiS6JXm\" crossorigin=\"anonymous\">");
    puts("<script src='https://kit.fontawesome.com/a076d05399.js'></script>");
    puts("</head>");
    puts("<body  class=\"text-center\">");
    puts("<div class=\"container\">");
    puts("<p></p>");
    puts("<h1><i class='fas fa-lock' style='font-size:74px'></i></h1>");
    puts("<h1  class=\"mt-5\">Mensaje secreto</h3>");
    puts("<p></p>");
    puts("<div class=\"container\">");
    printf("<p>Este programa mostrará un mensaje con la contraseña correcta.</p>");
    if(*params == '\0') {
        printf("   <p>Debe introducir parámetros.</p>");
    }
    else if(*usr == '\0') {
        printf("   <p>Debe introducir un nombre de usuario como primer parámetro.</p>");
    }
    else if(*pwd == '\0') {
        printf("   <p>Debe introducir la contraseña como segundo parámetro.</p>");
    }
    else if(!strcmp(password, pwd)) {
        printf("   <p><i class='fas fa-cat' style='font-size:36px'></i> Lo logró %s, la contraseña era: %s.</p>", buffer, password);
        printf("   <p>El secreto es: %s</p>", SECRET);
    }
    else {
        printf("   <p>Incorrecto %s, esa no es la contraseña.</p>", buffer);
        //printf("   <p>La contraseña es: %s.</p>", password);
    }
    puts("</div>");
    puts("</body>");
    puts("<script src=\"https://code.jquery.com/jquery-3.2.1.slim.min.js\" integrity=\"sha384-KJ3o2DKtIkvYIK3UENzmM7KCkRr/rE9/Qpg6aAZGJwFDMVNA/GpGFF93hXpG5KkN\" crossorigin=\"anonymous\"></script>\n"
         "<script src=\"https://cdnjs.cloudflare.com/ajax/libs/popper.js/1.12.9/umd/popper.min.js\" integrity=\"sha384-ApNbgh9B+Y1QKtv3Rn7W3mgPxhU9K/ScQsAP7hUibX39j7fakFPskvXusvfa0b4Q\" crossorigin=\"anonymous\"></script>\n"
         "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/4.0.0/js/bootstrap.min.js\" integrity=\"sha384-JZR6Spejh4U02d8jOt6vLEHfe/JQGiRRSQQxSfFWpi1MquVdAyjUar5+76PVCmYl\" crossorigin=\"anonymous\"></script>");
    puts("</html>");

    return 0;
}
