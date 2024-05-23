// // PUEDE USAR EL EJEMPLO DE SOCKETS servidor.c COMO PUNTO DE PARTIDA.
// #include <stdio.h>
// #include "master.h"
// #include "common.h"
// #include "common_srv.h"
// #include "map.h"
// #include "array.h"

// int main(int argc, char *argv[]) {
//     if (argc!=2) {
//         fprintf(stderr, "Uso: %s puerto\n", argv[0]);
//         return -1;
//     }
//     return 0;
// }

// EJEMPLO DE SERVIDOR MULTITHREAD QUE RECIBE PETICIONES DE LOS CLIENTES.
// PUEDE USARLO COMO BASE PARA DESARROLLAR EL MASTER Y EL SERVER DE LA PRÁCTICA.
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <pthread.h>
#include "common_srv.h"
#include "util/map.h"
#include "array.h"
#include "common.h"
#include "master.h"

typedef struct server_info
{
    unsigned long ip;
    unsigned short puerto;
} server_info;

array *servers;

typedef struct file_info
{
    int blocksize;
    int rep_factor;
    char *fname;
    array *block_list;
} file_info;

map *files_map;

// información que se la pasa el thread creado
typedef struct thread_info
{
    int socket;
    unsigned long ip;
} thread_info;

// asigna servidores siguiendo un turno rotatorio
static int alloc_srv(array *lista_srv){
    static int next_srv=0;
    int lista_size = array_size(lista_srv);
    if (lista_size==0) return -1; // no hay servidores
    return __sync_fetch_and_add(&next_srv,1) % lista_size; // op. atómica
}

