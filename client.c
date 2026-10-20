// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

#include "grafo.h"
#include "protocol.h"

#define SERVER_PORT 8080
#define MAX_BUFFER 1024

// =========================================================
// Variáveis globais
// =========================================================

static grafo_t *g = NULL;

static int sockfd = -1;
static struct sockaddr_storage servaddr;
static socklen_t servaddr_len = 0;
static int usar_ipv6 = 0;

// Status de cada cidade (0 = OK, 1 = ALERTA)
static int *status_cidade = NULL;
static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;

// Sinalização de ACK da telemetria
static int ack_telemetria_ok = 0;
static pthread_mutex_t telem_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t telem_cond = PTHREAD_COND_INITIALIZER;

// Estrutura de missão (compartilhada entre thread 3 e 4)
typedef struct {
    int ativa;
    int id_cidade;
    int id_equipe;
} missao_t;

static missao_t missao_atual = {0, -1, -1};
static pthread_mutex_t missao_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t missao_cond = PTHREAD_COND_INITIALIZER;

// Sinalização de ACK da conclusão de missão
static int ack_conclusao_ok = 0;
static pthread_mutex_t concl_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t concl_cond = PTHREAD_COND_INITIALIZER;

// =========================================================
// Funções auxiliares
// =========================================================

static void enviar_ack(int status_ack) {
    header_t hdr;
    payload_ack_t payload;

    hdr.tipo = MSG_ACK;
    hdr.tamanho = sizeof(payload_ack_t);
    payload.status = status_ack;

    unsigned char buffer[MAX_BUFFER];
    memcpy(buffer, &hdr, sizeof(header_t));
    memcpy(buffer + sizeof(header_t), &payload, sizeof(payload_ack_t));

    ssize_t total = sizeof(header_t) + sizeof(payload_ack_t);
    ssize_t n = sendto(sockfd, buffer, total, 0,
                       (struct sockaddr *)&servaddr, servaddr_len);
    if (n < 0) {
        perror("Erro ao enviar ACK");
    } else {
        printf("[CLIENTE] -> ACK enviado (status=%d)\n", status_ack);
    }
}

// =========================================================
// Thread 1: Simular monitoramento das cidades
// =========================================================

void *thread_monitoramento(void *arg) {
    (void)arg;

    int N = g->num_vertices;
    printf("[T1] Thread de monitoramento iniciada. Cidades: %d\n", N);

    while (1) {
        pthread_mutex_lock(&status_mutex);
        for (int i = 0; i < N; i++) {
            int r = rand() % 100;
            if (r < 3) {
                status_cidade[i] = STATUS_ALERTA;
            } else {
                status_cidade[i] = STATUS_OK;
            }
        }
        pthread_mutex_unlock(&status_mutex);

        int qtd_alertas = 0;
        for (int i = 0; i < N; i++) {
            if (status_cidade[i] == STATUS_ALERTA) {
                qtd_alertas++;
            }
        }
        printf("[T1] Monitoramento atualizado. Alertas nesta rodada: %d\n", qtd_alertas);

        sleep(5);
    }

    return NULL;
}

// =========================================================
// Thread 2: Enviar telemetria a cada 30 segundos
// =========================================================

