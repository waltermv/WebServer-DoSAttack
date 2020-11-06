"""
Autores:
Brandon Ledezma Fernández
Walter Morales Vásquez

Script para realizar N cantidad de consultas a un servidor web mediante una programa de cliente,
en este caso los clientes HTTP httpclient.py y httpclient.c

Para ejecutar el programa:

    python3 stress.py -n 50 [argumentos del cliente]

Donde 50 representa la cantidad de consultas a realizar y los argumentos del cliente los valores requeridos por
el programa cliente.
"""
import os
import sys
import threading
import time


# Funcion para executar un comando en el sistema
def execute(cmd):
    os.system(cmd)  # Se ejecuta el comando


# Funcion principal del script, recive los argumentos disponibles en la terminal
def main(argv):
    if len(argv) != 6:  # Se esperan un total de 6 elementos
        exit("Invalid Parameters")
    parameters = list()
    if argv[3] == "httpclient.py":  # Si se desea ejecutar el cliente en python
        parameters = ["python3", "httpclient.py"] + argv[-2:]  # Se establecen de esta manera la lista de parametros
    elif argv[3] == "httpclient":  # Si se desea ejecutar el cliente en C
        parameters = ["./httpclient"] + argv[-2:]  # Se establecen de esta otra manera la lista de parametros
    for i in range(int(argv[2])):  # Se ejecuta n veces en hilos el comando
        thread = threading.Thread(target=execute, args=(
        " ".join(parameters),))  # A un hilo se le establece la funcion a ejecutar y los parametros
        thread.start()  # Se inicia el hilo
        time.sleep(0.01)  # Se utiliza un sleep para que el GIL no tenga inconvenientes


# Llamado al main con los argumentos del sistema
if __name__ == '__main__':
    main(sys.argv)