// función del thread
void *servicio(void *arg)
{
    thread_info *thinf = arg; // argumento recibido

    char op_code;
    int longitud;
    int blocksize = 0;
    int rep_factor = 0;
    char *fname;

    // si recv devuelve <=0 el cliente ha cortado la conexión;
    // recv puede devolver menos datos de los solicitados
    // (misma semántica que el "pipe"), pero con MSG_WAITALL espera hasta que
    // se hayan recibido todos los datos solicitados o haya habido un error.
    while (1)
    {

        // Cada petición comienza con un entero que representa el código de operación
        if (recv(thinf->socket, &op_code, sizeof(char), MSG_WAITALL) != sizeof(char))
            break;
        printf("Recibido op_code: %c\n", op_code);

        if (op_code == 'C') // crea fichero
        {
            file_info *f;
            f = malloc(sizeof(file_info));
            // Operación de creación de un fichero

            // Viene seguido de un string precedido de su longitud
            if (recv(thinf->socket, &longitud, sizeof(int), MSG_WAITALL) != sizeof(int)){
                free(f);
                break;
            }
            longitud = ntohl(longitud);
            f->fname = malloc(longitud + 1); // +1 para el carácter nulo

            if (recv(thinf->socket, f->fname, longitud, MSG_WAITALL) != longitud){
                free(f->fname);
                free(f);
                break;
            }
            f->fname[longitud] = '\0';
            printf("Recibido fname: %s\n", f->fname);

            // Termina con dos enteros que representan blocksize y rep_factor
            if (recv(thinf->socket, &f->blocksize, sizeof(int), MSG_WAITALL) != sizeof(int)){
                free(f->fname);
                free(f);
                break;
            }
            printf("Recibido blocksize: %d\n", blocksize);

            if (recv(thinf->socket, &f->rep_factor, sizeof(int), MSG_WAITALL) != sizeof(int)){
                free(f->fname);
                free(f);
                break;
            }
            printf("Recibido rep_factor: %d\n", rep_factor);

            f->block_list = array_create(1); // inicializa el array

	        int result = map_put(files_map, f->fname, f);

            // enviamos el descriptor por el socket
            if (write(thinf->socket, &result, sizeof(int)) < 0){
                perror("error en write");
                free(f->fname);
                free(f);
                break;
            }

        }
        else if(op_code == 'N') // nº ficheros
        {
            int size = map_size(files_map);
            if (write(thinf->socket, &size, sizeof(int)) < 0){
                perror("error en write");
                break;
            }
        }
        else if(op_code == 'O') // abre fichero
        {
            // Viene seguido de un string precedido de su longitud
            if (recv(thinf->socket, &longitud, sizeof(int), MSG_WAITALL) != sizeof(int))
                break;
            longitud = ntohl(longitud);
            fname = malloc(longitud + 1); // +1 para el carácter nulo

            if (recv(thinf->socket, fname, longitud, MSG_WAITALL) != longitud){
                free(fname);
                break;
            }
            fname[longitud] = '\0';
            printf("Recibido fname: %s\n", fname);

            // buscamos file en el mapa
            int err = 0;
            file_info *fAux;
            fAux = map_get(files_map, fname, &err); // err guarda el resultado de la operación que enviaremos a la biblioteca
            free(fname);
            if (err == -1){
                perror("error en map_get");
                break;
            }
            
            // si se ha encontrado, enviamos como respuesta el tamaño de bloque y el factor de replicación
            struct iovec iov[3];
            iov[0].iov_base = &err;
            iov[0].iov_len = sizeof(int);

            iov[1].iov_base = &fAux->blocksize;
            iov[1].iov_len = sizeof(int);

            iov[2].iov_base = &fAux->rep_factor;
            iov[2].iov_len = sizeof(int);

            if (writev(thinf->socket, iov, 3) < 0){
                perror("error en writev");
                break;
            }
        }
        else if(op_code == 'L') // da de alta un servidor
        {

            unsigned short puerto;
            int puerto_net;
            if (recv(thinf->socket, &puerto_net, sizeof(int), MSG_WAITALL) != sizeof(int))
                break;
            puerto = ntohs(puerto_net);
            printf("Recibido puerto: %d\n", puerto);

            server_info *new_server;
            new_server = malloc(sizeof(server_info));
            new_server->ip = thinf->ip;
            new_server->puerto = puerto;

            array_append(servers, new_server);
            return NULL;
        }
        else if(op_code == 'S') // info del servidor
        {
            int n_server_net, n_server;
            if (recv(thinf->socket, &n_server_net, sizeof(int), MSG_WAITALL) != sizeof(int))
                break;
            n_server = ntohl(n_server_net);
            printf("Recibido número de servidor: %d\n", n_server);

            int err;
            server_info *server;
            server = array_get(servers, n_server, &err);
            if (err == -1){
                perror("error en array_get, el servidor no existe");
                break;
            }

            struct iovec iov[2];

            int puerto_net = htons(server->puerto);
            printf("puerto enviado %d\n", server->puerto);
            iov[0].iov_base = &puerto_net;
            iov[0].iov_len = sizeof(int);

            int ip_net = htonl(server->ip);
            // printf("ip enviada %d\n", server->ip);
            iov[1].iov_base = &ip_net;
            iov[1].iov_len = sizeof(int);

            if (writev(thinf->socket, iov, 2) < 0){
                perror("error en writev");
                break;
            }
        }
        else if(op_code == 'A') // asigna servidor a bloque
        {
            if (recv(thinf->socket, &longitud, sizeof(int), MSG_WAITALL) != sizeof(int))
                break;
            longitud = ntohl(longitud);
            fname = malloc(longitud + 1); // +1 para el carácter nulo

            if (recv(thinf->socket, fname, longitud, MSG_WAITALL) != longitud){
                free(fname);
                break;
            }
            fname[longitud] = '\0';
            printf("Recibido fname: %s\n", fname);

            int err = 0;
            file_info *fAux;
            fAux = map_get(files_map, fname, &err);
            free(fname);
            if (err == -1){
                perror("error en map_get");
                break;
            }

            int serv_asig = alloc_srv(servers); // usa alloc_srv para elegir el servidor
            server_info *server;
            server = array_get(servers, serv_asig, &err);
            if (err == -1){
                perror("error en array_get, el servidor no existe");
                break;
            }
            printf("server asignado es el numero %d\n\n\n\n", serv_asig);
            printf("blocksize y rep_factor del fichero: %d, %d\n\n", fAux->blocksize, fAux->rep_factor);
            printf("el size de block_list es %d\n\n", array_size(fAux->block_list));
            array_append(fAux->block_list, server); // insertamos en la lista de bloques del fichero el descriptor del servidor


            // envía la información del servidor asignado
            struct iovec iov[2];

            int puerto_net = htons(server->puerto);
            iov[0].iov_base = &puerto_net;
            iov[0].iov_len = sizeof(int);

            int ip_net = htonl(server->ip);
            iov[1].iov_base = &ip_net;
            iov[1].iov_len = sizeof(int);

            if (writev(thinf->socket, iov, 2) < 0){
                perror("error en writev");
                break;
            }
        }
        else if (op_code == 'I') // obtiene información de la asignación de servidores a bloques
        {
            if (recv(thinf->socket, &longitud, sizeof(int), MSG_WAITALL) != sizeof(int))
                break;
            longitud = ntohl(longitud);
            fname = malloc(longitud + 1); // +1 para el carácter nulo

            if (recv(thinf->socket, fname, longitud, MSG_WAITALL) != longitud){
                free(fname);
                break;
            }
            fname[longitud] = '\0';
            printf("Recibido fname: %s\n", fname);

            int n_bloque_net, n_bloque;
            if (recv(thinf->socket, &n_bloque_net, sizeof(int), MSG_WAITALL) != sizeof(int)){
                free(fname);
                break;
            }
            n_bloque = ntohl(n_bloque_net);


            struct iovec iov[4];
            int err = 0;
            file_info *fAux;
            fAux = map_get(files_map, fname, &err);
            free(fname);
            iov[0].iov_base = &err;
            iov[0].iov_len = sizeof(int);

            int err2 = -1;
            server_info *server;
            if (err != -1){
                printf("n_bloque recibido %d\n", n_bloque);
                server = array_get(fAux->block_list, n_bloque, &err2); // busca el descriptor de servidor asignado para n_bloque
            }

            iov[1].iov_base = &err2;
            iov[1].iov_len = sizeof(int);


            // envía la información del servidor asignado

            if (err != -1 && err2 != -1){
                int puerto_net = htons(server->puerto);
                iov[2].iov_base = &puerto_net;
                iov[2].iov_len = sizeof(int);

                int ip_net = htonl(server->ip);
                iov[3].iov_base = &ip_net;
                iov[3].iov_len = sizeof(int);
            }
            else{
                iov[2].iov_base = &err;
                iov[2].iov_len = sizeof(int);

                iov[3].iov_base = &err2;
                iov[3].iov_len = sizeof(int);
            }
            

            if (writev(thinf->socket, iov, 4) < 0){
                perror("error en writev");
                break;
            }
        }
    }
    close(thinf->socket);
    free(thinf);
    printf("conexión del cliente cerrada\n");
    return NULL;
}