void *thread_telemetria(void *arg) {
    (void)arg;
    int N = g->num_vertices;

    printf("[T2] Thread de telemetria iniciada.\n");

    while (1) {
        payload_telemetria_t payload;
        payload.total = N;

        pthread_mutex_lock(&status_mutex);
        for (int i = 0; i < N; i++) {
            payload.dados[i].id_cidade = g->cidades[i].id;
            payload.dados[i].status = status_cidade[i];
        }
        pthread_mutex_unlock(&status_mutex);

        header_t hdr;
        hdr.tipo = MSG_TELEMETRIA;
        hdr.tamanho = sizeof(payload_telemetria_t);

        unsigned char buffer[MAX_BUFFER];
        memcpy(buffer, &hdr, sizeof(header_t));
        memcpy(buffer + sizeof(header_t), &payload, sizeof(payload_telemetria_t));
        ssize_t total = sizeof(header_t) + sizeof(payload_telemetria_t);

        int tentativas = 0;
        int ack_ok = 0;

        while (tentativas < 3 && !ack_ok) {
            tentativas++;
            printf("[T2] Enviando MSG_TELEMETRIA (tentativa %d)...\n", tentativas);

            ssize_t n = sendto(sockfd, buffer, total, 0,
                               (struct sockaddr *)&servaddr, servaddr_len);
            if (n < 0) {
                perror("[T2] Erro ao enviar MSG_TELEMETRIA");
                break;
            }

            struct timespec ts;
            ts.tv_sec = time(NULL) + 5;  // agora + 5 segundos
            ts.tv_nsec = 0;

            pthread_mutex_lock(&telem_mutex);
            ack_telemetria_ok = 0;
            int rc;
            while (!ack_telemetria_ok) {
                rc = pthread_cond_timedwait(&telem_cond, &telem_mutex, &ts);
                if (rc == ETIMEDOUT) {
                    printf("[T2] Timeout esperando ACK_TELEMETRIA.\n");
                    break;
                }
            }

            if (ack_telemetria_ok) {
                printf("[T2] ACK_TELEMETRIA recebido com sucesso.\n");
                ack_ok = 1;
            }
            pthread_mutex_unlock(&telem_mutex);

            if (!ack_ok && tentativas < 3) {
                printf("[T2] Reenviando telemetria na próxima tentativa...\n");
            }
        }

        if (!ack_ok) {
            printf("[T2] Não foi possível obter ACK_TELEMETRIA após 3 tentativas. "
                   "Telemetria será reenviada no próximo ciclo.\n");
        }

        sleep(30);
    }

    return NULL;
}

// =========================================================
// Thread 4: Simulação dos drones
// =========================================================

void *thread_simulacao_drones(void *arg) {
    (void)arg;
    printf("[T4] Thread de simulação de drones iniciada.\n");

    while (1) {
        pthread_mutex_lock(&missao_mutex);
        while (!missao_atual.ativa) {
            pthread_cond_wait(&missao_cond, &missao_mutex);
        }

        int id_cidade = missao_atual.id_cidade;
        int id_equipe = missao_atual.id_equipe;
        missao_atual.ativa = 0;
        pthread_mutex_unlock(&missao_mutex);

        int duracao = (rand() % 30) + 1;
        printf("[T4] Iniciando missão: Equipe %d -> Cidade %d (%s). "
               "Duração simulada: %d segundos.\n",
               id_equipe,
               id_cidade,
               g->cidades[id_cidade].nome,
               duracao);

        sleep(duracao);

        printf("[T4] Missão concluída na cidade %d (%s) pela equipe %d.\n",
               id_cidade, g->cidades[id_cidade].nome, id_equipe);

        header_t hdr;
        payload_conclusao_t payload;
        hdr.tipo = MSG_CONCLUSAO;
        hdr.tamanho = sizeof(payload_conclusao_t);
        payload.id_cidade = id_cidade;
        payload.id_equipe = id_equipe;

        unsigned char buffer[MAX_BUFFER];
        memcpy(buffer, &hdr, sizeof(header_t));
        memcpy(buffer + sizeof(header_t), &payload, sizeof(payload_conclusao_t));
        ssize_t total = sizeof(header_t) + sizeof(payload_conclusao_t);

        printf("[T4] Enviando MSG_CONCLUSAO ao servidor...\n");
        ssize_t n = sendto(sockfd, buffer, total, 0,
                           (struct sockaddr *)&servaddr, servaddr_len);
        if (n < 0) {
            perror("[T4] Erro ao enviar MSG_CONCLUSAO");
            continue;
        }

        struct timespec ts;
        ts.tv_sec = time(NULL) + 5;  // agora + 5 segundos
        ts.tv_nsec = 0;

        pthread_mutex_lock(&concl_mutex);
        ack_conclusao_ok = 0;
        int rc;
        while (!ack_conclusao_ok) {
            rc = pthread_cond_timedwait(&concl_cond, &concl_mutex, &ts);
            if (rc == ETIMEDOUT) {
                printf("[T4] Timeout esperando ACK_CONCLUSAO.\n");
                break;
            }
        }
        if (ack_conclusao_ok) {
            printf("[T4] ACK_CONCLUSAO recebido. Missão encerrada com sucesso.\n");
        }
        pthread_mutex_unlock(&concl_mutex);
    }

    return NULL;
}

