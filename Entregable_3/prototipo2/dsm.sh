#!/usr/bin/env bash
# dsm.sh — menú interactivo para configurar y probar el prototipo 2 del DSM.
#
# Uso:  ./dsm.sh
#
# Todo se hace desde este menú: IPs de las dos máquinas, rol de esta máquina
# (A = home / nodo 0, B = cliente / nodo 1), compilación, arranque, consultas,
# logs, parada y diagnóstico de red.

set -u
cd "$(dirname "$0")" || exit 1

STATE=".dsm_state"
NODES="config/nodes.conf"
NODES2="config/nodes_2.conf"

ROLE=""
IP_A=""
IP_B=""

# --- estado ---------------------------------------------------------------

load_state() {
    [ -f "$STATE" ] || return 0
    # shellcheck disable=SC1090
    . "$STATE"
}

save_state() {
    {
        printf 'ROLE=%s\n' "${ROLE:-}"
        printf 'IP_A=%s\n' "${IP_A:-}"
        printf 'IP_B=%s\n' "${IP_B:-}"
    } >"$STATE"
}

cfg_for_role() {
    case "${ROLE:-}" in
        A) echo "config/node0.conf" ;;
        B) echo "config/node1.conf" ;;
        *) return 1 ;;
    esac
}

port_for_role() {
    case "${ROLE:-}" in
        A) echo 27300 ;;
        B) echo 27304 ;;
        *) return 1 ;;
    esac
}

peer_ip() {
    case "${ROLE:-}" in
        A) echo "${IP_B:-}" ;;
        B) echo "${IP_A:-}" ;;
        *) echo "" ;;
    esac
}

valid_ip() {
    case "$1" in
        *[!0-9.]*|"") return 1 ;;
    esac
    printf '%s' "$1" | awk -F. '
        NF != 4 { exit 1 }
        { for (i = 1; i <= 4; i++) if ($i < 0 || $i > 255) exit 1 }
    '
}

# --- pasos ----------------------------------------------------------------

do_ips() {
    echo
    echo "IP de la máquina A (home, nodo 0):"
    read -r -p "  IP_A: " a
    echo "IP de la máquina B (cliente, nodo 1):"
    read -r -p "  IP_B: " b

    if ! valid_ip "$a" || ! valid_ip "$b"; then
        echo "ERROR: alguna IP no es válida."
        return 1
    fi
    if [ "$a" = "$b" ]; then
        echo "ERROR: A y B no pueden tener la misma IP."
        return 1
    fi

    IP_A="$a"
    IP_B="$b"

    for f in "$NODES" "$NODES2"; do
        [ -f "$f" ] || continue
        awk -v id=0 -v ip="$IP_A" '
            /^[[:space:]]*#/ { print; next }
            NF == 0          { print; next }
            $1 == id         { $2 = ip; print; next }
            { print }
        ' "$f" >"$f.tmp" && mv "$f.tmp" "$f" || return 1

        awk -v id=1 -v ip="$IP_B" '
            /^[[:space:]]*#/ { print; next }
            NF == 0          { print; next }
            $1 == id         { $2 = ip; print; next }
            { print }
        ' "$f" >"$f.tmp" && mv "$f.tmp" "$f" || return 1
    done

    # Los nodos 1..3 apuntan al home (A).
    for f in config/node1.conf config/node2.conf config/node3.conf; do
        [ -f "$f" ] && sed -i "s|^seed_host.*|seed_host = $IP_A|" "$f"
    done

    save_state
    echo
    echo "Tablas actualizadas:"
    grep -v '^[[:space:]]*#' "$NODES2" | grep -v '^[[:space:]]*$'
    echo
    echo "seed_host de node1..3 = $IP_A"
}

do_role() {
    echo
    echo "Rol de ESTA máquina:"
    echo "  A) home / nodo 0"
    echo "  B) cliente / nodo 1"
    read -r -p "  Opción [A/B]: " r
    case "$r" in
        A|a) ROLE="A" ;;
        B|b) ROLE="B" ;;
        *) echo "Opción inválida."; return 1 ;;
    esac
    save_state
    echo "Rol guardado: $ROLE (config $(cfg_for_role))"
}

