// server.c
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

#include "grafo.h"
#include "protocol.h"

#define SERVER_PORT 8080
#define MAX_BUFFER 1024
#define MAX_ALERTAS 256

typedef struct {
    int ativo;
    int id_cidade;
    time_t timestamp;
    int equipe_id;       // -1 enquanto nenhuma equipe designada
    int equipe_atuando;  // 0 = não, 1 = sim (após ACK_EQ_EQUIPE)
} alerta_t;

// Lista de alertas ativos
static alerta_t alertas[MAX_ALERTAS];

// Equipes de drones (indexadas pelo ID da capital no grafo)
static int *equipe_livre = NULL; // vetor [num_vertices], 1 = livre, 0 = ocupada

// Fila simples de ordens de drone pendentes de ACK
#define MAX_PENDENTES 256
static int fila_pendentes[MAX_PENDENTES];
static int pend_ini = 0;
static int pend_fim = 0;

static void fila_pend_push(int idx_alerta) {
    int prox = (pend_fim + 1) % MAX_PENDENTES;
    if (prox == pend_ini) {
        fprintf(stderr, "Fila de pendentes cheia, descartando alerta %d\n", idx_alerta);
        return;
    }
    fila_pendentes[pend_fim] = idx_alerta;
    pend_fim = prox;
}

static int fila_pend_pop(void) {
    if (pend_ini == pend_fim) {
        return -1; // vazia
    }
    int idx = fila_pendentes[pend_ini];
    pend_ini = (pend_ini + 1) % MAX_PENDENTES;
    return idx;
}

// Procura alerta ativo para uma cidade
static int encontrar_alerta_por_cidade(int id_cidade) {
    for (int i = 0; i < MAX_ALERTAS; i++) {
        if (alertas[i].ativo && alertas[i].id_cidade == id_cidade) {
            return i;
        }
    }
    return -1;
}

// Cria novo alerta
static int criar_alerta(int id_cidade) {
    for (int i = 0; i < MAX_ALERTAS; i++) {
        if (!alertas[i].ativo) {
            alertas[i].ativo = 1;
            alertas[i].id_cidade = id_cidade;
            alertas[i].timestamp = time(NULL);
            alertas[i].equipe_id = -1;
            alertas[i].equipe_atuando = 0;
            return i;
        }
    }
    fprintf(stderr, "Limite de alertas atingido, não foi possível registrar novo alerta\n");
    return -1;
}

// Seleciona equipe livre mais próxima com base nas distâncias de Dijkstra
static int selecionar_equipe(const grafo_t *g, const int *dist, const int *equipe_livre_local, int *dist_escolhida) {
    int melhor_capital = -1;
    int melhor_dist = 0;

    for (int i = 0; i < g->num_vertices; i++) {
        if (g->cidades[i].tipo == 1 && equipe_livre_local[i] == 1) {
            int d = dist[i];
            if (d > 100000000) {
                continue; // inacessível
            }
            if (melhor_capital == -1 || d < melhor_dist) {
                melhor_capital = i;
                melhor_dist = d;
            }
        }
    }

    if (melhor_capital >= 0 && dist_escolhida) {
        *dist_escolhida = melhor_dist;
    }
    return melhor_capital;
}

static void enviar_ack(int sockfd, struct sockaddr *cliaddr, socklen_t cli_len, int status_ack) {
    header_t hdr;
    payload_ack_t payload;

    hdr.tipo = MSG_ACK;
    hdr.tamanho = sizeof(payload_ack_t);

    payload.status = status_ack;

    unsigned char buffer[MAX_BUFFER];
    memcpy(buffer, &hdr, sizeof(header_t));
    memcpy(buffer + sizeof(header_t), &payload, sizeof(payload_ack_t));

    ssize_t total = sizeof(header_t) + sizeof(payload_ack_t);
    ssize_t n = sendto(sockfd, buffer, total, 0, cliaddr, cli_len);
    if (n < 0) {
        perror("Erro ao enviar ACK");
    } else {
        printf("-> ACK enviado (tipo=%d)\n", status_ack);
    }
}