int main(int argc, char *argv[])
{
    int s, s_conec;
    unsigned int tam_dir;
    struct sockaddr_in dir_cliente;

    if (argc != 2)
    {
        fprintf(stderr, "Uso: %s puerto\n", argv[0]);
        return -1;
    }
    // inicializa el socket y lo prepara para aceptar conexiones
    if ((s = create_socket_srv(atoi(argv[1]), NULL)) < 0)
        return -1;

    servers = array_create(1); // array con cerrojos

    files_map = map_create(key_string, 1); // la clave será el nombre del fichero, mapa con cerrojos

    // prepara atributos adecuados para crear thread "detached"
    pthread_t thid;
    pthread_attr_t atrib_th;
    pthread_attr_init(&atrib_th); // evita pthread_join
    pthread_attr_setdetachstate(&atrib_th, PTHREAD_CREATE_DETACHED);
    while (1)
    {
        tam_dir = sizeof(dir_cliente);
        // acepta la conexión
        if ((s_conec = accept(s, (struct sockaddr *)&dir_cliente, &tam_dir)) < 0)
        {
            perror("error en accept");
            close(s);
            return -1;
        }
        printf("conectado cliente con ip %u y puerto %u (formato red)\n",
               dir_cliente.sin_addr.s_addr, dir_cliente.sin_port);
        // crea el thread de servicio
        thread_info *thinf = malloc(sizeof(thread_info));
        thinf->socket = s_conec;
        thinf->ip = dir_cliente.sin_addr.s_addr;
        pthread_create(&thid, &atrib_th, servicio, thinf);
    }
    close(s); // cierra el socket general
    return 0;
}