do_build() {
    echo
    echo "Compilando dsm_node y dsmctl..."
    local hdr
    hdr="$(mktemp)" || return 1

    cat >"$hdr" <<'EOF'
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <signal.h>
#include <time.h>
#include <semaphore.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
EOF

    if gcc -std=c11 -D_GNU_SOURCE -Iinclude -O2 -g -include "$hdr" -o dsm_node \
        src/sync.c src/stats.c src/memory.c src/fault.c src/coherence.c \
        src/config.c src/protocol.c src/log.c src/nodes.c src/net.c \
        src/dsm_api.c src/runtime.c src/demo_page.c src/apps.c \
        src/app_common.c src/app_matrices.c src/app_file.c src/app_counter.c \
        src/app_bench.c src/app_evict.c src/control.c src/web.c src/heartbeat.c \
        src/node_main.c -lpthread -lm -lrt &&
        gcc -std=c11 -D_GNU_SOURCE -Iinclude -O2 -g -include "$hdr" -o dsmctl \
            src/ctl_main.c src/config.c -lpthread -lm; then
        rm -f "$hdr"
        echo "OK: binarios listos"
        ls -l dsm_node dsmctl
    else
        rm -f "$hdr"
        echo "ERROR: la compilación falló."
        return 1
    fi
}

kill_leftovers() {
    pkill -f 'dsm_node --config' 2>/dev/null || true
    sleep 1
}

check_port_free() {
    local port
    port="$(port_for_role)" || return 1
    if ss -ltn 2>/dev/null | grep -q ":$port "; then
        echo "ERROR: el puerto $port ya está ocupado."
        echo "       Cerrá el nodo anterior (opción 8) y reintentá."
        return 1
    fi
    return 0
}

do_start() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Definí el rol primero (opción 2)."; return 1; }
    [ -x ./dsm_node ] || { echo "Falta compilar (opción 3)."; return 1; }

    kill_leftovers
    check_port_free || return 1

    echo
    echo "Arrancando: ./dsm_node --config $cfg --nodes $NODES2"
    echo "Dejá esta terminal abierta. Ctrl+C para detener."
    echo
    ./dsm_node --config "$cfg" --nodes "$NODES2"
}

do_app() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Definí el rol primero (opción 2)."; return 1; }
    [ -x ./dsm_node ] || { echo "Falta compilar (opción 3)."; return 1; }

    echo
    echo "Aplicación:"
    echo "  1) demo"
    echo "  2) file (1 MB)"
    echo "  3) counter"
    echo "  4) bench"
    echo "  5) evict (64 páginas / 32 marcos)"
    echo "  6) matrices"
    read -r -p "  Opción: " opt

    local args=()
    case "$opt" in
        1) args=(--app demo) ;;
        2) args=(--app file) ;;
        3)
            read -r -p "  iters [100]: " it; it="${it:-100}"
            read -r -p "  threads [2]: " th; th="${th:-2}"
            args=(--app counter --iters "$it" --threads "$th")
            ;;
        4) args=(--app bench) ;;
        5) args=(--app evict --pages 64 --frames 32) ;;
        6)
            read -r -p "  tamaño [64]: " sz; sz="${sz:-64}"
            args=(--app matrices --size "$sz")
            ;;
        *) echo "Opción inválida."; return 1 ;;
    esac

    kill_leftovers
    check_port_free || return 1

    echo
    echo "Ejecutando en ESTA máquina:"
    echo "  ./dsm_node --config $cfg --nodes $NODES2 ${args[*]}"
    echo
    echo "Lanzá lo MISMO en la otra máquina AHORA (tenés menos de 2 minutos)."
    echo
    ./dsm_node --config "$cfg" --nodes "$NODES2" "${args[@]}"
}

