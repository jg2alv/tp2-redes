# Deforestation Patrol: UDP alert coordination and simulated drone dispatch in C

Deforestation Patrol connects simulated environmental alerts with graph-based drone dispatch. A C client models monitoring and missions, while a UDP server coordinates responses using shortest-path distances and the availability of drone teams in Brazil's Legal Amazon.

## 1. Academic context and scope

- **Institution:** Universidade Federal de Minas Gerais (UFMG)
- **Degree:** Ciência da Computação
- **Course:** Redes de Computadores
- **Assignment:** Trabalho Prático 2
- **Semester:** 2025-02

The project combines IPv4/IPv6 UDP communication, weighted graphs, Dijkstra's algorithm, POSIX threads, and shared-state synchronization. It simulates sensors and drone missions; it does not communicate with physical environmental sensors or aircraft.

## 2. How to run

The Makefile builds C11 programs with GCC and links the client with POSIX threads. From the repository root:

```sh
make
```

In separate terminals, start the server and then the client:

```sh
./server v4 grafo_amazonia_legal.txt
```

```sh
./client v4 grafo_amazonia_legal.txt
```

Use `v6` in both commands for IPv6. Both programs accept an optional graph path and default to the bundled filename. The port is fixed at `8080`; the client uses loopback, `127.0.0.1` or `::1`. The simulation runs continuously.

## 3. Usage example

The repository's `grafo_amazonia_legal.txt` is the supplied simulation instance. Its header declares 45 vertices and 126 edges; subsequent records identify cities and which locations host teams, followed by weighted routes. The launch commands above load this existing fixture on both endpoints.

The client generates simulated alerts, reports telemetry, receives dispatch orders, and models mission completion. The server selects available teams using graph distances. Alert generation is time-seeded, so no fixed event sequence or exact runtime transcript is promised.

## 4. Implementation decisions

The shared `protocol.h` defines binary headers and payloads for telemetry, acknowledgments, dispatch orders, and completion notices. The server computes shortest-path distances from an alert location and chooses among available team locations, maintaining alert and team state through the mission lifecycle.

Four client threads separate monitoring, telemetry, dispatch reception, and mission simulation. Mutexes and condition variables coordinate city status, acknowledgments, and the current mission. Keeping these responsibilities separate allows communication and simulation to progress through independent execution flows.

## 5. Learning and limitations

The work connected protocol design with concurrent state transitions and graph-based decisions. A dispatch is meaningful only when both endpoints agree on its acknowledgment and completion state.

The current client targets a local server and has a single shared current-mission structure. Binary payloads are copied from native C structures, so cross-platform serialization requires additional design. Randomized simulation and the absence of an automated integration suite limit reproducible evaluation; no packet-loss, concurrency, or performance results are established here.
