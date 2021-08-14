# Sistemas Operativos - Segunda Tarea: Mi DDoS

Segunda tarea del curso de Principios de Sistemas Operativos (código ic-6600) en la carrera de Ingeniería en Computación del Tecnológico de Costa Rica.

## Objetivo

Crear un WebServer el cual utiliza enteramente el protocolo HTTP/1.1 para su comunicación. Este WebServer podría ejecutarse en modo pre-threaded o pre-forked. Además crear un cliente web que busque vencer los webservers dejándolos sin posibilidad de atender a otro cliente.

## Requerimientos

### Pre-thread WebServer
Se debe de crear un web server el cual implemente la técnica llamada prethread. Esta técnica consiste en crear previamente varios hilos de ejecución del método que atiende la solicitudes. Estos hilos se crean utilizando la biblioteca pthreads de Unix. Debe de recibir como parámetro el número de hilos N que se deben pre-crear, el webserver escuchará en el puerto estándar de HTTP, y tendrá N hilos posibles para atender la solicitud, cada solicitud se atenderá por el primer hilo que esté disponible. En caso que no existan más disponibles mostrará un mensaje de error, indicando que se ha sobrepasado el número de clientes que pueden ser atendidos.

### Pre-forked WebServer
Se debe de crear un web server el cual implemente la técnica llamada pre-forked. Esta técnica consiste en crear previamente varios procesos del método que atiende la solicitudes. Estos procesos se crean utilizando el system call estándar de Unix. Debe de recibir como parámetro el número de procesos N que se deben pre-crear, el webserver escuchará en el puerto estándar de HTTP, y tendrá N procesos posibles para atender la solicitud, cada solicitud se atenderá por el primer proceso que esté disponible. En caso que no existan más disponibles mostrará un mensaje de error, indicando que se ha sobrepasado el número de clientes que pueden ser atendidos.

### HTTPClient 
Se debe crear un cliente HTTP el cual permita descargar un recurso. Para ello utilice la biblioteca curl en el lenguaje Python. HTTPClient en C Se debe crear un cliente HTTP el cual permita descargar un recurso. Para ello utilice el lenguaje de programación C. StressCMD Se debe crear una aplicación que reciba como parámetro un ejecutable (Además de los parámetros del ejecutable). Luego debe de crear la cantidad de hilos que el cliente especifique, con el objetivo de lanzar un ataque de Denegación de Servicio. El principal objetivo de unir el HTTPClient y el StressCMD es de saturar los webservers hasta que estos se queden posibilidad de atender otro cliente más. En el lenguaje Python.

### CGI Injector (exploit) 
Este cliente se encargará de inyectar código nuevo al servidor anteriormente creado.

## Funcionamiento del programa

### Ejecución del código PreThreadedServer

El siguiente es un ejemplo para ejecutar el servidor con la configuración de Pre-Thread en el puerto 8080, con la carpeta raíz "resources" y con 10 hilos atendiendo a los clientes.

```console
gcc -o PreThreadedServer PreThreadedServer.c -lpthread
./PreThreadedServer -p 8080 -w resources -n 10
```

### Ejecución del código PreForkedServer

Para ejecutar el servidor con la técnica de Pre-Fork con el puerto 8080, con la carpeta raíz "resources" y con 10 procesos atendiendo las solicitudes se deberán seguir los siguientes comandos.

```console
gcc -o PreForkedServer PreForkedServer.c
./PreThreadedServer -p 8080 -w resources -n 10
```

### Ejecución del código httpclient

A continuación un ejemplo de cómo compilar y ejecutar el Cliente HTTP en C, con una URL de ejemplo.

```console
gcc httpclient.c -o httpclient
./httpclient -u 127.0.0.1:8686/index.html
```

### Ejecución del código httpclient.py

Para ejecutar el cliente HTTP programado en python para obtener el recurso en la URL 127.0.0.1:8686/index.html se deberá utilizar el siguiente comando.

```console
python3 httpclient.py -u 127.0.0.1:8686/index.html
```

### Ejecución del código stress.py

Para ejecutar la prueba de estrés se indica el numero de solicitudes, en el ejemplo 50, y finalmente los parámetros requeridos por el cliente, la URL con el recurso a solicitar.

```console
python3 stress.py -n 50 httpclient -u 127.0.0.1:8686/index.html
```

## Estado

El programa funciona de manera correcta, y fueron implementadas las funcionalidades solicitadas.

## Realizado por:

* Brandon Ledezma Fernández

* Walter Morales Vásquez