do_status() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Definí el rol primero (opción 2)."; return 1; }

    echo
    echo "--- nodos ---"
    ./dsmctl --config "$cfg" nodes
    echo
    echo "--- estadísticas ---"
    ./dsmctl --config "$cfg" stats
    echo
    echo "--- páginas locales (primeras 15) ---"
    ./dsmctl --config "$cfg" pages | head -15
    echo
    echo "--- objetos ---"
    ./dsmctl --config "$cfg" objects

    if [ -n "${IP_A:-}" ]; then
        echo
        echo "--- directorio global en el home ($IP_A) ---"
        ./dsmctl --host "$IP_A" --port 27301 dir
    fi
}

do_log() {
    local cfg n
    cfg="$(cfg_for_role)" || { echo "Definí el rol primero (opción 2)."; return 1; }
    n="${cfg##*node}"; n="${n%%.conf}"
    echo
    echo "--- logs/dsm-node$n.log (últimas 40 líneas) ---"
    if [ -f "logs/dsm-node$n.log" ]; then
        tail -40 "logs/dsm-node$n.log"
    else
        echo "(todavía no existe)"
    fi
}

do_stop() {
    echo
    kill_leftovers
    echo "Nodos detenidos."
}

do_diag() {
    local port peer
    port="$(port_for_role)" || port=""
    peer="$(peer_ip)"

    echo
    echo "--- IPs locales ---"
    hostname -I 2>/dev/null || ip -4 addr show | grep inet
    echo
    echo "--- procesos dsm_node ---"
    pgrep -af dsm_node || echo "(ninguno)"
    echo
    echo "--- puertos 27300-27315 escuchando ---"
    ss -ltn 2>/dev/null | grep -E ':273(0[0-9]|1[0-5]) ' || echo "(ninguno)"
    echo
    if [ -n "$peer" ]; then
        echo "--- alcance a la otra máquina ($peer) ---"
        local p ok
        for p in 27300 27301 27302 27304 27305 27306; do
            ok=0
            if command -v nc >/dev/null 2>&1; then
                nc -z -w 2 "$peer" "$p" 2>/dev/null && ok=1
            else
                (exec 3<>"/dev/tcp/$peer/$p") 2>/dev/null && ok=1
            fi
            if [ "$ok" = 1 ]; then
                echo "  $peer:$p  OK"
            else
                echo "  $peer:$p  sin respuesta"
            fi
        done
    else
        echo "(definí las IPs y el rol para probar la otra máquina)"
    fi
    echo
    echo "--- espacio en disco ---"
    df -h . | tail -1
}

# --- menú -----------------------------------------------------------------

menu() {
    clear 2>/dev/null || true
    echo "=============================================================="
    echo " DSM — Prototipo 2        host: $(hostname 2>/dev/null)"
    echo "=============================================================="
    echo " Rol: ${ROLE:-sin definir}    A=${IP_A:-?}    B=${IP_B:-?}"
    echo "--------------------------------------------------------------"
    echo "  1) Configurar IPs de las dos máquinas"
    echo "  2) Elegir rol de esta máquina (A/B)"
    echo "  3) Compilar (dsm_node + dsmctl)"
    echo "  4) Arrancar nodo escuchando"
    echo "  5) Arrancar con aplicación"
    echo "  6) Consultar estado"
    echo "  7) Ver log de este nodo"
    echo "  8) Detener nodos"
    echo "  9) Diagnóstico de red"
    echo "  0) Salir"
    echo "--------------------------------------------------------------"
}

load_state

while true; do
    menu
    read -r -p "Opción: " opt
    case "$opt" in
        1) do_ips ;;
        2) do_role ;;
        3) do_build ;;
        4) do_start ;;
        5) do_app ;;
        6) do_status ;;
        7) do_log ;;
        8) do_stop ;;
        9) do_diag ;;
        0) exit 0 ;;
        *) echo "Opción inválida." ;;
    esac
    echo
    read -r -p "Enter para volver al menú..." _
done