// Envia ordem de equipe de drones
static void enviar_ordem_drone(int sockfd,
                               struct sockaddr *cliaddr,
                               socklen_t cli_len,
                               const grafo_t *g,
                               int id_cidade_alerta,
                               int id_equipe) {
    header_t hdr;
    payload_equipe_drone_t payload;

    hdr.tipo = MSG_EQUIPE_DRONE;
    hdr.tamanho = sizeof(payload_equipe_drone_t);

    payload.id_cidade = id_cidade_alerta;
    payload.id_equipe = id_equipe;

    unsigned char buffer[MAX_BUFFER];
    memcpy(buffer, &hdr, sizeof(header_t));
    memcpy(buffer + sizeof(header_t), &payload, sizeof(payload_equipe_drone_t));

    ssize_t total = sizeof(header_t) + sizeof(payload_equipe_drone_t);
    ssize_t n = sendto(sockfd, buffer, total, 0, cliaddr, cli_len);
    if (n < 0) {
        perror("Erro ao enviar MSG_EQUIPE_DRONE");
        return;
    }

    printf("-> Ordem enviada: Equipe %s (ID=%d) -> Cidade %s (ID=%d)\n",
           g->cidades[id_equipe].nome, id_equipe,
           g->cidades[id_cidade_alerta].nome, id_cidade_alerta);
}

// Processa mensagem de telemetria
static void processar_telemetria(int sockfd,
                                 struct sockaddr *cliaddr,
                                 socklen_t cli_len,
                                 const grafo_t *g,
                                 const payload_telemetria_t *payload) {
    printf("[TELEMETRIA RECEBIDA]\n");
    printf("Total de cidades monitoradas: %d\n", payload->total);

    int *status_cidade = (int *)calloc(g->num_vertices, sizeof(int));
    if (!status_cidade) {
        fprintf(stderr, "Falha ao alocar vetor de status\n");
        return;
    }

    for (int i = 0; i < payload->total; i++) {
        int id = payload->dados[i].id_cidade;
        int st = payload->dados[i].status;
        if (id < 0 || id >= g->num_vertices) {
            continue;
        }
        status_cidade[id] = st;
        if (st == STATUS_ALERTA) {
            printf("ALERTA: %s (ID=%d)\n", g->cidades[id].nome, id);
        }
    }

    // Envia ACK de telemetria
    enviar_ack(sockfd, cliaddr, cli_len, ACK_TELEMETRIA);

    // Para cada cidade em alerta, cria alerta (se ainda não existir) e despacha drone
    printf("\n[DESPACHANDO DRONES]\n");
    for (int id = 0; id < g->num_vertices; id++) {
        if (status_cidade[id] == STATUS_ALERTA) {
            int idx_alerta = encontrar_alerta_por_cidade(id);
            if (idx_alerta == -1) {
                idx_alerta = criar_alerta(id);
            }

            if (idx_alerta == -1) {
                continue;
            }

            printf("Cidade em alerta: %s (ID=%d)\n", g->cidades[id].nome, id);

            int *dist = grafo_dijkstra(g, id);
            if (!dist) {
                fprintf(stderr, "Falha em Dijkstra para cidade %d\n", id);
                continue;
            }

            int dist_escolhida = 0;
            int id_capital = selecionar_equipe(g, dist, equipe_livre, &dist_escolhida);
            if (id_capital == -1) {
                printf("-> Nenhuma equipe livre disponível para esta cidade.\n");
                free(dist);
                continue;
            }

            printf("-> Dijkstra: capital %s (ID=%d) selecionada, distancia=%d km\n",
                   g->cidades[id_capital].nome, id_capital, dist_escolhida);

            enviar_ordem_drone(sockfd, cliaddr, cli_len, g, id, id_capital);

            alertas[idx_alerta].equipe_id = id_capital;
            alertas[idx_alerta].equipe_atuando = 0;
            fila_pend_push(idx_alerta);

            free(dist);
        }
    }

    free(status_cidade);
}

