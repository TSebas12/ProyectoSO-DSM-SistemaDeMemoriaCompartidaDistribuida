#!/usr/bin/env bash
# dsm.sh — menú interactivo para configurar y probar el prototipo 2 del DSM.
#
# Uso:  ./dsm.sh
#
# Todo se hace desde este menú: topología (2/3/4 PCs), IPs, rol de esta máquina
# (A = home / nodo 0, B/C/D = clientes), compilación, arranque, consultas,
# logs, parada, validación de configuración y diagnóstico de red.

set -u
cd "$(dirname "$0")" || exit 1

STATE=".dsm_state"

ROLE=""
TOPOLOGY="2"
IP_A=""
IP_B=""
IP_C=""
IP_D=""

# --- estado ---------------------------------------------------------------

load_state() {
    [ -f "$STATE" ] || return 0
    # shellcheck disable=SC1090
    . "$STATE"
    TOPOLOGY="${TOPOLOGY:-2}"
}

save_state() {
    {
        printf 'ROLE=%s\n' "${ROLE:-}"
        printf 'TOPOLOGY=%s\n' "${TOPOLOGY:-2}"
        printf 'IP_A=%s\n' "${IP_A:-}"
        printf 'IP_B=%s\n' "${IP_B:-}"
        printf 'IP_C=%s\n' "${IP_C:-}"
        printf 'IP_D=%s\n' "${IP_D:-}"
    } >"$STATE"
}

cfg_for_role() {
    case "${ROLE:-}" in
        A) echo "config/node0.conf" ;;
        B) echo "config/node1.conf" ;;
        C) echo "config/node2.conf" ;;
        D) echo "config/node3.conf" ;;
        *) return 1 ;;
    esac
}

port_for_role() {
    case "${ROLE:-}" in
        A) echo 27300 ;;
        B) echo 27304 ;;
        C) echo 27308 ;;
        D) echo 27312 ;;
        *) return 1 ;;
    esac
}

role_ports() {
    case "${ROLE:-}" in
        A) echo "27300 27301 27302 27303" ;;
        B) echo "27304 27305 27306 27307" ;;
        C) echo "27308 27309 27310 27311" ;;
        D) echo "27312 27313 27314 27315" ;;
        *) return 1 ;;
    esac
}

peer_ips() {
    # Imprime las IP de los otros nodos según la topología.
    [ -n "${IP_B:-}" ] && [ "${TOPOLOGY:-2}" -ge 2 ] && echo "B ${IP_B}"
    [ -n "${IP_C:-}" ] && [ "${TOPOLOGY:-2}" -ge 3 ] && echo "C ${IP_C}"
    [ -n "${IP_D:-}" ] && [ "${TOPOLOGY:-2}" -ge 4 ] && echo "D ${IP_D}"
}

nodes_file_for_topology() {
    echo "config/nodes_${TOPOLOGY:-2}.conf"
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

write_nodes_file() {
    local n="${TOPOLOGY:-2}"
    local f="config/nodes_${n}.conf"
    local ips=("${IP_A}" "${IP_B}" "${IP_C}" "${IP_D}")
    local ports=(27300 27304 27308 27312)

    {
        echo "# Tabla de ${n} nodos (generada por dsm.sh)."
        echo "# Formato: id host puerto"
        local i
        for ((i = 0; i < n; i++)); do
            echo "${i} ${ips[$i]} ${ports[$i]}"
        done
    } >"$f"
}

# --- pasos ----------------------------------------------------------------

do_ips() {
    echo
    echo "Topología del clúster:"
    echo "  2) 2 PCs"
    echo "  3) 3 PCs"
    echo "  4) 4 PCs"
    read -r -p "  Topología [2/3/4]: " t
    case "$t" in
        2|3|4) TOPOLOGY="$t" ;;
        *) echo "Opción inválida."; return 1 ;;
    esac

    echo
    read -r -p "  IP_A (home, nodo 0): " a
    read -r -p "  IP_B (nodo 1): " b
    IP_A="$a"; IP_B="$b"
    if [ "$TOPOLOGY" -ge 3 ]; then
        read -r -p "  IP_C (nodo 2): " c; IP_C="$c"
    fi
    if [ "$TOPOLOGY" -ge 4 ]; then
        read -r -p "  IP_D (nodo 3): " d; IP_D="$d"
    fi

    if ! valid_ip "$IP_A" || ! valid_ip "$IP_B"; then
        echo "ERROR: IP_A o IP_B no válida."
        return 1
    fi
    if [ "$TOPOLOGY" -ge 3 ] && ! valid_ip "$IP_C"; then
        echo "ERROR: IP_C no válida."
        return 1
    fi
    if [ "$TOPOLOGY" -ge 4 ] && ! valid_ip "$IP_D"; then
        echo "ERROR: IP_D no válida."
        return 1
    fi

    write_nodes_file

    # Los nodos 1..3 apuntan al home (A).
    for f in config/node1.conf config/node2.conf config/node3.conf; do
        [ -f "$f" ] && sed -i "s|^seed_host.*|seed_host = $IP_A|" "$f"
    done

    save_state
    echo
    echo "Topología: $TOPOLOGY nodos"
    echo "Tabla generada: $(nodes_file_for_topology)"
    cat "$(nodes_file_for_topology)"
}

