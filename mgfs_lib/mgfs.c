// IMPLEMENTACIÓN DE LA BIBLIOTECA DE CLIENTE.
// PUEDE USAR EL EJEMPLO DE SOCKETS cliente.c COMO PUNTO DE PARTIDA.
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/uio.h>
#include "common.h"
#include "common_cln.h"
#include "master.h"
#include "server.h"
#include "mgfs.h"

// TIPOS INTERNOS

// descriptor de un sistema de ficheros:
// tipo interno que almacena información de un sistema de ficheros
typedef struct mgfs_fs
{
    int socket;
    int def_blocksize;
    int def_rep_factor;
} mgfs_fs;

// descriptor de un fichero:
// tipo interno que almacena información de un fichero
typedef struct mgfs_file
{
    mgfs_fs *fs;
    char *fname;
    int blocksize;
    int rep_factor;
    int n_bloque_sig;
} mgfs_file;

/*
 * FASE 1: CREACIÓN DE FICHEROS
 */

// PASO 1: CONEXIÓN Y DESCONEXIÓN

// Establece una conexión con el sistema de ficheros especificado,
// fijando los valores por defecto para el tamaño de bloque y el
// factor de replicación para los ficheros creados en esta sesión.
// Devuelve el descriptor del s. ficheros si OK y NULL en caso de error.
mgfs_fs *mgfs_connect(const char *master_host, const char *master_port,
                      int def_blocksize, int def_rep_factor)
{
    mgfs_fs *fs = malloc(sizeof(mgfs_fs));
    if (fs == NULL)
    {
        perror("error en malloc");
        return NULL;
    }

    fs->socket = create_socket_cln_by_name(master_host, master_port);
    if (fs->socket == -1)
    {
        perror("error en create_socket_cln_by_name");
        free(fs);
        return NULL;
    }
    fs->def_blocksize = def_blocksize;
    fs->def_rep_factor = def_rep_factor;

    return fs;
}
// Cierra la conexión con ese sistema de ficheros.
// Devuelve 0 si OK y un valor negativo en caso de error.
int mgfs_disconnect(mgfs_fs *fs)
{
    if (fs == NULL)
    {
        perror("conexión no existente");
        return -1;
    }

    close(fs->socket);
    free(fs);
    return 0;
}
// Devuelve tamaño de bloque por defecto y un valor negativo en caso de error.
int mgfs_get_def_blocksize(const mgfs_fs *fs)
{
    if (fs == NULL)
    {
        // perror("conexión no existente");
        return -1;
    }
    return fs->def_blocksize;
}
// Devuelve factor de replicación por defecto y valor negativo en caso de error.
int mgfs_get_def_rep_factor(const mgfs_fs *fs)
{
    if (fs == NULL)
    {
        // perror("conexión no existente");
        return -1;
    }
    return fs->def_rep_factor;
}

// PASO 2: CREAR FICHERO

