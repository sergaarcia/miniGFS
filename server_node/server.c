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
#include <sys/stat.h>
#include <sys/mman.h>

// información que se la pasa el thread creado
typedef struct thread_info
{
    int socket; // añadir los campos necesarios
    char *dir;
} thread_info;

void *servicio(void *arg)
{
    thread_info *thinf = arg; // argumento recibido

    // si recv devuelve <=0 el cliente ha cortado la conexión;
    // recv puede devolver menos datos de los solicitados
    // (misma semántica que el "pipe"), pero con MSG_WAITALL espera hasta que
    // se hayan recibido todos los datos solicitados o haya habido un error.
    while (1)
    {
        int longitud_fname_net;
        if (recv(thinf->socket, &longitud_fname_net, sizeof(int), MSG_WAITALL) != sizeof(int))
            break;
        int longitud_fname = ntohl(longitud_fname_net);

        char *fname = malloc(longitud_fname + 1);
        if (recv(thinf->socket, fname, longitud_fname, MSG_WAITALL) != longitud_fname)
        {
            free(fname);
            break;
        }
        fname[longitud_fname] = '\0';

        int n_bloque_net;
        if (recv(thinf->socket, &n_bloque_net, sizeof(int), MSG_WAITALL) != sizeof(int))
        {
            free(fname);
            break;
        }
        int n_bloque = ntohl(n_bloque_net);

        int num_replica_net;
        if (recv(thinf->socket, &num_replica_net, sizeof(int), MSG_WAITALL) != sizeof(int)){
            free(fname);
            break;
        }
        int num_replica = ntohl(num_replica_net);

        int size_net;
        if (recv(thinf->socket, &size_net, sizeof(int), MSG_WAITALL) != sizeof(int))
        {
            free(fname);
            break;
        }
        int size = ntohl(size_net);
        printf("recibido size %d\n", size);

        char dirname[256];
        sprintf(dirname, "%s/%s", thinf->dir, fname);
        mkdir(dirname, 0755);

        char filename[256];
        sprintf(filename, "%s/%d_%d", dirname, n_bloque, num_replica);
        int fd = open(filename, O_RDWR | O_CREAT, 0666);
        printf("fichero %s creado\n", filename);

        if (ftruncate(fd,size) < 0)
        {
            perror("error en ftruncate");
            break;
        }
        void *p;
        if ((p = mmap(NULL, size, PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED)
        {
            perror("error en mmap");
            close(fd);
            break;
        }
        close(fd);

        if (recv(thinf->socket, p, size, MSG_WAITALL) != size)
            break;

        if (num_replica == 0){
            int num_replicas_net;
            if (recv(thinf->socket, &num_replicas_net, sizeof(int), MSG_WAITALL) != sizeof(int))
            {
                break;
            }
            int num_replicas = ntohl(num_replicas_net);

            unsigned int *ips = malloc(num_replicas * sizeof(unsigned int));
            unsigned short *ports = malloc(num_replicas * sizeof(unsigned short));
            if (recv(thinf->socket, ips, num_replicas * sizeof(unsigned int), MSG_WAITALL) != num_replicas * sizeof(unsigned int)){
                break;
            }
            if (recv(thinf->socket, ports, num_replicas * sizeof(unsigned short), MSG_WAITALL) != num_replicas * sizeof(unsigned short)){
                break;
            }

            int bytes_escritos_total = 0;
            int bytes_escritos = 0;
            for (int i = 1; i < num_replicas; i++){

                int sockfd = create_socket_cln_by_addr(ips[i], ports[i]);
                if (sockfd < 0){
                    perror("error en create_socket_cln_by_addr");
                    return NULL;
                }

                struct iovec iov[6];

                iov[0].iov_base = &longitud_fname_net;
                iov[0].iov_len = sizeof(int);

                iov[1].iov_base = fname;
                iov[1].iov_len = longitud_fname;

                iov[2].iov_base = &n_bloque_net;
                iov[2].iov_len = sizeof(int);

                int n_replica_net = htonl(i);
                iov[3].iov_base = &n_replica_net;
                iov[3].iov_len = sizeof(int);

                iov[4].iov_base = &size_net;
                iov[4].iov_len = sizeof(int);

                iov[5].iov_base = p;
                iov[5].iov_len = size;

                if (writev(sockfd, iov, 6) < 0)
                {
                    perror("error en writev");
                    close(sockfd);
                    return NULL;
                }

                if (read(sockfd, &bytes_escritos, sizeof(int)) != sizeof(int)){
                    perror("error en read");
                    close(sockfd);
                    return NULL;
                }

                bytes_escritos = ntohl(bytes_escritos);
                if (bytes_escritos != size){
                    close(sockfd);
                    return NULL;
                }
                bytes_escritos_total += bytes_escritos;
            }
            int bytes_escritos_net = htonl(size);
            if (write(thinf->socket, &bytes_escritos_net, sizeof(int)) < 0){
                perror("error en write");
                break;
            }
            free(ips);
            free(ports);
        }
        else{
            int bytes_escritos = htonl(size);
            if (write(thinf->socket, &bytes_escritos, sizeof(int)) < 0)
            {
                perror("error en write");
                break;
            }
        }
        free(fname);
    }
    close(thinf->socket);
    free(thinf);
    printf("conexión del cliente cerrada\n");
    return NULL;
}



int main(int argc, char *argv[])
{
    int s, s_conec, s_cli;
    char op_code;
    unsigned int tam_dir;
    struct sockaddr_in dir_cliente;

    if (argc != 4)
    {
        fprintf(stderr, "Uso: %s nombre_dir master_host master_puerto\n", argv[0]);
        return -1;
    }
    // Asegurándose de que el directorio de almacenamiento existe
    mkdir(argv[1], 0755);

    // inicializa el socket y lo prepara para aceptar conexiones
    unsigned short puerto;
    if ((s = create_socket_srv(0, &puerto)) < 0)
        return -1;

    // crea socket de cliente, envia el código de operación junto con el puerto y cierra el socket
    s_cli = create_socket_cln_by_name(argv[2], argv[3]);
    op_code = 'L'; // identificador de operación para dar de alta el servidor

    struct iovec iov[2];
    iov[0].iov_base = &op_code;
    iov[0].iov_len = sizeof(char);

    int puerto_net;
    puerto_net = htons(puerto);
    iov[1].iov_base = &puerto_net;
    iov[1].iov_len = sizeof(int);

    if (writev(s_cli, iov, 2) < 0)
    {
        perror("error en writev");
        close(s);
        close(s_cli);
        return -1;
    }
    close(s_cli); // cierra el socket ya que no se usa más

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
        thinf->dir = argv[1];
        pthread_create(&thid, &atrib_th, servicio, thinf);
    }
    close(s); // cierra el socket general
    return 0;
}