do_role() {
    echo
    echo "Rol de ESTA máquina:"
    echo "  A) home / nodo 0"
    echo "  B) cliente / nodo 1"
    echo "  C) cliente / nodo 2"
    echo "  D) cliente / nodo 3"
    read -r -p "  Opción [A/B/C/D]: " r
    case "$r" in
        A|a) ROLE="A" ;;
        B|b) ROLE="B" ;;
        C|c) ROLE="C" ;;
        D|d) ROLE="D" ;;
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
        src/app_bench.c src/app_evict.c src/app_chat.c src/app_task.c src/control.c src/web.c src/heartbeat.c \
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
    local pids

    pids=$(pgrep -f '[d]sm_node' 2>/dev/null || true)
    if [ -n "$pids" ]; then
        kill $pids 2>/dev/null || true
        sleep 2
    fi
    pids=$(pgrep -f '[d]sm_node' 2>/dev/null || true)
    if [ -n "$pids" ]; then
        kill -9 $pids 2>/dev/null || true
        sleep 1
    fi
}

check_port_free() {
    local p
    for p in $(role_ports); do
        if ss -ltn 2>/dev/null | grep -q ":$p "; then
            echo "ERROR: el puerto $p ya está ocupado."
            echo "       Cierra el nodo anterior (opción 8) o libéralo con la opción 10."
            ss -ltnp 2>/dev/null | grep ":$p " || true
            return 1
        fi
    done
    return 0
}

do_start() {
    local cfg nf
    cfg="$(cfg_for_role)" || { echo "Define el rol primero (opción 2)."; return 1; }
    [ -x ./dsm_node ] || { echo "Falta compilar (opción 3)."; return 1; }
    nf="$(nodes_file_for_topology)"

    kill_leftovers
    check_port_free || return 1

    echo
    echo "Arrancando: ./dsm_node --config $cfg --nodes $nf"
    echo "Deja esta terminal abierta. Ctrl+C para detener."
    echo
    ./dsm_node --config "$cfg" --nodes "$nf"
}

do_app() {
    local cfg nf
    cfg="$(cfg_for_role)" || { echo "Define el rol primero (opción 2)."; return 1; }
    [ -x ./dsm_node ] || { echo "Falta compilar (opción 3)."; return 1; }
    nf="$(nodes_file_for_topology)"

    echo
    echo "Aplicación:"
    echo "  1) demo"
    echo "  2) file (1 MB o --file RUTA)"
    echo "  3) counter"
    echo "  4) bench"
    echo "  5) evict (64 páginas / 32 marcos)"
    echo "  6) matrices"
    echo "  7) chat (texto libre)"
    read -r -p "  Opción: " opt

    local args=()
    case "$opt" in
        1) args=(--app demo) ;;
        2)
            read -r -p "  ruta de archivo (Enter = sintético 1 MB): " fp
            if [ -n "$fp" ]; then
                args=(--app file --file "$fp")
            else
                args=(--app file)
            fi
            ;;
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
        7)
            read -r -p "  nick (Enter = nodoN): " nk
            read -r -p "  duración seg [0=indefinido]: " du; du="${du:-0}"
            if [ -n "$nk" ]; then
                args=(--app chat --nick "$nk" --duration "$du")
            else
                args=(--app chat --duration "$du")
            fi
            ;;
        *) echo "Opción inválida."; return 1 ;;
    esac

    kill_leftovers
    check_port_free || return 1

    echo
    echo "Ejecutando en ESTA máquina:"
    echo "  ./dsm_node --config $cfg --nodes $nf ${args[*]}"
    echo
    echo "Lanza lo MISMO en las otras máquinas AHORA."
    echo
    ./dsm_node --config "$cfg" --nodes "$nf" "${args[@]}"
}

do_status() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Define el rol primero (opción 2)."; return 1; }

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
    cfg="$(cfg_for_role)" || { echo "Define el rol primero (opción 2)."; return 1; }
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
    local ports p occupied=0
    ports="$(role_ports)"
    echo
    kill_leftovers
    for p in $ports; do
        if ss -ltn 2>/dev/null | grep -q ":$p "; then
            echo "ERROR: el puerto $p sigue ocupado:"
            ss -ltnp 2>/dev/null | grep ":$p " || true
            occupied=1
        fi
    done
    if [ "$occupied" -eq 1 ]; then
        echo "Matá el proceso con: sudo kill -9 <PID>"
        return 1
    fi
    echo "Nodos detenidos; puertos del rol libres."
}

do_validate() {
    local cfg nf
    cfg="$(cfg_for_role)" || { echo "Define el rol primero (opción 2)."; return 1; }
    nf="$(nodes_file_for_topology)"
    echo
    echo "Validando configuración..."
    ./dsm_node --config "$cfg" --nodes "$nf" --check-config
}

