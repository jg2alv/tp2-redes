// grafo.c
#include "grafo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static vizinho_t *cria_vizinho(int id_destino, int peso, vizinho_t *prox) {
    vizinho_t *v = (vizinho_t *)malloc(sizeof(vizinho_t));
    if (!v) return NULL;
    v->id_destino = id_destino;
    v->peso = peso;
    v->prox = prox;
    return v;
}

grafo_t *grafo_carregar(const char *caminho_arquivo) {
    FILE *f = fopen(caminho_arquivo, "r");
    if (!f) {
        perror("Erro ao abrir arquivo de grafo");
        return NULL;
    }

    int N, M;
    if (fscanf(f, "%d %d", &N, &M) != 2) {
        fprintf(stderr, "Formato inválido na linha 1 do arquivo de grafo\n");
        fclose(f);
        return NULL;
    }

    grafo_t *g = (grafo_t *)malloc(sizeof(grafo_t));
    if (!g) {
        fclose(f);
        return NULL;
    }

    g->num_vertices = N;
    g->num_arestas = M;

    g->cidades = (cidade_t *)calloc(N, sizeof(cidade_t));
    g->adj = (vizinho_t **)calloc(N, sizeof(vizinho_t *));
    if (!g->cidades || !g->adj) {
        fprintf(stderr, "Erro de alocacao ao criar estruturas do grafo\n");
        fclose(f);
        free(g->cidades);
        free(g->adj);
        free(g);
        return NULL;
    }

    // Linhas 2..N+1: i nome tipo
    for (int k = 0; k < N; k++) {
        int id, tipo;
        char nome[64];

        if (fscanf(f, "%d %63s %d", &id, nome, &tipo) != 3) {
            fprintf(stderr, "Erro ao ler vértice %d\n", k);
            grafo_destruir(g);
            fclose(f);
            return NULL;
        }

        if (id < 0 || id >= N) {
            fprintf(stderr, "ID de vértice inválido: %d\n", id);
            grafo_destruir(g);
            fclose(f);
            return NULL;
        }

        g->cidades[id].id = id;
        strncpy(g->cidades[id].nome, nome, sizeof(g->cidades[id].nome));
        g->cidades[id].nome[sizeof(g->cidades[id].nome) - 1] = '\0';
        g->cidades[id].tipo = tipo;
    }

    // Próximas M linhas: u v p
    for (int k = 0; k < M; k++) {
        int u, v, p;
        if (fscanf(f, "%d %d %d", &u, &v, &p) != 3) {
            fprintf(stderr, "Erro ao ler aresta %d\n", k);
            grafo_destruir(g);
            fclose(f);
            return NULL;
        }

        if (u < 0 || u >= N || v < 0 || v >= N || p <= 0) {
            fprintf(stderr, "Aresta inválida: %d %d %d\n", u, v, p);
            grafo_destruir(g);
            fclose(f);
            return NULL;
        }

        // Grafo não-direcionado: adiciona nas duas listas
        g->adj[u] = cria_vizinho(v, p, g->adj[u]);
        g->adj[v] = cria_vizinho(u, p, g->adj[v]);

        if (!g->adj[u] || !g->adj[v]) {
            fprintf(stderr, "Erro de alocacao ao criar vizinho\n");
            grafo_destruir(g);
            fclose(f);
            return NULL;
        }
    }

    fclose(f);
    return g;
}

void grafo_destruir(grafo_t *g) {
    if (!g) return;

    if (g->adj) {
        for (int i = 0; i < g->num_vertices; i++) {
            vizinho_t *v = g->adj[i];
            while (v) {
                vizinho_t *tmp = v;
                v = v->prox;
                free(tmp);
            }
        }
        free(g->adj);
    }

    free(g->cidades);
    free(g);
}

int *grafo_dijkstra(const grafo_t *g, int origem) {
    if (!g || origem < 0 || origem >= g->num_vertices) {
        return NULL;
    }

    int N = g->num_vertices;
    int *dist = (int *)malloc(N * sizeof(int));
    int *visitado = (int *)calloc(N, sizeof(int));

    if (!dist || !visitado) {
        free(dist);
        free(visitado);
        return NULL;
    }

    // Inicializa distâncias
    for (int i = 0; i < N; i++) {
        dist[i] = INT_MAX / 2;  // evita overflow
    }
    dist[origem] = 0;

    // Dijkstra O(N^2) simples (suficiente para N=45)
    for (int i = 0; i < N; i++) {
        int u = -1;
        int menor = INT_MAX;

        // Escolhe vértice não visitado com menor dist
        for (int v = 0; v < N; v++) {
            if (!visitado[v] && dist[v] < menor) {
                menor = dist[v];
                u = v;
            }
        }

        if (u == -1) break;  // restantes são inacessíveis
        visitado[u] = 1;

        // Relaxa arestas de u
        for (vizinho_t *viz = g->adj[u]; viz != NULL; viz = viz->prox) {
            int v = viz->id_destino;
            int peso = viz->peso;

            if (!visitado[v] && dist[u] + peso < dist[v]) {
                dist[v] = dist[u] + peso;
            }
        }
    }

    free(visitado);
    return dist;
}