// =========================================================
// Thread 3: Recebe todas as mensagens do servidor
// =========================================================

void *thread_receptor(void *arg) {
    (void)arg;
    printf("[T3] Thread de recepção iniciada.\n");

    while (1) {
        unsigned char buffer[MAX_BUFFER];
        struct sockaddr_storage from;
        socklen_t from_len = sizeof(from);

        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            perror("[T3] Erro em recvfrom");
            continue;
        }

        if (n < (ssize_t)sizeof(header_t)) {
            fprintf(stderr, "[T3] Pacote muito pequeno (%zd bytes)\n", n);
            continue;
        }

        header_t hdr;
        memcpy(&hdr, buffer, sizeof(header_t));
        unsigned char *payload_ptr = buffer + sizeof(header_t);
        ssize_t payload_len = n - sizeof(header_t);

        printf("[T3] Pacote recebido. tipo=%d, tamanho=%d, bytes=%zd\n",
               hdr.tipo, hdr.tamanho, n);

        if (hdr.tipo == MSG_EQUIPE_DRONE) {
            if (payload_len < (ssize_t)sizeof(payload_equipe_drone_t)) {
                fprintf(stderr, "[T3] Payload de MSG_EQUIPE_DRONE com tamanho inesperado (%zd)\n", payload_len);
                continue;
            }

            payload_equipe_drone_t payload;
            memcpy(&payload, payload_ptr, sizeof(payload_equipe_drone_t));

            int id_cidade = payload.id_cidade;
            int id_equipe = payload.id_equipe;

            printf("[T3] MSG_EQUIPE_DRONE recebida: Equipe %d -> Cidade %d (%s)\n",
                   id_equipe,
                   id_cidade,
                   g->cidades[id_cidade].nome);

            enviar_ack(ACK_EQ_EQUIPE);

            pthread_mutex_lock(&missao_mutex);
            if (missao_atual.ativa) {
                printf("[T3] Já existe missão ativa. Substituindo pela nova.\n");
            }
            missao_atual.ativa = 1;
            missao_atual.id_cidade = id_cidade;
            missao_atual.id_equipe = id_equipe;
            pthread_cond_signal(&missao_cond);
            pthread_mutex_unlock(&missao_mutex);

        } else if (hdr.tipo == MSG_ACK) {
            if (payload_len < (ssize_t)sizeof(payload_ack_t)) {
                fprintf(stderr, "[T3] Payload de MSG_ACK com tamanho inesperado (%zd)\n", payload_len);
                continue;
            }
            payload_ack_t payload;
            memcpy(&payload, payload_ptr, sizeof(payload_ack_t));

            if (payload.status == ACK_TELEMETRIA) {
                printf("[T3] ACK_TELEMETRIA recebido.\n");
                pthread_mutex_lock(&telem_mutex);
                ack_telemetria_ok = 1;
                pthread_cond_signal(&telem_cond);
                pthread_mutex_unlock(&telem_mutex);
            } else if (payload.status == ACK_EQ_EQUIPE) {
                printf("[T3] ACK_EQ_EQUIPE recebido do servidor.\n");
            } else if (payload.status == ACK_CONCLUSAO) {
                printf("[T3] ACK_CONCLUSAO recebido do servidor.\n");
                pthread_mutex_lock(&concl_mutex);
                ack_conclusao_ok = 1;
                pthread_cond_signal(&concl_cond);
                pthread_mutex_unlock(&concl_mutex);
            } else {
                printf("[T3] ACK com status desconhecido: %d\n", payload.status);
            }

        } else {
            printf("[T3] Tipo de mensagem inesperado: %d (ignorado)\n", hdr.tipo);
        }
    }

    return NULL;
}

