// protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

//
// TIPOS DE MENSAGEM (campo header_t.tipo)
//
#define MSG_TELEMETRIA   1  // Cliente -> Servidor
#define MSG_ACK          2  // Cliente <-> Servidor
#define MSG_EQUIPE_DRONE 3  // Servidor -> Cliente
#define MSG_CONCLUSAO    4  // Cliente -> Servidor

//
// STATUS DE TELEMETRIA
//
#define STATUS_OK        0
#define STATUS_ALERTA    1

//
// TIPOS DE ACK (payload_ack_t.status)
//
#define ACK_TELEMETRIA   0
#define ACK_EQ_EQUIPE    1
#define ACK_CONCLUSAO    2

//
// Cabeçalho comum a TODAS as mensagens
// (definido no enunciado)
//
typedef struct {
    // Tipo da mensagem
    // (1 = telemetria, 2 = ACK, 3 = equipe de drones, 4 = conclusao)
    uint16_t tipo;

    // Tamanho do payload em bytes
    uint16_t tamanho;
} header_t;

//
// 1) Cliente -> Servidor: Telemetria
//

// Um item de telemetria: cidade + status (OK/ALERTA)
typedef struct {
    int id_cidade;  // identificador do vértice
    int status;     // 0 = OK, 1 = ALERTA
} telemetria_t;

// Payload de telemetria: lista de cidades monitoradas
typedef struct {
    int total;               // número de cidades monitoradas
    telemetria_t dados[50];  // lista de (id_cidade, status)
} payload_telemetria_t;

//
// 2) Cliente <-> Servidor: ACK
//
typedef struct {
    int status;  // 0 = ACK TELEMETRIA, 1 = ACK EQUIPE DRONE, 2 = ACK CONCLUSAO
} payload_ack_t;

//
// 3) Servidor -> Cliente: Ordem de envio de drone
//
typedef struct {
    int id_cidade;  // cidade onde o alerta foi detectado
    int id_equipe;  // equipe de drones designada
} payload_equipe_drone_t;

//
// 4) Cliente -> Servidor: Conclusão de missão
//
typedef struct {
    int id_cidade;  // cidade atendida
    int id_equipe;  // equipe que atuou
} payload_conclusao_t;

#endif // PROTOCOL_H
