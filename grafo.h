// grafo.h
#ifndef GRAFO_H
#define GRAFO_H

// Representa uma cidade da Amazônia Legal
typedef struct {
    int id;         // índice do vértice no grafo (0 .. N-1)
    char nome[64];  // nome da cidade
    int tipo;       // 0 = regional, 1 = capital
} cidade_t;

// Nó da lista de adjacência (vizinho)
typedef struct vizinho {
    int id_destino;         // id do vértice vizinho
    int peso;               // distância em km
    struct vizinho *prox;   // próximo vizinho
} vizinho_t;

// Estrutura principal do grafo
typedef struct {
    int num_vertices;   // N
    int num_arestas;    // M
    cidade_t *cidades;  // vetor de tamanho N
    vizinho_t **adj;    // vetor de listas de adjacência (tamanho N)
} grafo_t;

/**
 * Carrega o grafo a partir de um arquivo no formato:
 * Linha 1: N M
 * Linhas 2..N+1: i nome tipo
 * Próximas M linhas: u v p
 *
 * Retorna um ponteiro para grafo_t alocado dinamicamente,
 * ou NULL em caso de erro.
 */
grafo_t *grafo_carregar(const char *caminho_arquivo);

/**
 * Libera toda a memória associada ao grafo.
 */
void grafo_destruir(grafo_t *g);

/**
 * Executa o algoritmo de Dijkstra a partir de um vértice de origem.
 *
 * Retorna um vetor de inteiros com tamanho g->num_vertices,
 * onde dist[i] é a menor distância da origem até i.
 * - Em caso de erro, retorna NULL.
 * - O chamador é responsável por dar free(dist).
 */
int *grafo_dijkstra(const grafo_t *g, int origem);

#endif // GRAFO_H
