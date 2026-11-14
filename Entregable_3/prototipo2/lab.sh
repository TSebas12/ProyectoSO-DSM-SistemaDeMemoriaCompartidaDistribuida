#!/bin/sh
# lab.sh — Atajos para construir y probar el prototipo 2 del DSM.
#
# Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
# Curso    : IC-6600 Principios de Sistemas Operativos
# Entregable: 3 — Prototipo 2
#
# Uso rapido:
#   ./lab.sh build            compila dsm_node y dsmctl
#   ./lab.sh demo             demo de pagina compartida (2 nodos)
#   ./lab.sh matrices         multiplicacion de matrices (4 nodos)
#   ./lab.sh file             checksum distribuido (2 nodos)
#   ./lab.sh counter          incrementos concurrentes (2 nodos)
#   ./lab.sh bench            latencias local/remota (2 nodos)
#   ./lab.sh evict            presion de memoria (2 nodos)
#   ./lab.sh stress           matrices 192x192 + counter 100x2
#   ./lab.sh serve N          deja el nodo N abierto (sin aplicacion)
#   ./lab.sh ctl N CMD        consulta el nodo N (stats/pages/dir/...)
#   ./lab.sh ip N DIRECCION   cambia la IP del nodo N en nodes.conf
#   ./lab.sh show             muestra nodes.conf y procesos vivos
#   ./lab.sh stop             detiene todos los nodos
#
# Es POSIX sh a proposito: corre igual en Ubuntu, Debian y Alpine.

set -u
cd "$(dirname "$0")" || exit 1

# Si un nodo falla, no queremos que un volcado de nucleo bloquee la prueba.
ulimit -c 0 2>/dev/null || true

BIN=./dsm_node
CTL=./dsmctl
NODES=config/nodes.conf
LOGS=logs
PIDS=$LOGS/pids
DEFAULT_NODES=4

mkdir -p "$LOGS" || exit 1

usage() {
    sed -n '8,22p' "$0" | sed 's/^# \{0,1\}//'
}

start_node() {
    node_id=$1
    shift
    nodes_file="${NODES_FILE:-$NODES}"
    "$BIN" --config "config/node$node_id.conf" --nodes "$nodes_file" "$@" \
        >"$LOGS/run-node$node_id.out" 2>&1 &
    echo $! >>"$PIDS"
}

wait_all() {
    status=0
    if [ ! -f "$PIDS" ]; then
        return 0
    fi
    while read -r pid; do
        if [ -n "$pid" ] && ! wait "$pid" 2>/dev/null; then
            status=1
        fi
    done <"$PIDS"
    return $status
}

show_outputs() {
    for f in "$LOGS"/run-node*.out; do
        [ -f "$f" ] || continue
        echo "----- $f"
        cat "$f"
    done
}

run_app() {
    app=$1
    shift
    count=${APP_NODES:-$DEFAULT_NODES}
    : >"$PIDS"
    i=0
    while [ "$i" -lt "$count" ]; do
        start_node "$i" --app "$app" "$@"
        i=$((i + 1))
    done
    if wait_all; then
        result=OK
    else
        result=FALLO
    fi
    show_outputs
    echo "==> $app: $result"
    [ "$result" = OK ] || return 1
    return 0
}

cmd=${1:-}
[ -n "$cmd" ] || { usage; exit 2; }
shift 2>/dev/null || true

case "$cmd" in
build)
    make clean >/dev/null 2>&1
    make || exit 1
    ;;
demo)
    # 2 nodos: la barrera cuenta los nodos de nodes_2.conf y el demo es corto.
    APP_NODES=2 NODES_FILE=config/nodes_2.conf run_app demo
    ;;
matrices)
    # 4 nodos: reparte filas entre todos y ejercita el directorio distribuido.
    run_app matrices "$@"
    ;;
file)
    APP_NODES=2 NODES_FILE=config/nodes_2.conf run_app file "$@"
    ;;
counter)
    APP_NODES=2 NODES_FILE=config/nodes_2.conf run_app counter "$@"
    ;;
bench)
    APP_NODES=2 NODES_FILE=config/nodes_2.conf run_app bench "$@"
    ;;
evict)
    # 2 nodos y 64 paginas: con 4 nodos la presion de memoria provoca
    # timeouts de red intermitentes (ver limitaciones en el entregable).
    APP_NODES=2 NODES_FILE=config/nodes_2.conf run_app evict --pages 64 --frames 32 "$@"
    ;;
stress)
    run_app matrices --size 192 || exit 1
    APP_NODES=2 NODES_FILE=config/nodes_2.conf run_app counter --iters 100 --threads 2
    ;;
serve)
    node_id=${1:-0}
    echo "nodo $node_id abierto; Ctrl+C para detener"
    exec "$BIN" --config "config/node$node_id.conf" --nodes "$NODES"
    ;;
ctl)
    node_id=${1:-0}
    shift 2>/dev/null || true
    if [ $# -eq 0 ]; then
        set -- stats
    fi
    "$CTL" --config "config/node$node_id.conf" "$@"
    ;;
ip)
    node_id=${1:-}
    addr=${2:-}
    if [ -z "$node_id" ] || [ -z "$addr" ]; then
        echo "uso: ./lab.sh ip N DIRECCION"
        exit 2
    fi
    # Se actualizan las dos tablas para que las apps de 2 nodos y las de 4
    # nodos apunten a la misma maquina.
    for file in "$NODES" config/nodes_2.conf; do
        [ -f "$file" ] || continue
        awk -v id="$node_id" -v ip="$addr" '
            /^[[:space:]]*#/ { print; next }
            NF == 0          { print; next }
            $1 == id         { $2 = ip; print; next }
            { print }
        ' "$file" >"$file.tmp" && mv "$file.tmp" "$file" || exit 1
    done
    echo "nodo $node_id -> $addr"
    echo "----- $NODES"
    cat "$NODES"
    if [ -f config/nodes_2.conf ]; then
        echo "----- config/nodes_2.conf"
        cat config/nodes_2.conf
    fi
    ;;
show)
    echo "----- $NODES"
    cat "$NODES"
    if [ -f config/nodes_2.conf ]; then
        echo "----- config/nodes_2.conf"
        cat config/nodes_2.conf
    fi
    echo "----- procesos"
    ps -eo pid,args 2>/dev/null | grep '[d]sm_node' || echo "(ninguno)"
    ;;
stop)
    if command -v pkill >/dev/null 2>&1; then
        pkill -f 'dsm_node --config' 2>/dev/null || true
    fi
    if [ -f "$PIDS" ]; then
        while read -r pid; do
            [ -n "$pid" ] && kill "$pid" 2>/dev/null || true
        done <"$PIDS"
        : >"$PIDS"
    fi
    echo "nodos detenidos"
    ;;
help | -h | --help)
    usage
    ;;
*)
    echo "subcomando desconocido: $cmd"
    usage
    exit 2
    ;;
esac
