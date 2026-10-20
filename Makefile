# Compilador e flags
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -g
# Nomes dos binários
PROGS   = server client

all: $(PROGS)
	rm -rf *.o

# =========================
# Regras de linkedição
# =========================

server: server.o grafo.o
	$(CC) $(CFLAGS) $^ -o $@

client: client.o grafo.o
	$(CC) $(CFLAGS) $^ -o $@ -pthread

# =========================
# Regras de compilação
# =========================

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# =========================
# Dependências de headers
# =========================

server.o: server.c grafo.h protocol.h
client.o: client.c grafo.h protocol.h
grafo.o: grafo.c grafo.h

# =========================
# Limpeza
# =========================

clean:
	rm -rf $(BINDIR)
	rm -rf $(PROGS)

.PHONY: all clean