// Processa ACK recebido do cliente
static void processar_ack(const grafo_t *g, const payload_ack_t *payload) {
    if (payload->status == ACK_TELEMETRIA) {
        printf("[ACK RECEBIDO] Cliente confirmou recebimento de telemetria\n");
    } else if (payload->status == ACK_EQ_EQUIPE) {
        int idx_alerta = fila_pend_pop();
        if (idx_alerta == -1) {
            printf("[ACK RECEBIDO] ACK_EQ_EQUIPE sem alerta pendente\n");
            return;
        }
        alerta_t *a = &alertas[idx_alerta];
        a->equipe_atuando = 1;
        if (a->equipe_id >= 0) {
            equipe_livre[a->equipe_id] = 0;
            printf("[ACK RECEBIDO]\n");
            printf("Cliente confirmou recebimento de ordem de drone para %s (ID=%d)\n",
                   g->cidades[a->id_cidade].nome, a->id_cidade);
        } else {
            printf("[ACK RECEBIDO] Alerta sem equipe associada\n");
        }
    } else if (payload->status == ACK_CONCLUSAO) {
        printf("[ACK RECEBIDO] Cliente confirmou recebimento do ACK de conclusão de missão\n");
    } else {
        printf("[ACK RECEBIDO] Código de ACK desconhecido: %d\n", payload->status);
    }
}

