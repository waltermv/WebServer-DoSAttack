"""
Autores:
Brandon Ledezma Fernández
Walter Morales Vásquez

Cliente HTTP capaz de realizar colsultas a un servidor, mediante los metodos de GET, POST, DELETE y PUT.

Para ejecutar el programa:

    GET: python3 httpclient.py -u [direccion del recurso]

Donde direccion del recurso significa la ubicacion del servidor con la ruta en especifico del recurso.

    POST: python3 httpclient.py -u [direccion del recurso] POST param1:value1,param2:value2

Donde direccion del recurso significa la ubicacion del servidor con la ruta en especifico del recurso y los
campos param y value los parametros que se desean enviar, se pueden enviar varios dividiendolos por coma [,].

    PUT: python3 httpclient.py -u [direccion del recurso] PUT [recurso]

Donde direccion del recurso significa la ubicacion del servidor con la ruta en especifico del recurso el nombre del
recurso que desea enviar.

    DELETE: python3 httpclient.py -u [direccion del recurso] DELETE

Donde direccion del recurso significa la ubicacion del servidor con la ruta en especifico del recurso que se desea
eliminar.
"""
import os
import sys
from io import BytesIO
import pycurl
from urllib.parse import urlencode


# Funcion para utilizar el metodo GET con un servidor
def getRequest(url):
    file = open(url.split("/")[-1], "wb")  # Se abre un archivo para escribir los resultados
    buffer = BytesIO()
    curl = pycurl.Curl()  # Se establece una conexion
    curl.setopt(curl.URL, url)
    curl.setopt(curl.WRITEDATA, file)  # Los datos obtenidos se guardan en el archivo
    curl.setopt(pycurl.HEADERFUNCTION, buffer.write)  # Se lee el encabezado
    curl.perform()
    header = buffer.getvalue().splitlines()
    curl.close()  # Se cierra la conexion
    file.close()  # Se cierra el archivo
    for i in header:  # Se imprimen los encabezados
        print(i.decode("utf-8"))


# Funcion para utilizar el metodo POST con un servidor
def postRequest(url, data):
    file = open(url.split("/")[-1], "wb")  # Se abre un archivo para almacenar los resultados
    buffer = BytesIO()
    curl = pycurl.Curl()  # Se establece la conexion
    curl.setopt(curl.URL, url)
    postFields = urlencode(data)  # Se le pasan los parametros
    curl.setopt(curl.POSTFIELDS, postFields)
    curl.setopt(curl.WRITEDATA, file)  # Se optiene el resultado
    curl.setopt(pycurl.HEADERFUNCTION, buffer.write)  # Se lee los encabezados
    curl.perform()
    header = buffer.getvalue().splitlines()
    curl.close()  # Se cierra la conexion
    file.close()  # Se cierra el archivo
    for i in header:  # Se imprimen los encabezados
        print(i.decode("utf-8"))


# Funcion para agregar un recurso del servidor
def putMethod(url, fileName):
    buffer = BytesIO()
    curl = pycurl.Curl()  # Se establece la conexion utilizando curl
    length = os.stat(fileName).st_size
    curl.setopt(curl.URL, url)
    curl.setopt(pycurl.UPLOAD, 1)
    curl.setopt(pycurl.INFILESIZE, length) # Se asigna el largo del contenido
    file = open(fileName, 'rb')  # Se abre el archivo a colocar
    curl.setopt(curl.READDATA, file) # Se agrega el archivo
    curl.setopt(pycurl.HEADERFUNCTION, buffer.write)  # Se lee el encabezado
    curl.perform()
    file.close()  # Se cierra el archivo
    header = buffer.getvalue().splitlines()
    curl.close()  # Se cierra la conexion

    for i in header:  # Se imprimen los encabezados
        print(i.decode("utf-8"))


# Funcion para eliminar un recurso del servidor
def deleteMethod(url):
    buffer = BytesIO()
    curl = pycurl.Curl()  # Se establece la conexion utilizando curl
    curl.setopt(curl.URL, url)
    curl.setopt(pycurl.CUSTOMREQUEST, "DELETE")
    curl.setopt(pycurl.HEADERFUNCTION, buffer.write)
    curl.perform()  # Se realiza la consulta
    header = buffer.getvalue().splitlines()  # Se obtiene el resultado del encabezado
    curl.close()  # Se cierra la conexion
    for i in header:  # Se imprime los encabezados en consola
        print(i.decode("utf-8"))


# Funcion para elegir la el metodo a utilizar
def createRequest(method, url, args):
    if method == "GET":  # GET es el metodo por defecto
        getRequest(url)
    elif method == "POST":  # Si el metodo es un POST, se agregaran al cuerpo los parametros en el cuerpo
        data = {}  # Se utiliza un diccionario para parsear los parametros
        if len(args) == 5:
            fields = args[4].split(',')  # Se dividen los parametros
            for i in fields:  # Por cada parametro se incluye en el diccionario los valores
                data[i.split(':')[0]] = i.split(':')[1]
        postRequest(url, data)
    elif method == "PUT":  # Si es el metodo PUT, el 5to argumento es el nombre del archivo o la ruta
        putMethod(url, args[4])
    elif method == "DELETE":  # Medolo para eliminar un recurso del servidor, se obtiene de la URL el nombre
        deleteMethod(url)
    else:  # Si el metodo no es reconocido se dectiene
        sys.exit("Method not supported")


# Funcion principal del cliente, se leen los parametros obtenidos de consola
def main(args):
    if len(args) < 2:
        exit("Invalid parameters")
    url = args[2]
    if not ("http://" in url):  # Se agrega el prefijo al URL sino lo posee
        url = "http://" + url
    method = "GET"  # El metodo por defecto es GET
    if len(args) >= 4:  # Si la cantidad de parametros obtenidos es mayor igual a 4, es porque se estar usando
        method = args[3]  # Los metodos de PUT, DELETE o POST
    createRequest(method, url, args)


if __name__ == '__main__':
    main(sys.argv)