do_diag() {
    local peer
    echo
    echo "--- IPs locales ---"
    hostname -I 2>/dev/null || ip -4 addr show | grep inet
    echo
    echo "--- topología: ${TOPOLOGY:-2} nodos ---"
    echo "  IP_A=$IP_A  IP_B=$IP_B  IP_C=${IP_C:-}  IP_D=${IP_D:-}"
    echo
    echo "--- procesos dsm_node ---"
    pgrep -af dsm_node || echo "(ninguno)"
    echo
    echo "--- puertos 27300-27315 escuchando (con PID/proceso) ---"
    ss -ltnp 2>/dev/null | grep -E ':273(0[0-9]|1[0-5]) ' || echo "(ninguno)"
    echo
    for peer in $(peer_ips); do
        # peer_ips prints "LABEL IP"
        :
    done
    # Probar alcance a cada peer.
    local label ip p ok
    for entry in $(peer_ips); do
        :
    done
    # peer_ips output is "B 1.2.3.4" (two tokens); iterate manually.
    if [ -n "${IP_B:-}" ] && [ "${TOPOLOGY:-2}" -ge 2 ]; then
        echo "--- alcance a B ($IP_B) ---"
        for p in 27300 27301 27302 27304 27305 27306; do
            ok=0
            if command -v nc >/dev/null 2>&1; then
                nc -z -w 2 "$IP_B" "$p" 2>/dev/null && ok=1
            else
                (exec 3<>"/dev/tcp/$IP_B/$p") 2>/dev/null && ok=1
            fi
            [ "$ok" = 1 ] && echo "  $IP_B:$p  OK" || echo "  $IP_B:$p  sin respuesta"
        done
    fi
    if [ -n "${IP_C:-}" ] && [ "${TOPOLOGY:-2}" -ge 3 ]; then
        echo "--- alcance a C ($IP_C) ---"
        for p in 27308 27309 27310; do
            ok=0
            if command -v nc >/dev/null 2>&1; then
                nc -z -w 2 "$IP_C" "$p" 2>/dev/null && ok=1
            else
                (exec 3<>"/dev/tcp/$IP_C/$p") 2>/dev/null && ok=1
            fi
            [ "$ok" = 1 ] && echo "  $IP_C:$p  OK" || echo "  $IP_C:$p  sin respuesta"
        done
    fi
    if [ -n "${IP_D:-}" ] && [ "${TOPOLOGY:-2}" -ge 4 ]; then
        echo "--- alcance a D ($IP_D) ---"
        for p in 27312 27313 27314; do
            ok=0
            if command -v nc >/dev/null 2>&1; then
                nc -z -w 2 "$IP_D" "$p" 2>/dev/null && ok=1
            else
                (exec 3<>"/dev/tcp/$IP_D/$p") 2>/dev/null && ok=1
            fi
            [ "$ok" = 1 ] && echo "  $IP_D:$p  OK" || echo "  $IP_D:$p  sin respuesta"
        done
    fi
    echo
    echo "--- espacio en disco ---"
    df -h . | tail -1
}

do_force() {
    local pids

    echo
    echo "Forzando liberación de puertos (SIGKILL)..."
    pids=$(pgrep -f '[d]sm_node' 2>/dev/null || true)
    if [ -n "$pids" ]; then
        kill -9 $pids 2>/dev/null || true
    fi
    sleep 1
    if command -v fuser >/dev/null 2>&1; then
        fuser -k 27300/tcp 27301/tcp 27302/tcp 27303/tcp \
              27304/tcp 27305/tcp 27306/tcp 27307/tcp \
              27308/tcp 27309/tcp 27310/tcp 27311/tcp \
              27312/tcp 27313/tcp 27314/tcp 27315/tcp 2>/dev/null || true
    fi
    echo "Listo. Verifica con la opción 9."
}

# --- menú -----------------------------------------------------------------

menu() {
    clear 2>/dev/null || true
    echo "=============================================================="
    echo " DSM — Prototipo 2        host: $(hostname 2>/dev/null)"
    echo "=============================================================="
    echo " Rol: ${ROLE:-sin definir}    Topología: ${TOPOLOGY:-2} nodos    A=${IP_A:-?}  B=${IP_B:-?}  C=${IP_C:-?}  D=${IP_D:-?}"
    echo "--------------------------------------------------------------"
    echo "  1) Configurar IPs / topología"
    echo "  2) Elegir rol de esta máquina (A/B/C/D)"
    echo "  3) Compilar (dsm_node + dsmctl)"
    echo "  4) Arrancar nodo escuchando"
    echo "  5) Arrancar con aplicación"
    echo "  6) Consultar estado"
    echo "  7) Ver log de este nodo"
    echo "  8) Detener nodos"
    echo "  9) Diagnóstico de red"
    echo " 10) Forzar liberación de puertos (SIGKILL)"
    echo " 11) Validar configuración"
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
       10) do_force ;;
       11) do_validate ;;
        0) exit 0 ;;
        *) echo "Opción inválida." ;;
    esac
    echo
    read -r -p "Enter para volver al menú..." _
done