// Processa conclusão de missão
static void processar_conclusao(int sockfd,
                                struct sockaddr *cliaddr,
                                socklen_t cli_len,
                                const grafo_t *g,
                                const payload_conclusao_t *payload) {
    int id_cidade = payload->id_cidade;
    int id_equipe = payload->id_equipe;

    printf("[MISSAO CONCLUIDA]\n");
    if (id_cidade >= 0 && id_cidade < g->num_vertices) {
        printf("Cidade atendida: %s (ID=%d)\n", g->cidades[id_cidade].nome, id_cidade);
    } else {
        printf("Cidade atendida: ID=%d\n", id_cidade);
    }

    if (id_equipe >= 0 && id_equipe < g->num_vertices) {
        printf("Equipe: %s (ID=%d)\n", g->cidades[id_equipe].nome, id_equipe);
    } else {
        printf("Equipe: ID=%d\n", id_equipe);
    }

    if (id_equipe >= 0 && id_equipe < g->num_vertices) {
        equipe_livre[id_equipe] = 1;
        printf("-> Equipe %s liberada para novas missoes\n",
               g->cidades[id_equipe].nome);
    }

    int idx_alerta = encontrar_alerta_por_cidade(id_cidade);
    if (idx_alerta != -1) {
        alertas[idx_alerta].ativo = 0;
    }

    enviar_ack(sockfd, cliaddr, cli_len, ACK_CONCLUSAO);
}

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

    int usar_ipv6 = 0;
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

    printf("Carregando grafo a partir de '%s'...\n", arquivo_grafo);
    grafo_t *g = grafo_carregar(arquivo_grafo);
    if (!g) {
        fprintf(stderr, "Falha ao carregar grafo. Encerrando servidor.\n");
        return 1;
    }

    equipe_livre = (int *)calloc(g->num_vertices, sizeof(int));
    if (!equipe_livre) {
        fprintf(stderr, "Falha ao alocar vetor de equipes\n");
        grafo_destruir(g);
        return 1;
    }

    for (int i = 0; i < g->num_vertices; i++) {
        if (g->cidades[i].tipo == 1) {
            equipe_livre[i] = 1;
        } else {
            equipe_livre[i] = 0;
        }
    }

    memset(alertas, 0, sizeof(alertas));

    int sockfd = socket(usar_ipv6 ? AF_INET6 : AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        free(equipe_livre);
        grafo_destruir(g);
        return 1;
    }

    if (usar_ipv6) {
        struct sockaddr_in6 serv6;
        memset(&serv6, 0, sizeof(serv6));
        serv6.sin6_family = AF_INET6;
        serv6.sin6_addr = in6addr_any;
        serv6.sin6_port = htons(SERVER_PORT);

        int yes = 1;
        setsockopt(sockfd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));

        if (bind(sockfd, (struct sockaddr *)&serv6, sizeof(serv6)) < 0) {
            perror("bind (IPv6)");
            close(sockfd);
            free(equipe_livre);
            grafo_destruir(g);
            return 1;
        }
        printf("Servidor IPv6 escutando na porta %d (::)\n\n", SERVER_PORT);
    } else {
        struct sockaddr_in serv4;
        memset(&serv4, 0, sizeof(serv4));
        serv4.sin_family = AF_INET;
        serv4.sin_addr.s_addr = htonl(INADDR_ANY);
        serv4.sin_port = htons(SERVER_PORT);

        if (bind(sockfd, (struct sockaddr *)&serv4, sizeof(serv4)) < 0) {
            perror("bind (IPv4)");
            close(sockfd);
            free(equipe_livre);
            grafo_destruir(g);
            return 1;
        }
        printf("Servidor IPv4 escutando na porta %d (0.0.0.0)\n\n", SERVER_PORT);
    }

    while (1) {
        unsigned char buffer[MAX_BUFFER];
        struct sockaddr_storage cliaddr;
        socklen_t cli_len = sizeof(cliaddr);

        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&cliaddr, &cli_len);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }

        if (n < (ssize_t)sizeof(header_t)) {
            fprintf(stderr, "Pacote muito pequeno recebido (%zd bytes)\n", n);
            continue;
        }

        header_t hdr;
        memcpy(&hdr, buffer, sizeof(header_t));

        unsigned char *payload_ptr = buffer + sizeof(header_t);
        ssize_t payload_len = n - sizeof(header_t);

        printf("\n[PACOTE RECEBIDO] tipo=%d, tamanho=%d, bytes=%zd\n",
               hdr.tipo, hdr.tamanho, n);

        if (hdr.tipo == MSG_TELEMETRIA) {
            if (payload_len < (ssize_t)sizeof(payload_telemetria_t)) {
                fprintf(stderr, "Payload de telemetria com tamanho inesperado (%zd)\n", payload_len);
                continue;
            }
            payload_telemetria_t payload;
            memcpy(&payload, payload_ptr, sizeof(payload_telemetria_t));
            processar_telemetria(sockfd,
                                 (struct sockaddr *)&cliaddr,
                                 cli_len,
                                 g,
                                 &payload);
        } else if (hdr.tipo == MSG_ACK) {
            if (payload_len < (ssize_t)sizeof(payload_ack_t)) {
                fprintf(stderr, "Payload de ACK com tamanho inesperado (%zd)\n", payload_len);
                continue;
            }
            payload_ack_t payload;
            memcpy(&payload, payload_ptr, sizeof(payload_ack_t));
            processar_ack(g, &payload);
        } else if (hdr.tipo == MSG_CONCLUSAO) {
            if (payload_len < (ssize_t)sizeof(payload_conclusao_t)) {
                fprintf(stderr, "Payload de CONCLUSAO com tamanho inesperado (%zd)\n", payload_len);
                continue;
            }
            payload_conclusao_t payload;
            memcpy(&payload, payload_ptr, sizeof(payload_conclusao_t));
            processar_conclusao(sockfd,
                                (struct sockaddr *)&cliaddr,
                                cli_len,
                                g,
                                &payload);
        } else if (hdr.tipo == MSG_EQUIPE_DRONE) {
            printf("MSG_EQUIPE_DRONE recebida no servidor (ignorada).\n");
        } else {
            printf("Tipo de mensagem desconhecido: %d\n", hdr.tipo);
        }
    }

    close(sockfd);
    free(equipe_livre);
    grafo_destruir(g);
    return 0;
}