// =========================================================
// main()
// =========================================================

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
                "Uso: %s <v4|v6> [arquivo_grafo]\n"
                "Exemplos:\n"
                "  %s v4\n"
                "  %s v6 grafo_amazonia_legal.txt\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "v6") == 0) {
        usar_ipv6 = 1;
    } else if (strcmp(argv[1], "v4") == 0) {
        usar_ipv6 = 0;
    } else {
        fprintf(stderr, "Primeiro argumento deve ser 'v4' ou 'v6'\n");
        return 1;
    }

    const char *arquivo_grafo = "grafo_amazonia_legal.txt";
    if (argc >= 3) {
        arquivo_grafo = argv[2];
    }

    srand((unsigned int)time(NULL));

    printf("[MAIN] Carregando grafo a partir de '%s'...\n", arquivo_grafo);
    g = grafo_carregar(arquivo_grafo);
    if (!g) {
        fprintf(stderr, "[MAIN] Falha ao carregar grafo.\n");
        return 1;
    }
    printf("[MAIN] Grafo carregado. Numero de cidades: %d\n", g->num_vertices);

    status_cidade = (int *)calloc(g->num_vertices, sizeof(int));
    if (!status_cidade) {
        fprintf(stderr, "[MAIN] Falha ao alocar vetor de status.\n");
        grafo_destruir(g);
        return 1;
    }

    sockfd = socket(usar_ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[MAIN] Erro ao criar socket");
        free(status_cidade);
        grafo_destruir(g);
        return 1;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr_len = usar_ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

    if (usar_ipv6) {
        struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&servaddr;
        sa6->sin6_family = AF_INET6;
        sa6->sin6_port = htons(SERVER_PORT);
        if (inet_pton(AF_INET6, "::1", &sa6->sin6_addr) <= 0) {
            fprintf(stderr, "[MAIN] Falha em inet_pton para ::1\n");
            close(sockfd);
            free(status_cidade);
            grafo_destruir(g);
            return 1;
        }
        printf("[MAIN] Cliente configurado para servidor IPv6 em ::1:%d\n", SERVER_PORT);
    } else {
        struct sockaddr_in *sa4 = (struct sockaddr_in *)&servaddr;
        sa4->sin_family = AF_INET;
        sa4->sin_port = htons(SERVER_PORT);
        if (inet_pton(AF_INET, "127.0.0.1", &sa4->sin_addr) <= 0) {
            fprintf(stderr, "[MAIN] Falha em inet_pton para 127.0.0.1\n");
            close(sockfd);
            free(status_cidade);
            grafo_destruir(g);
            return 1;
        }
        printf("[MAIN] Cliente configurado para servidor IPv4 em 127.0.0.1:%d\n", SERVER_PORT);
    }

    pthread_t t1, t2, t3, t4;

    if (pthread_create(&t1, NULL, thread_monitoramento, NULL) != 0) {
        perror("[MAIN] Erro ao criar thread 1");
        close(sockfd);
        free(status_cidade);
        grafo_destruir(g);
        return 1;
    }

    if (pthread_create(&t2, NULL, thread_telemetria, NULL) != 0) {
        perror("[MAIN] Erro ao criar thread 2");
        close(sockfd);
        free(status_cidade);
        grafo_destruir(g);
        return 1;
    }

    if (pthread_create(&t3, NULL, thread_receptor, NULL) != 0) {
        perror("[MAIN] Erro ao criar thread 3");
        close(sockfd);
        free(status_cidade);
        grafo_destruir(g);
        return 1;
    }

    if (pthread_create(&t4, NULL, thread_simulacao_drones, NULL) != 0) {
        perror("[MAIN] Erro ao criar thread 4");
        close(sockfd);
        free(status_cidade);
        grafo_destruir(g);
        return 1;
    }

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    pthread_join(t4, NULL);

    close(sockfd);
    free(status_cidade);
    grafo_destruir(g);

    return 0;
}