// Crea un fichero con los parámetros especificados.
// Si blocksize es 0, usa el valor por defecto.
// Si rep_factor es 0, usa el valor por defecto.
// Devuelve el descriptor del fichero si OK y NULL en caso de error.
mgfs_file *mgfs_create(const mgfs_fs *fs, const char *fname,
                       int blocksize, int rep_factor)
{
    if (fs == NULL)
    {
        // perror("conexión no existente");
        return NULL;
    }
    mgfs_file *file = malloc(sizeof(mgfs_file));
    if (file == NULL)
    {
        // perror("error en malloc");
        return NULL;
    }

    file->n_bloque_sig = 0;
    file->blocksize = blocksize == 0 ? fs->def_blocksize : blocksize;
    file->rep_factor = rep_factor == 0 ? fs->def_rep_factor : rep_factor;
    file->fs = fs;
    file->fname = strdup(fname);

    char op_code = 'C';
    //    op_code = htonl(op_code);

    struct iovec iov[5];
    iov[0].iov_base = &op_code;
    iov[0].iov_len = sizeof(char);

    int longitud_fname = strlen(file->fname);
    int longitud_fname_net = htonl(longitud_fname);
    iov[1].iov_base = &longitud_fname_net;
    iov[1].iov_len = sizeof(int);

    iov[2].iov_base = file->fname;
    iov[2].iov_len = longitud_fname;

    iov[3].iov_base = &file->blocksize;
    iov[3].iov_len = sizeof(int);

    iov[4].iov_base = &file->rep_factor;
    iov[4].iov_len = sizeof(int);

    if (writev(fs->socket, iov, 5) < 0)
    {
        // perror("error en writev");
        free(file);
        return NULL;
    }

    int result;
    if (recv(fs->socket, &result, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        // perror("error en recv");
        free(file);
        return NULL;
    }

    if (result == -1)
    {
        // perror("error al crear el fichero");
        free(file);
        return NULL;
    }

    return file;
}
// Cierra un fichero.
// Devuelve 0 si OK y un valor negativo si error.
int mgfs_close(mgfs_file *f)
{
    if (f == NULL)
    {
        return -1;
    }

    free(f->fname);
    free(f);
    return 0;
}
// Devuelve tamaño de bloque y un valor negativo en caso de error.
int mgfs_get_blocksize(const mgfs_file *f)
{
    if (f == NULL)
    {
        return -1;
    }
    return f->blocksize;
}
// Devuelve factor de replicación y valor negativo en caso de error.
int mgfs_get_rep_factor(const mgfs_file *f)
{
    if (f == NULL)
    {
        return -1;
    }
    return f->rep_factor;
}

// Devuelve el nº ficheros existentes y un valor negativo si error.
int _mgfs_nfiles(const mgfs_fs *fs)
{
    if (fs == NULL)
    {
        return -1;
    }
    char op_code = 'N';
    write(fs->socket, &op_code, sizeof(char));

    int n_files;
    read(fs->socket, &n_files, sizeof(int));

    return n_files;
}

// PASO 3: APERTURA DE FICHERO PARA LECTURA

// Abre un fichero para su lectura.
// Devuelve el descriptor del fichero si OK y NULL en caso de error.
mgfs_file *mgfs_open(const mgfs_fs *fs, const char *fname)
{
    if (fs == NULL)
    {
        // perror("conexión no existente");
        return NULL;
    }
    mgfs_file *file = malloc(sizeof(mgfs_file));
    if (file == NULL)
    {
        // perror("error en malloc");
        return NULL;
    }

    file->fs = fs;
    file->fname = strdup(fname);

    char op_code = 'O';

    struct iovec iov[3];
    iov[0].iov_base = &op_code;
    iov[0].iov_len = sizeof(char);

    int longitud_fname = strlen(file->fname);
    int longitud_fname_net = htonl(longitud_fname);
    iov[1].iov_base = &longitud_fname_net;
    iov[1].iov_len = sizeof(int);

    iov[2].iov_base = file->fname;
    iov[2].iov_len = longitud_fname;

    if (writev(fs->socket, iov, 3) < 0)
    {
        // perror("error en writev");
        free(file);
        return NULL;
    }

    int result;
    if (recv(fs->socket, &result, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        // perror("error en recv");
        free(file);
        return NULL;
    }

    if (result == -1)
    {
        // perror("error al abrir el fichero");
        free(file);
        return NULL;
    }

    if (recv(fs->socket, &file->blocksize, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        // perror("error al recibir el tamaño de bloque");
        free(file);
        return NULL;
    }

    if (recv(fs->socket, &file->rep_factor, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        // perror("error al recibir el factor de replicación");
        free(file);
        return NULL;
    }
    return file;
}

/*
 * FASE 2: ALTA DE LOS SERVIDORES
 */

// Operación interna para test; no para uso de las aplicaciones.
// Obtiene la información de localización (ip y puerto en formato de red)
// de un servidor.
// Devuelve 0 si OK y un valor negativo si error.
int _mgfs_serv_info(const mgfs_fs *fs, int n_server, unsigned int *ip,
                    unsigned short *port)
{
    if (fs == NULL)
    {
        perror("conexión no existente");
        return -1;
    }

    char op_code = 'S';

    struct iovec iov[2];
    iov[0].iov_base = &op_code;
    iov[0].iov_len = sizeof(char);

    int n_server_net = htonl(n_server);
    iov[1].iov_base = &n_server_net;
    iov[1].iov_len = sizeof(int);

    if (writev(fs->socket, iov, 2) < 0)
    {
        perror("error en writev");
        return -1;
    }

    int port_net;
    if (recv(fs->socket, &port_net, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        perror("error en recv");
        return -1;
    }
    *port = ntohs(port_net);
    printf("Puerto recibido %d\n", port);

    int ip_net;
    if (recv(fs->socket, &ip_net, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        perror("error en recv");
        return -1;
    }
    *ip = ntohl(ip_net);
    printf("ip recibida %d, %d\n", ip, ip_net);

    return 0;
}

/*
 * FASE 3: ASIGNACIÓN DE SERVIDORES A BLOQUES.
 */

// Operación interna: será usada por write.
// Asigna servidores a las réplicas del siguiente bloque del fichero.
// Devuelve la información de localización (ip y puerto en formato de red)
// de cada una de ellas.
// Retorna 0 si OK y un valor negativo si error.
int _mgfs_alloc_next_block(const mgfs_file *file, unsigned int *ips, unsigned short *ports)
{
    if (file == NULL)
    {
        //        perror("file no existente");
        return -1;
    }
    if (file->fs == NULL)
    {
        //        perror("conexión no existente");
        return -1;
    }

    char op_code = 'A';

    struct iovec iov[3];
    iov[0].iov_base = &op_code;
    iov[0].iov_len = sizeof(char);

    int longitud_fname = strlen(file->fname);
    int longitud_fname_net = htonl(longitud_fname);
    iov[1].iov_base = &longitud_fname_net;
    iov[1].iov_len = sizeof(int);

    iov[2].iov_base = file->fname;
    iov[2].iov_len = longitud_fname;

    if (writev(file->fs->socket, iov, 3) < 0)
    {
        //        perror("error en writev");
        return -1;
    }

    int port_net;
    if (recv(file->fs->socket, &port_net, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        //        perror("error en recv");
        return -1;
    }
    *ports = ntohs(port_net);
    //    printf("Puerto recibido %d\n", ports);

    int ip_net;
    if (recv(file->fs->socket, &ip_net, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        //        perror("error en recv");
        return -1;
    }
    *ips = ntohl(ip_net);
    //    printf("ip recibida %d, %d\n", ips, ip_net);

    return 0;
}

// Obtiene la información de localización (ip y puerto en formato de red)
// de los servidores asignados a las réplicas del bloque.
// Retorna 0 si OK y un valor negativo si error.
int _mgfs_get_block_allocation(const mgfs_file *file, int n_bloque,
                               unsigned int *ips, unsigned short *ports)
{
    if (file == NULL){
        perror("file no existente");
        return -1;
    }
    if (file->fs == NULL){
        perror("conexión no existente");
        return -1;
    }

    char op_code = 'I';

    struct iovec iov[4];
    iov[0].iov_base = &op_code;
    iov[0].iov_len = sizeof(char);

    int longitud_fname = strlen(file->fname);
    int longitud_fname_net = htonl(longitud_fname);
    iov[1].iov_base = &longitud_fname_net;
    iov[1].iov_len = sizeof(int);

    iov[2].iov_base = file->fname;
    iov[2].iov_len = longitud_fname;

    int n_bloque_net = htonl(n_bloque);
    iov[3].iov_base = &n_bloque_net;
    iov[3].iov_len = sizeof(int);

    if (writev(file->fs->socket, iov, 4) < 0)
    {
        //        perror("error en writev");
        return -1;
    }

    int err;
    // read(file->fs->socket, &err, sizeof(int));
    recv(file->fs->socket, &err, sizeof(int), MSG_WAITALL);
    // if (err == -1)
    //     return -1;
    
    int err2;
    // read(file->fs->socket, &err, sizeof(int));
    recv(file->fs->socket, &err2, sizeof(int), MSG_WAITALL);
    // if (err == -1)
    //     return -1;

    int port_net;
    if (recv(file->fs->socket, &port_net, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        //        perror("error en recv");
        return -1;
    }
    *ports = ntohs(port_net);
    //    printf("Puerto recibido %d\n", ports);

    int ip_net;
    if (recv(file->fs->socket, &ip_net, sizeof(int), MSG_WAITALL) != sizeof(int))
    {
        //        perror("error en recv");
        return -1;
    }
    *ips = ntohl(ip_net);
    //    printf("ip recibida %d, %d\n", ips, ip_net);
    if (err == -1 || err2 == -1)
        return -1;

    return 0;
}

/*
 * FASE 4: ESCRITURA EN EL FICHERO.
 */

// Escritura en el fichero.
// Devuelve el tamaño escrito si OK y un valor negativo si error.
// Por restricciones de la práctica, "size" tiene que ser múltiplo
// del tamaño de bloque y el valor devuelto deber ser igual a "size".
int mgfs_write(mgfs_file *file, const void *buff, unsigned long size)
{
    if (file == NULL)
    {
        perror("file no existente");
        return -1;
    }
    if (file->fs == NULL)
    {
        perror("conexión no existente");
        return -1;
    }

    int blocksize = mgfs_get_blocksize(file);
    if (size % blocksize)
        return -1;

    int num_blocks = size / blocksize;
    int bytes_escritos_total = 0;

    for (int i = 0; i < num_blocks; i++)
    {
        unsigned int ips[file->rep_factor];
        unsigned short ports[file->rep_factor];
        if (_mgfs_alloc_next_block(file, ips, ports) < 0)
        {
            perror("error en _mgfs_alloc_next_block");
            return -1;
        }

        int sockfd = create_socket_cln_by_addr(ips[0], ports[0]);
        if (sockfd < 0)
        {
            perror("error en create_socket_cln_by_addr");
            return -1;
        }

        // envía al servidor el nombre del fichero, número de bloque y el contenido a escribir
        struct iovec iov[5];

        int longitud_fname = strlen(file->fname);
        int longitud_fname_net = htonl(longitud_fname);
        iov[0].iov_base = &longitud_fname_net;
        iov[0].iov_len = sizeof(int);

        iov[1].iov_base = file->fname;
        iov[1].iov_len = longitud_fname;

        int n_bloque_net = htonl(file->n_bloque_sig);
        iov[2].iov_base = &n_bloque_net;
        iov[2].iov_len = sizeof(int);
        file->n_bloque_sig++;

        int blocksize_net = htonl(blocksize);
        iov[3].iov_base = &blocksize_net;
        iov[3].iov_len = sizeof(int);

        iov[4].iov_base = buff;
        iov[4].iov_len = blocksize;

        if (writev(sockfd, iov, 5) < 0)
        {
            perror("error en writev");
            close(sockfd);
            return -1;
        }

        int bytes_escritos;
        if (recv(sockfd, &bytes_escritos, sizeof(int), MSG_WAITALL) != sizeof(int))
        {
            perror("error en recv");
            close(sockfd);
            return -1;
        }

        bytes_escritos = ntohl(bytes_escritos);
        bytes_escritos_total += bytes_escritos;
        if (bytes_escritos != blocksize)
        {
            perror("error en bytes_escritos");
            close(sockfd);
            return -1;
        }

        close(sockfd);
    }

    return bytes_escritos_total;
}
