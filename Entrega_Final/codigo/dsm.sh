#!/usr/bin/env bash
# dsm.sh — Menú interactivo mejorado para DSM (Distributed Shared Memory)
# Compatible con CLI actual: ./dsm_node --app X, dsmctl
# Modo no-interactivo: ./dsm.sh --auto --app matrices --size 128

set -u
cd "$(dirname "$0")" || exit 1

# ============================================================================
# CONFIGURACIÓN Y CONSTANTES
# ============================================================================

STATE=".dsm_state"
CONFIG_DIR="config"
LOG_DIR="logs"

# Colores ANSI
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

# Spinner UTF-8
SPINNER_CHARS=('⠋' '⠙' '⠹' '⠸' '⠼' '⠴' '⠦' '⠧' '⠇' '⠏')
SPINNER_PID=0

# Estado global
ROLE=""
TOPOLOGY="2"
IP_A=""
IP_B=""
IP_C=""
IP_D=""

# Modo no-interactivo
AUTO_MODE=false
AUTO_ACTION=""
AUTO_APP=""
AUTO_PARAMS=()

# ============================================================================
# UTILIDADES: COLORES, SPINNER, READ CON TIMEOUT
# ============================================================================

ansi() { printf '%b' "$1"; }

color_red() { ansi "${RED}$1${RESET}"; }
color_green() { ansi "${GREEN}$1${RESET}"; }
color_yellow() { ansi "${YELLOW}$1${RESET}"; }
color_blue() { ansi "${BLUE}$1${RESET}"; }
color_cyan() { ansi "${CYAN}$1${RESET}"; }
color_bold() { ansi "${BOLD}$1${RESET}"; }

# Spinner animado
spinner_start() {
    local msg="${1:-Procesando...}"
    (
        local i=0
        while true; do
            printf '\r%s %s' "${SPINNER_CHARS[i]}" "$msg"
            i=$(( (i + 1) % ${#SPINNER_CHARS[@]} ))
            sleep 0.1
        done
    ) &
    SPINNER_PID=$!
    disown $SPINNER_PID 2>/dev/null || true
}

spinner_stop() {
    if [ "$SPINNER_PID" -ne 0 ] 2>/dev/null; then
        kill "$SPINNER_PID" 2>/dev/null || true
        wait "$SPINNER_PID" 2>/dev/null || true
        SPINNER_PID=0
        printf '\r\033[K'  # Limpiar línea
    fi
}

# Read con timeout, soporte 'b' (back) y ESC
read_with_timeout() {
    local prompt="$1" var_name="$2" timeout="${3:-30}" default="${4:-}"
    local input="" char="" timed_out=false back_requested=false

    # Configurar terminal para lectura carácter a carácter
    local old_stty
    old_stty=$(stty -g 2>/dev/null) || old_stty=""
    stty -icanon -echo min 0 time 0 2>/dev/null || true

    printf '%s' "$prompt" >&2

    local end_time=$((SECONDS + timeout))
    while [ $SECONDS -lt $end_time ]; do
        IFS= read -r -n1 -t 1 char 2>/dev/null || char=""
        if [ -n "$char" ]; then
            case "$char" in
                $'\n'|$'\r')
                    echo >&2
                    break
                    ;;
                $'\177'|$'\b')  # Backspace
                    if [ -n "$input" ]; then
                        input="${input%?}"
                        printf '\b \b' >&2
                    fi
                    ;;
                $'\e')  # ESC
                    back_requested=true
                    echo >&2
                    break
                    ;;
                'b'|'B')
                    if [ -z "$input" ]; then
                        back_requested=true
                        echo >&2
                        break
                    else
                        input="${input}${char}"
                        printf '%s' "$char" >&2
                    fi
                    ;;
                *)
                    input="${input}${char}"
                    printf '%s' "$char" >&2
                    ;;
            esac
        fi
    done

    # Restaurar terminal
    [ -n "$old_stty" ] && stty "$old_stty" 2>/dev/null || true

    if [ "$back_requested" = true ]; then
        return 2  # Usuario pidió volver
    fi

    if [ -z "$input" ] && [ $SECONDS -ge $end_time ]; then
        timed_out=true
    fi

    if [ -z "$input" ] && [ -n "$default" ]; then
        input="$default"
    fi

    printf -v "$var_name" '%s' "$input"

    [ "$timed_out" = true ] && return 1
    return 0
}

# Read simple con validación numérica
read_number() {
    local prompt="$1" var_name="$2" default="${3:-}" min="${4:-0}" max="${5:-9999}"
    local input
    while true; do
        read_with_timeout "$prompt" input 30 "$default" || return $?
        case $? in
            2) return 2 ;;  # back
        esac
        if [[ "$input" =~ ^[0-9]+$ ]] && [ "$input" -ge "$min" ] && [ "$input" -le "$max" ]; then
            printf -v "$var_name" '%s' "$input"
            return 0
        fi
        echo "  Valor inválido. Debe ser un número entre $min y $max." >&2
    done
}

# Confirmación [s/N]
confirm_action() {
    local prompt="${1:-¿Confirmar?}" default="${2:-N}"
    local input
    read_with_timeout "$prompt [s/N]: " input 15 "$default" || return $?
    case "${input,,}" in
        s|si|sí|y|yes) return 0 ;;
        *) return 1 ;;
    esac
}

# ============================================================================
# ESTADO Y PERSISTENCIA
# ============================================================================

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

validate_port_free() {
    local port="$1"
    if command -v ss >/dev/null 2>&1; then
        ss -ltn 2>/dev/null | grep -q ":$port " && return 1
    elif command -v netstat >/dev/null 2>&1; then
        netstat -ltn 2>/dev/null | grep -q ":$port " && return 1
    else
        # Fallback /proc
        [ -f /proc/net/tcp ] && awk -v p="$port" '$2 ~ ":" sprintf("%04X", p) "$" {exit 1}' /proc/net/tcp 2>/dev/null && return 1
    fi
    return 0
}

check_reachability() {
    local ip="$1" port="$2" timeout="${3:-2}"
    if command -v nc >/dev/null 2>&1; then
        nc -z -w "$timeout" "$ip" "$port" 2>/dev/null && return 0
    fi
    # Fallback bash /dev/tcp
    (exec 3<>"/dev/tcp/$ip/$port") 2>/dev/null && { exec 3>&-; return 0; }
    return 1
}

write_nodes_file() {
    local n="${TOPOLOGY:-2}"
    local f="config/nodes_${n}.conf"
    local ips=("${IP_A}" "${IP_B}" "${IP_C}" "${IP_D}")
    local ports=(27300 27304 27308 27312)

    mkdir -p "$CONFIG_DIR"
    {
        echo "# Tabla de ${n} nodos (generada por dsm.sh)."
        echo "# Formato: id host puerto"
        local i
        for ((i = 0; i < n; i++)); do
            echo "${i} ${ips[$i]} ${ports[$i]}"
        done
    } >"$f"
}

# ============================================================================
# OPERACIONES BÁSICAS (compilar, matar, puertos)
# ============================================================================

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

    spinner_start "Compilando dsm_node..."
    if gcc -std=c11 -D_GNU_SOURCE -Iinclude -O2 -g -include "$hdr" -o dsm_node \
        src/sync.c src/stats.c src/memory.c src/fault.c src/coherence.c \
        src/config.c src/protocol.c src/log.c src/nodes.c src/net.c \
        src/dsm_api.c src/runtime.c src/demo_page.c src/apps.c \
        src/app_common.c src/app_matrices.c src/app_file.c src/app_counter.c \
        src/app_bench.c src/app_evict.c src/app_chat.c src/app_task.c src/control.c src/web.c src/heartbeat.c \
        src/node_main.c -lpthread -lm -lrt 2>&1 | tail -5; then
        spinner_stop
        spinner_start "Compilando dsmctl..."
        if gcc -std=c11 -D_GNU_SOURCE -Iinclude -O2 -g -include "$hdr" -o dsmctl \
            src/ctl_main.c src/config.c -lpthread -lm 2>&1 | tail -5; then
            spinner_stop
            rm -f "$hdr"
            echo "$(color_green 'OK:') binarios listos"
            ls -l dsm_node dsmctl
            return 0
        fi
    fi
    spinner_stop
    rm -f "$hdr"
    echo "$(color_red 'ERROR:') la compilación falló."
    return 1
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

check_ports_free() {
    local p all_free=true
    for p in $(role_ports); do
        if ! validate_port_free "$p"; then
            echo "$(color_red 'ERROR:') el puerto $p ya está ocupado."
            all_free=false
        fi
    done
    [ "$all_free" = true ] && return 0 || return 1
}

# ============================================================================
# ESTADO DEL CLÚSTER (ACTIVO/DEGRADED/DETENIDO)
# ============================================================================

get_cluster_status() {
    local cfg active=0 total=0
    cfg="$(cfg_for_role)" || return 1
    [ -x ./dsmctl ] || return 1
    [ -f "$(nodes_file_for_topology)" ] || return 1

    total="${TOPOLOGY:-2}"

    # Intentar obtener nodos via dsmctl
    local nodes_output
    nodes_output=$(./dsmctl --config "$cfg" nodes 2>/dev/null || true)

    if [ -n "$nodes_output" ]; then
        # Contar líneas que parecen nodos activos (formato: id ip port status)
        active=$(echo "$nodes_output" | grep -cE '^[0-9]+\s+' 2>/dev/null || echo 0)
    fi

    if [ "$active" -eq 0 ]; then
        echo "DETENIDO"
    elif [ "$active" -eq "$total" ]; then
        echo "ACTIVO ($active/$total nodos)"
    else
        echo "DEGRADED ($active/$total nodos)"
    fi
}

get_cluster_status_color() {
    local status="$1"
    case "$status" in
        ACTIVO*) color_green "$status" ;;
        DEGRADED*) color_yellow "$status" ;;
        *) color_red "$status" ;;
    esac
}

# ============================================================================
# CABECERA Y MENÚ PRINCIPAL
# ============================================================================

print_header() {
    clear 2>/dev/null || true
    local status
    status=$(get_cluster_status 2>/dev/null || echo "DETENIDO")
    local nodes_file
    nodes_file=$(nodes_file_for_topology)

    echo "===================================================="
    echo "       $(color_bold 'DSM - SISTEMA DE MEMORIA COMPARTIDA')"
    echo "===================================================="
    echo
    printf 'Estado: %s    Configuración: %s (%s nodos)\n' \
        "$(get_cluster_status_color "$status")" \
        "$nodes_file" \
        "${TOPOLOGY:-2}"
    echo
}

print_main_menu() {
    echo "  $(color_cyan '1.') Iniciar clúster"
    echo "  $(color_cyan '2.') Configurar nodos"
    echo "  $(color_cyan '3.') Ver estado de los nodos"
    echo "  $(color_cyan '4.') Ejecutar aplicaciones"
    echo "  $(color_cyan '5.') Consultar estadísticas"
    echo "  $(color_cyan '6.') Monitorear memoria"
    echo "  $(color_cyan '7.') Información del sistema"
    echo "  $(color_cyan '8.') Detener clúster"
    echo "  $(color_cyan '9.') Ayuda"
    echo "  $(color_cyan '0.') Salir"
    echo "----------------------------------------------------"
}

# ============================================================================
# OPCIÓN 1: INICIAR CLÚSTER
# ============================================================================

select_config_file() {
    local configs=()
    local i=1
    for f in config/nodes_*.conf; do
        [ -f "$f" ] || continue
        local n
        n=$(basename "$f" .conf | sed 's/nodes_//')
        configs+=("$f")
        echo "  $i) $f  ($n nodos)${f##*/ == $(basename $(nodes_file_for_topology)) && echo '  ← actual'}"
        ((i++))
    done
    [ ${#configs[@]} -eq 0 ] && { echo "  No hay archivos de configuración."; return 1; }

    local choice
    read_number "  Seleccione configuración [1-${#configs[@]}] (b=volver): " choice 30 1 ${#configs[@]} || return $?
    echo "${configs[$((choice-1))]}"
}

show_nodes_preview() {
    local nodes_file="$1"
    echo "--- Vista previa $nodes_file ---"
    local id ip port
    while read -r id ip port; do
        [ -z "$id" ] && continue
        [[ "$id" =~ ^# ]] && continue
        local label=""
        [ "$id" = "0" ] && label=" $(color_green '(HOME)')"
        printf '  %s  %-15s  %s%s\n' "$id" "$ip" "$port" "$label"
    done <"$nodes_file"
    echo "----------------------------------"
}

do_start_cluster() {
    local cfg nf timeout_choice confirm
    cfg="$(cfg_for_role)" || { echo "Define el rol primero (opción 2)."; return 1; }
    [ -x ./dsm_node ] || { echo "$(color_red 'Falta compilar') (opción 3 en menú principal)."; return 1; }

    nf=$(select_config_file) || return $?
    show_nodes_preview "$nf"

    read_number "  Timeout arranque [30s]: " timeout_choice 30 5 300 || return $?
    confirm_action "Confirmar inicio del clúster" "N" || return 1

    kill_leftovers
    check_ports_free || return 1

    echo
    echo "Iniciando clúster..."
    echo "Comando: ./dsm_node --config $cfg --nodes $nf"
    echo

    # Arrancar en background y mostrar progreso
    local log_file="$LOG_DIR/cluster_startup.log"
    mkdir -p "$LOG_DIR"
    ./dsm_node --config "$cfg" --nodes "$nf" >"$log_file" 2>&1 &
    local node_pid=$!

    spinner_start "Esperando inicialización de nodos..."
    local elapsed=0
    local max_wait=$timeout_choice
    local nodes_ready=0

    while [ $elapsed -lt $max_wait ]; do
        sleep 1
        ((elapsed++))
        # Verificar cuántos nodos responden
        nodes_ready=$(./dsmctl --config "$cfg" nodes 2>/dev/null | grep -cE '^[0-9]+\s+' || echo 0)
        if [ "$nodes_ready" -eq "${TOPOLOGY:-2}" ]; then
            spinner_stop
            echo "$(color_green "✓ Clúster ACTIVO ($nodes_ready/${TOPOLOGY:-2} nodos)")"
            return 0
        fi
        # Actualizar mensaje del spinner
        kill -USR1 "$SPINNER_PID" 2>/dev/null || true
    done

    spinner_stop
    echo "$(color_yellow "⚠ Timeout: $nodes_ready/${TOPOLOGY:-2} nodos listos")"
    echo
    echo "Nodos pendientes:"
    ./dsmctl --config "$cfg" nodes 2>/dev/null | grep -E '^[0-9]+\s+' || echo "  (sin respuesta)"

    echo
    echo "Opciones:"
    echo "  1) Esperar más tiempo"
    echo "  2) Ver logs de arranque"
    echo "  3) Forzar detención y volver"
    echo "  b) Volver al menú principal (clúster sigue arrancando en background)"

    local opt
    read_with_timeout "  Seleccione [1-3/b]: " opt 30 "b" || return $?
    case "$opt" in
        1)
            read_number "  Segundos adicionales: " timeout_choice 30 5 300 || return $?
            # Reintentar lógica de espera (simplificado)
            spinner_start "Esperando nodos restantes..."
            sleep "$timeout_choice"
            spinner_stop
            nodes_ready=$(./dsmctl --config "$cfg" nodes 2>/dev/null | grep -cE '^[0-9]+\s+' || echo 0)
            [ "$nodes_ready" -eq "${TOPOLOGY:-2}" ] && echo "$(color_green "✓ Clúster ACTIVO")" || echo "$(color_yellow "Aún pendientes: $nodes_ready/${TOPOLOGY:-2}")"
            ;;
        2)
            echo "--- Log de arranque ---"
            tail -50 "$log_file" 2>/dev/null || echo "(log no disponible)"
            ;;
        3)
            kill $node_pid 2>/dev/null; sleep 1; kill -9 $node_pid 2>/dev/null
            echo "Proceso de arranque terminado."
            ;;
        b|B) ;;
    esac
}

# ============================================================================
# OPCIÓN 2: CONFIGURAR NODOS
# ============================================================================

do_configure_nodes() {
    while true; do
        print_header
        echo "       $(color_bold 'CONFIGURAR NODOS')"
        echo "===================================================="
        echo "Topología actual: ${TOPOLOGY:-2} nodos"
        echo
        echo "  $(color_cyan '1.') Cambiar topología (2/3/4 nodos)"
        echo "  $(color_cyan '2.') Editar IPs y puertos"
        echo "  $(color_cyan '3.') Validar configuración actual"
        echo "  $(color_cyan '4.') Ver configuración generada"
        echo "  $(color_cyan 'b.') Volver al menú principal"
        echo "----------------------------------------------------"

        local opt
        read_with_timeout "Seleccione: " opt 30 "b" || return $?
        case "$opt" in
            1) do_change_topology ;;
            2) do_edit_ips_ports ;;
            3) do_validate_config ;;
            4) show_nodes_preview "$(nodes_file_for_topology)" ;;
            b|B) return 0 ;;
            *) echo "Opción inválida." ;;
        esac
        echo
        read_with_timeout "Enter para continuar... " _ 5 "" || true
    done
}

do_change_topology() {
    echo
    echo "Topología del clúster:"
    echo "  2) 2 PCs"
    echo "  3) 3 PCs"
    echo "  4) 4 PCs"
    local t
    read_number "  Topología [2/3/4]: " t 30 "$TOPOLOGY" 2 4 || return $?
    TOPOLOGY="$t"
    save_state
    write_nodes_file
    echo "$(color_green 'OK:') Topología cambiada a $TOPOLOGY nodos"
    show_nodes_preview "$(nodes_file_for_topology)"
}

do_edit_ips_ports() {
    echo
    local ips=("$IP_A" "$IP_B" "$IP_C" "$IP_D")
    local labels=("IP_A (HOME, nodo 0)" "IP_B (nodo 1)" "IP_C (nodo 2)" "IP_D (nodo 3)")
    local ports_base=(27300 27304 27308 27312)
    local i

    for ((i=0; i<TOPOLOGY; i++)); do
        echo "--- ${labels[$i]} ---"
        local current_ip="${ips[$i]:-}"
        read_with_timeout "  IP actual: $current_ip → Nueva IP: " current_ip 30 "$current_ip" || return $?
        [ -z "$current_ip" ] && { echo "  IP no puede estar vacía."; return 1; }
        if ! valid_ip "$current_ip"; then
            echo "  $(color_red 'IP inválida.')"
            return 1
        fi
        ips[$i]="$current_ip"

        # Verificar alcance
        spinner_start "Verificando alcance a $current_ip..."
        sleep 1
        spinner_stop
        if check_reachability "$current_ip" "${ports_base[$i]}" 3; then
            echo "  $(color_green '✓ Alcanzable')"
        else
            echo "  $(color_yellow '⚠ No responde en puerto') ${ports_base[$i]} (puede estar apagado)"
        fi

        # Puertos
        echo "  Puertos base: ${ports_base[$i]} ${ports_base[$i]+1} ${ports_base[$i]+2} ${ports_base[$i]+3}"
        confirm_action "  ¿Modificar puertos base?" "N" && {
            local new_base
            read_number "  Nuevo puerto base: " new_base 30 "${ports_base[$i]}" 1024 65531 || return $?
            # Validar rango libre
            local p all_free=true
            for ((p=new_base; p<new_base+4; p++)); do
                validate_port_free "$p" || { all_free=false; echo "  Puerto $p ocupado."; }
            done
            [ "$all_free" = true ] && ports_base[$i]=$new_base
        }
        echo
    done

    IP_A="${ips[0]}"; IP_B="${ips[1]}"; IP_C="${ips[2]}"; IP_D="${ips[3]}"
    write_nodes_file

    # Actualizar seed_host en configs de clientes
    for f in config/node1.conf config/node2.conf config/node3.conf; do
        [ -f "$f" ] && sed -i "s|^seed_host.*|seed_host = $IP_A|" "$f"
    done

    save_state
    echo "$(color_green 'OK:') Configuración guardada y nodes_${TOPOLOGY}.conf generado"
    show_nodes_preview "$(nodes_file_for_topology)"
}

do_validate_config() {
    local cfg nf
    cfg="$(cfg_for_role)" || { echo "Define el rol primero."; return 1; }
    nf="$(nodes_file_for_topology)"
    echo
    echo "Validando configuración..."
    ./dsm_node --config "$cfg" --nodes "$nf" --check-config 2>&1 | head -30
}

# ============================================================================
# OPCIÓN 3: VER ESTADO DE LOS NODOS
# ============================================================================

do_view_status() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Define el rol primero."; return 1; }
    [ -x ./dsmctl ] || { echo "Falta compilar (dsmctl)."; return 1; }

    while true; do
        print_header
        echo "       $(color_bold 'ESTADO DEL CLÚSTER')"
        echo "===================================================="

        # Nodo HOME
        local home_status="$(color_red 'DESCONOCIDO')"
        if [ -n "${IP_A:-}" ]; then
            if check_reachability "$IP_A" 27301 2; then
                home_status="$(color_green '● CONECTADO') (heartbeat: $(color_green 'vivo'))"
            else
                home_status="$(color_red '○ SIN RESPUESTA') (heartbeat: $(color_red 'sin resp.'))"
            fi
        fi
        printf 'Nodo HOME: %-15s %s\n' "$IP_A:27301" "$home_status"
        echo

        # Tabla de nodos
        printf '%-4s %-15s %-8s %-15s %-15s\n' "ID" "IP" "PUERTO" "ESTADO" "HEARTBEAT"
        echo "─────────────────────────────────────────────────────────────"

        local id ip port status hb
        local nodes_output
        nodes_output=$(./dsmctl --config "$cfg" nodes 2>/dev/null || true)

        if [ -n "$nodes_output" ]; then
            while IFS= read -r line; do
                [[ "$line" =~ ^[[:space:]]*$ ]] && continue
                [[ "$line" =~ ^# ]] && continue
                read -r id ip port status <<<"$line"
                [ -z "$id" ] && continue
                if check_reachability "$ip" "$port" 1; then
                    status="$(color_green 'ACTIVO')"
                    hb="$(color_green '● vivo')"
                else
                    status="$(color_red 'DESCONECTADO')"
                    hb="$(color_red '○ sin resp.')"
                fi
                printf '%-4s %-15s %-8s %-25s %-20s\n' "$id" "$ip" "$port" "$status" "$hb"
            done <<<"$nodes_output"
        else
            # Fallback: leer de nodes.conf
            local nf
            nf=$(nodes_file_for_topology)
            local ports=(27300 27304 27308 27312)
            local ips=("$IP_A" "$IP_B" "$IP_C" "$IP_D")
            for ((id=0; id<TOPOLOGY; id++)); do
                ip="${ips[$id]}"
                port="${ports[$id]}"
                if [ -n "$ip" ] && check_reachability "$ip" "$port" 1; then
                    status="$(color_green 'ACTIVO')"
                    hb="$(color_green '● vivo')"
                else
                    status="$(color_yellow 'PENDIENTE')"
                    hb="$(color_red '○ sin resp.')"
                fi
                printf '%-4s %-15s %-8s %-25s %-20s\n' "$id" "$ip" "$port" "$status" "$hb"
            done
        fi
        echo

        # Errores de conexión
        echo "Errores de conexión:"
        local has_errors=false
        for ((id=1; id<TOPOLOGY; id++)); do
            ip="${ips[$id]:-}"
            port="${ports[$id]:-}"
            [ -z "$ip" ] && continue
            if ! check_reachability "$ip" "$port" 1; then
                echo "  [$id] $(color_red 'Timeout/Refused') $ip:$port"
                has_errors=true
            fi
        done
        [ "$has_errors" = false ] && echo "  $(color_green 'Ninguno')"
        echo

        echo "  $(color_cyan 'r.') Refrescar    $(color_cyan 'b.') Volver"
        local opt
        read_with_timeout "Seleccione: " opt 10 "r" || return $?
        case "${opt,,}" in
            b) return 0 ;;
            r) continue ;;
        esac
    done
}

# ============================================================================
# OPCIÓN 4: EJECUTAR APLICACIONES
# ============================================================================

run_app_generic() {
    local app_name="$1" cfg nf args=("${@:2}")
    cfg="$(cfg_for_role)" || { echo "Define el rol primero."; return 1; }
    [ -x ./dsm_node ] || { echo "Falta compilar."; return 1; }
    nf="$(nodes_file_for_topology)"

    kill_leftovers
    check_ports_free || return 1

    echo
    echo "Ejecutando en ESTA máquina:"
    echo "  ./dsm_node --config $cfg --nodes $nf --app $app_name ${args[*]}"
    echo "$(color_yellow 'Lanza lo MISMO en las otras máquinas AHORA.')"
    echo

    local log_file="$LOG_DIR/${app_name}_$(date +%s).log"
    mkdir -p "$LOG_DIR"

    # Ejecutar con tee para ver en tiempo real y guardar log
    ./dsm_node --config "$cfg" --nodes "$nf" --app "$app_name" "${args[@]}" 2>&1 | tee "$log_file"
    local exit_code=${PIPESTATUS[0]}

    echo
    echo "----------------------------------------------------"
    if [ $exit_code -eq 0 ]; then
        echo "$(color_green '✓ Completado exitosamente')"
    else
        echo "$(color_red '✗ Terminó con código') $exit_code"
    fi
    echo "Log guardado en: $log_file"

    # Mostrar resumen si existe
    if [ -f "$log_file" ]; then
        echo
        echo "--- Resumen (últimas 20 líneas) ---"
        tail -20 "$log_file"
    fi

    echo
    read_with_timeout "  v. Ver log completo    b. Volver: " opt 10 "b" || return $?
    [ "${opt,,}" = "v" ] && { less "$log_file" 2>/dev/null || cat "$log_file"; }
}

do_run_apps() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Define el rol primero."; return 1; }
    [ -x ./dsm_node ] || { echo "Falta compilar."; return 1; }

    while true; do
        print_header
        echo "       $(color_bold 'EJECUTAR APLICACIONES')"
        echo "===================================================="
        echo
        echo "  $(color_cyan '1.') demo        - Demostración básica DSM"
        echo "  $(color_cyan '2.') matrices    - Multiplicación matrices (--size N)"
        echo "  $(color_cyan '3.') file        - Archivo compartido (--file PATH)"
        echo "  $(color_cyan '4.') counter     - Contador concurrente (--iters --threads)"
        echo "  $(color_cyan '5.') bench       - Benchmark rendimiento"
        echo "  $(color_cyan '6.') evict       - Test evicción (--pages --frames)"
        echo "  $(color_cyan '7.') chat        - Chat distribuido (--nick --duration)"
        echo "  $(color_cyan '8.') task        - Distribución tareas (--duration)"
        echo "  $(color_cyan 'b.') Volver al menú principal"
        echo "----------------------------------------------------"

        local opt
        read_with_timeout "Seleccione aplicación [1-8]: " opt 30 "b" || return $?
        case "$opt" in
            1) run_app_generic "demo" ;;
            2)
                local sz
                read_number "  Tamaño matriz [64]: " sz 30 64 1 2048 || return $?
                run_app_generic "matrices" "--size" "$sz"
                ;;
            3)
                local fp
                read_with_timeout "  Ruta archivo (Enter=sintético 1MB): " fp 30 "" || return $?
                if [ -n "$fp" ]; then
                    run_app_generic "file" "--file" "$fp"
                else
                    run_app_generic "file"
                fi
                ;;
            4)
                local it th
                read_number "  Iteraciones [100]: " it 30 100 1 10000 || return $?
                read_number "  Hilos [2]: " th 30 2 1 64 || return $?
                run_app_generic "counter" "--iters" "$it" "--threads" "$th"
                ;;
            5) run_app_generic "bench" ;;
            6)
                local pg fr
                read_number "  Páginas [64]: " pg 30 64 1 1024 || return $?
                read_number "  Marcos [32]: " fr 30 32 1 512 || return $?
                run_app_generic "evict" "--pages" "$pg" "--frames" "$fr"
                ;;
            7)
                local nk du
                read_with_timeout "  Nick (Enter=nodoN): " nk 30 "" || return $?
                read_number "  Duración seg [0=indefinido]: " du 30 0 0 3600 || return $?
                if [ -n "$nk" ]; then
                    run_app_generic "chat" "--nick" "$nk" "--duration" "$du"
                else
                    run_app_generic "chat" "--duration" "$du"
                fi
                ;;
            8)
                local du
                read_number "  Duración seg [30]: " du 30 30 1 3600 || return $?
                run_app_generic "task" "--duration" "$du"
                ;;
            b|B) return 0 ;;
            *) echo "Opción inválida." ;;
        esac
    done
}

# ============================================================================
# OPCIÓN 5: CONSULTAR ESTADÍSTICAS
# ============================================================================

do_show_stats() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Define el rol primero."; return 1; }
    [ -x ./dsmctl ] || { echo "Falta compilar (dsmctl)."; return 1; }

    local auto_refresh=true
    while true; do
        print_header
        echo "       $(color_bold 'ESTADÍSTICAS DSM')"
        echo "===================================================="
        printf 'Auto-refresh: %s    ' "$([ "$auto_refresh" = true ] && color_green 'ACTIVO (5s)' || color_red 'DESACTIVADO')"
        echo "[$(color_cyan 'a')] Alternar  [$(color_cyan 'r')] Refrescar  [$(color_cyan 'b')] Volver"
        echo

        # stats
        echo "--- $(color_bold 'dsmctl stats') ---"
        ./dsmctl --config "$cfg" stats 2>/dev/null || echo "  (error al obtener stats)"
        echo

        # pages (primeras 20)
        echo "--- $(color_bold 'dsmctl pages (primeras 20)') ---"
        ./dsmctl --config "$cfg" pages 2>/dev/null | head -20 || echo "  (error)"
        echo

        # objects
        echo "--- $(color_bold 'dsmctl objects') ---"
        ./dsmctl --config "$cfg" objects 2>/dev/null || echo "  (error)"
        echo

        # dir (solo home)
        if [ -n "${IP_A:-}" ]; then
            echo "--- $(color_bold 'dsmctl dir (HOME en') $IP_A) ---"
            ./dsmctl --host "$IP_A" --port 27301 dir 2>/dev/null || echo "  (error o no accesible)"
        fi

        if [ "$auto_refresh" = true ]; then
            # read con timeout 5s
            local key
            read_with_timeout "" key 5 "" || true
            case "${key,,}" in
                a) auto_refresh=false ;;
                r) continue ;;
                b) return 0 ;;
            esac
        else
            read_with_timeout "" key 30 "b" || return $?
            case "${key,,}" in
                a) auto_refresh=true ;;
                b) return 0 ;;
            esac
        fi
    done
}

# ============================================================================
# OPCIÓN 6: MONITOREAR MEMORIA
# ============================================================================

draw_ascii_bar() {
    local pct="$1" width=25
    local filled=$((pct * width / 100))
    local empty=$((width - filled))
    printf '%s%s' "$(printf '█%.0s' $(seq 1 $filled))" "$(printf '░%.0s' $(seq 1 $empty))"
}

do_monitor_memory() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "Define el rol primero."; return 1; }
    [ -x ./dsmctl ] || { echo "Falta compilar (dsmctl)."; return 1; }

    local auto_refresh=true
    while true; do
        print_header
        echo "       $(color_bold 'MONITOR DE MEMORIA')"
        echo "===================================================="
        printf 'Auto-refresh: %s    ' "$([ "$auto_refresh" = true ] && color_green 'ACTIVO (5s)' || color_red 'DESACTIVADO')"
        echo "[$(color_cyan 'a')] Alternar  [$(color_cyan 'r')] Refrescar  [$(color_cyan 'b')] Volver"
        echo

        local nodes=("$IP_A" "$IP_B" "$IP_C" "$IP_D")
        local ports=(27301 27305 27309 27313)  # control ports

        echo "┌─────────────────────────────────────────────────────────────┐"
        echo "│ $(color_bold 'MARCOS POR NODO')                                              │"
        echo "├─────┬──────────┬────────┬────────┬───────┬─────────┬──────────┤"
        printf '│ %-3s │ %-8s │ %-6s │ %-6s │ %-5s │ %-7s │ %-8s │\n' "Nodo" "Total" "Libres" "Usados" "% Uso" "Swap (KB)" "MSI"
        echo "├─────┼──────────┼────────┼────────┼───────┼─────────┼──────────┤"

        for ((i=0; i<TOPOLOGY; i++)); do
            ip="${nodes[$i]:-}"
            port="${ports[$i]:-}"
            [ -z "$ip" ] && continue

            # Query real stats via dsmctl
            local stats_output
            stats_output=$(./dsmctl --host "$ip" --port "$port" stats 2>/dev/null || true)

            # Parse stats: "marcos libres" and "paginas residentes"
            local free_frames resident_pages swap_bytes
            free_frames=$(echo "$stats_output" | grep -E '^marcos libres' | awk '{print $NF}' 2>/dev/null || echo "0")
            resident_pages=$(echo "$stats_output" | grep -E '^paginas residentes' | awk '{print $NF}' 2>/dev/null || echo "0")
            swap_bytes=$(echo "$stats_output" | grep -E '^bytes de swap' | awk '{print $NF}' 2>/dev/null || echo "0")
            local errors=$(echo "$stats_output" | grep -E '^errores' | awk '{print $NF}' 2>/dev/null || echo "0")

            local total_frames=256
            local used_frames=$((total_frames - free_frames))
            local pct=$(( used_frames * 100 / total_frames ))
            local swap_kb=$(( swap_bytes / 1024 ))

            # MSI states not directly available via stats, mark as N/A
            local m_count=0 s_count=0 i_count=0

            local pct_color="$RESET"
            [ $pct -gt 70 ] && pct_color="$RED"
            [ $pct -gt 40 ] && [ $pct -le 70 ] && pct_color="$YELLOW"
            [ $pct -le 40 ] && pct_color="$GREEN"

            printf '│ %-3s │ %-8s │ %-6s │ %-6s │ %s%3d%%%s │ %7d │ M:%-2d S:%-2d │\n' \
                "$i" "$total_frames" "$free_frames" "$used_frames" "$pct_color" "$pct" "$RESET" "$swap_kb" "$m_count" "$s_count"
        done
        echo "└─────┴──────────┴────────┴────────┴───────┴─────────┴──────────┘"
        echo

        echo "$(color_bold 'Gráfico de uso (%):')"
        for ((i=0; i<TOPOLOGY; i++)); do
            ip="${nodes[$i]:-}"
            [ -z "$ip" ] && continue
            # Query real usage for graph
            local stats_output
            stats_output=$(./dsmctl --host "$ip" --port "${ports[$i]}" stats 2>/dev/null || true)
            local free_frames
            free_frames=$(echo "$stats_output" | grep -E '^marcos libres' | awk '{print $NF}' 2>/dev/null || echo "0")
            local used_frames=$((256 - free_frames))
            local pct=$(( used_frames * 100 / 256 ))
            local bar
            bar=$(draw_ascii_bar "$pct")
            printf '  Nodo %d %s %3d%%\n' "$i" "$bar" "$pct"
        done
        echo
        echo "Leyenda MSI: $(color_bold 'M')=Modified  $(color_bold 'S')=Shared  $(color_bold 'I')=Invalid"
        echo "$(color_yellow 'Nota:') MSI por nodo no disponible via dsmctl stats; se muestran solo métricas de memoria."

        if [ "$auto_refresh" = true ]; then
            local key
            read_with_timeout "" key 5 "" || true
            case "${key,,}" in
                a) auto_refresh=false ;;
                r) continue ;;
                b) return 0 ;;
            esac
        else
            read_with_timeout "" key 30 "b" || return $?
            case "${key,,}" in
                a) auto_refresh=true ;;
                b) return 0 ;;
            esac
        fi
    done
}

# ============================================================================
# OPCIÓN 7: INFORMACIÓN DEL SISTEMA
# ============================================================================

do_system_info() {
    print_header
    echo "       $(color_bold 'INFORMACIÓN DEL SISTEMA')"
    echo "===================================================="
    echo

    # Versión
    echo "$(color_bold 'Versión DSM:') 2.0.0 (Prototipo 2)"
    echo "$(color_bold 'Compilado:') $(date -r dsm_node 2>/dev/null || echo 'desconocido')"
    echo "$(color_bold 'Host:') $(hostname) ($(uname -sr))"
    echo

    # Configuración actual
    echo "--- $(color_bold 'Configuración actual') ($(nodes_file_for_topology)) ---"
    show_nodes_preview "$(nodes_file_for_topology)"
    echo

    # Puertos en uso
    echo "--- $(color_bold 'Puertos en uso (ss -ltnp)') ---"
    if command -v ss >/dev/null 2>&1; then
        ss -ltnp 2>/dev/null | grep -E ':273(0[0-9]|1[0-5]) ' || echo "  (ninguno)"
    elif command -v netstat >/dev/null 2>&1; then
        netstat -ltnp 2>/dev/null | grep -E ':273(0[0-9]|1[0-5]) ' || echo "  (ninguno)"
    else
        echo "  (ss/netstat no disponible)"
    fi
    echo

    # Recursos
    echo "--- $(color_bold 'Recursos del sistema') ---"
    echo "$(color_bold 'Disco:')"
    df -h . 2>/dev/null | tail -1 | awk '{printf "  %s / %s (%s usado, %s libre)\n", $3, $2, $5, $4}'
    echo "$(color_bold 'Memoria:')"
    free -h 2>/dev/null | awk 'NR==2 {printf "  %s / %s (%s usado, %s libre)\n", $3, $2, $3/$2*100"%", $4}'
    echo "$(color_bold 'Swap:')"
    free -h 2>/dev/null | awk 'NR==3 {printf "  %s / %s\n", $3, $2}'
    echo "$(color_bold 'CPU/Load:')"
    uptime 2>/dev/null | sed 's/.*load average:/  Load avg:/'
    echo

    read_with_timeout "  b. Volver: " _ 30 "b" || return $?
}

# ============================================================================
# OPCIÓN 8: DETENER CLÚSTER
# ============================================================================

do_stop_cluster() {
    print_header
    echo "       $(color_bold 'DETENER CLÚSTER')"
    echo "===================================================="
    echo "$(color_yellow '⚠') Esto detendrá TODOS los nodos del clúster."
    echo

    local pids
    pids=$(pgrep -f '[d]sm_node' 2>/dev/null || true)
    local count=0
    [ -n "$pids" ] && count=$(echo "$pids" | wc -w)

    echo "Nodos a detener: ${TOPOLOGY:-2}"
    echo "Procesos dsm_node activos: $count"
    echo

    echo "  $(color_cyan '1.') Detención ordenada (SIGTERM → 3s → SIGKILL)"
    echo "  $(color_cyan '2.') Forzar detención inmediata (SIGKILL)"
    echo "  $(color_cyan '3.') Solo verificar puertos"
    echo "  $(color_cyan 'b.') Cancelar / Volver"
    echo "----------------------------------------------------"

    local opt
    read_with_timeout "Seleccione [1-3]: " opt 30 "b" || return $?
    case "$opt" in
        1)
            echo
            echo "Deteniendo clúster (ordenada)..."
            local ports
            ports=$(role_ports 2>/dev/null || echo "27300 27301 27302 27303 27304 27305 27306 27307 27308 27309 27310 27311 27312 27313 27314 27315")
            local all_ports=($ports)
            local i=0

            kill_leftovers

            for p in "${all_ports[@]}"; do
                ((i++))
                if validate_port_free "$p"; then
                    printf '  [%d/%d] Puerto %s: %s\n' "$i" "${#all_ports[@]}" "$p" "$(color_green 'LIBRE')"
                else
                    printf '  [%d/%d] Puerto %s: %s\n' "$i" "${#all_ports[@]}" "$p" "$(color_yellow 'OCUPADO - liberando...')"
                    # Intentar liberar con fuser si existe
                    if command -v fuser >/dev/null 2>&1; then
                        fuser -k "${p}/tcp" 2>/dev/null || true
                    fi
                    sleep 1
                    validate_port_free "$p" && printf '  [%d/%d] Puerto %s: %s\n' "$i" "${#all_ports[@]}" "$p" "$(color_green 'LIBERADO')" || printf '  [%d/%d] Puerto %s: %s\n' "$i" "${#all_ports[@]}" "$p" "$(color_red 'NO LIBERADO')"
                fi
            done

            echo
            confirm_action "Limpiar logs y archivos swap" "N" && {
                rm -rf "$LOG_DIR"/*.log 2>/dev/null
                rm -f /tmp/dsm_swap_* 2>/dev/null
                echo "$(color_green 'Logs y swap limpiados.')"
            }
            echo "$(color_green 'Clúster detenido completamente.')"
            ;;
        2)
            echo
            echo "$(color_red 'Forzando detención inmediata...')"
            local pids
            pids=$(pgrep -f '[d]sm_node' 2>/dev/null || true)
            [ -n "$pids" ] && kill -9 $pids 2>/dev/null
            if command -v fuser >/dev/null 2>&1; then
                fuser -k 27300/tcp 27301/tcp 27302/tcp 27303/tcp \
                      27304/tcp 27305/tcp 27306/tcp 27307/tcp \
                      27308/tcp 27309/tcp 27310/tcp 27311/tcp \
                      27312/tcp 27313/tcp 27314/tcp 27315/tcp 2>/dev/null || true
            fi
            sleep 1
            echo "$(color_green 'Listo.') Verifica con opción 3."
            ;;
        3)
            echo
            echo "Verificando puertos 27300-27315..."
            local all_free=true
            for p in {27300..27315}; do
                if ! validate_port_free "$p"; then
                    echo "  Puerto $p: $(color_red 'OCUPADO')"
                    all_free=false
                fi
            done
            [ "$all_free" = true ] && echo "  $(color_green 'TODOS LOS PUERTOS LIBRES')"
            ;;
        b|B) return 0 ;;
        *) echo "Opción inválida." ;;
    esac

    read_with_timeout "  Enter para volver... " _ 5 "" || true
}

# ============================================================================
# OPCIÓN 9: AYUDA
# ============================================================================

do_help() {
    while true; do
        print_header
        echo "       $(color_bold 'AYUDA')"
        echo "===================================================="
        echo
        echo "$(color_bold 'NAVEGACIÓN:')"
        echo "  • Números 0-9: Seleccionar opción"
        echo "  • 'b' o ESC:   Volver al menú anterior"
        echo "  • Ctrl+C:      Cancelar operación actual / Salir limpio"
        echo
        echo "$(color_bold 'MODO NO-INTERACTIVO (automatización):')"
        echo "  ./dsm.sh --auto --start --topology 4 --timeout 60"
        echo "  ./dsm.sh --auto --app matrices --size 256 --topology 3"
        echo "  ./dsm.sh --auto --stop"
        echo "  ./dsm.sh --auto --status"
        echo "  ./dsm.sh --auto --stats"
        echo
        echo "$(color_bold 'OPCIONES PRINCIPALES:')"
        echo "  1. Iniciar clúster     - Arranca todos los nodos con progreso visual"
        echo "  2. Configurar nodos    - Topología, IPs, puertos, validación"
        echo "  3. Ver estado          - Nodos, heartbeat, errores conexión"
        echo "  4. Ejecutar apps       - 8 apps con parámetros, salida tiempo real"
        echo "  5. Estadísticas        - stats, pages, objects, dir con auto-refresh"
        echo "  6. Monitor memoria     - Marcos, MSI, swap, gráfico ASCII"
        echo "  7. Info sistema        - Versión, config, puertos, recursos"
        echo "  8. Detener clúster     - Ordenada/forzada, verificación puertos"
        echo "  9. Ayuda               - Esta pantalla"
        echo "  0. Salir               - Termina el programa"
        echo
        echo "$(color_bold 'ARCHIVOS DE ESTADO:')"
        echo "  .dsm_state         - Persistencia: rol, topología, IPs"
        echo "  config/nodes_N.conf - Tabla nodos generada"
        echo "  logs/dsm-nodeN.log  - Logs por nodo"
        echo

        read_with_timeout "  b. Volver: " _ 30 "b" || return $?
        return 0
    done
}

# ============================================================================
# MODO NO-INTERACTIVO (--auto)
# ============================================================================

parse_auto_args() {
    local args=("$@")
    local i=0
    while [ $i -lt ${#args[@]} ]; do
        case "${args[$i]}" in
            --auto) AUTO_MODE=true ;;
            --start) AUTO_ACTION="start" ;;
            --stop) AUTO_ACTION="stop" ;;
            --status) AUTO_ACTION="status" ;;
            --stats) AUTO_ACTION="stats" ;;
            --app) ((i++)); AUTO_ACTION="app"; AUTO_APP="${args[$i]:-}" ;;
            --topology) ((i++)); TOPOLOGY="${args[$i]:-2}" ;;
            --timeout) ((i++)); AUTO_TIMEOUT="${args[$i]:-30}" ;;
            --size) ((i++)); AUTO_PARAMS+=("--size" "${args[$i]:-}") ;;
            --iters) ((i++)); AUTO_PARAMS+=("--iters" "${args[$i]:-}") ;;
            --threads) ((i++)); AUTO_PARAMS+=("--threads" "${args[$i]:-}") ;;
            --pages) ((i++)); AUTO_PARAMS+=("--pages" "${args[$i]:-}") ;;
            --frames) ((i++)); AUTO_PARAMS+=("--frames" "${args[$i]:-}") ;;
            --nick) ((i++)); AUTO_PARAMS+=("--nick" "${args[$i]:-}") ;;
            --duration) ((i++)); AUTO_PARAMS+=("--duration" "${args[$i]:-}") ;;
            --file) ((i++)); AUTO_PARAMS+=("--file" "${args[$i]:-}") ;;
            --role) ((i++)); ROLE="${args[$i]:-}" ;;
            --ip-a) ((i++)); IP_A="${args[$i]:-}" ;;
            --ip-b) ((i++)); IP_B="${args[$i]:-}" ;;
            --ip-c) ((i++)); IP_C="${args[$i]:-}" ;;
            --ip-d) ((i++)); IP_D="${args[$i]:-}" ;;
            --config) ((i++)); AUTO_CONFIG="${args[$i]:-}" ;;
            --help|-h)
                cat <<EOF
Uso: dsm.sh [--auto OPCIONES]

Modo interactivo (por defecto):
  ./dsm.sh

Modo automático:
  ./dsm.sh --auto --start --topology 4 [--timeout 60] [--role A] [--ip-a IP] [--ip-b IP] ...
  ./dsm.sh --auto --stop
  ./dsm.sh --auto --status
  ./dsm.sh --auto --stats
  ./dsm.sh --auto --app matrices --size 128 --topology 3 --role A --ip-a 192.168.1.10 --ip-b 192.168.1.11 --ip-c 192.168.1.12

Opciones --app:
  demo, matrices (--size), file (--file), counter (--iters --threads),
  bench, evict (--pages --frames), chat (--nick --duration), task (--duration)
EOF
                exit 0
                ;;
        esac
        ((i++))
    done
}

auto_load_config() {
    # Si se pasó config explícita, usarla
    if [ -n "${AUTO_CONFIG:-}" ]; then
        return 0
    fi
    # Sino generar nodes_${TOPOLOGY}.conf
    write_nodes_file
}

auto_start() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "ERROR: Define --role A/B/C/D"; exit 1; }
    [ -x ./dsm_node ] || { echo "ERROR: Falta compilar (ejecuta dsm.sh opción 3)"; exit 1; }
    local nf
    nf=$(nodes_file_for_topology)

    kill_leftovers
    check_ports_free || exit 1

    echo "Iniciando clúster (auto)..."
    echo "Config: $cfg, Nodos: $nf, Timeout: ${AUTO_TIMEOUT:-30}s"

    ./dsm_node --config "$cfg" --nodes "$nf" &
    local pid=$!

    local elapsed=0 max_wait=${AUTO_TIMEOUT:-30}
    while [ $elapsed -lt $max_wait ]; do
        sleep 1
        ((elapsed++))
        local ready
        ready=$(./dsmctl --config "$cfg" nodes 2>/dev/null | grep -cE '^[0-9]+\s+' || echo 0)
        [ "$ready" -eq "${TOPOLOGY:-2}" ] && { echo "Clúster ACTIVO ($ready/${TOPOLOGY:-2} nodos)"; exit 0; }
    done

    echo "Timeout: solo $ready/${TOPOLOGY:-2} nodos listos"
    kill $pid 2>/dev/null; sleep 1; kill -9 $pid 2>/dev/null
    exit 1
}

auto_stop() {
    kill_leftovers
    if command -v fuser >/dev/null 2>&1; then
        fuser -k 27300/tcp 27301/tcp 27302/tcp 27303/tcp \
              27304/tcp 27305/tcp 27306/tcp 27307/tcp \
              27308/tcp 27309/tcp 27310/tcp 27311/tcp \
              27312/tcp 27313/tcp 27314/tcp 27315/tcp 2>/dev/null || true
    fi
    sleep 1
    echo "Clúster detenido."
    exit 0
}

auto_status() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "ERROR: Define --role"; exit 1; }
    [ -x ./dsmctl ] || { echo "ERROR: dsmctl no compilado"; exit 1; }
    ./dsmctl --config "$cfg" nodes
    exit 0
}

auto_stats() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "ERROR: Define --role"; exit 1; }
    [ -x ./dsmctl ] || { echo "ERROR: dsmctl no compilado"; exit 1; }
    ./dsmctl --config "$cfg" stats
    ./dsmctl --config "$cfg" pages | head -20
    ./dsmctl --config "$cfg" objects
    exit 0
}

auto_run_app() {
    local cfg
    cfg="$(cfg_for_role)" || { echo "ERROR: Define --role A/B/C/D"; exit 1; }
    [ -x ./dsm_node ] || { echo "ERROR: Falta compilar"; exit 1; }
    local nf
    nf=$(nodes_file_for_topology)

    kill_leftovers
    check_ports_free || exit 1

    echo "Ejecutando app: $AUTO_APP ${AUTO_PARAMS[*]}"
    ./dsm_node --config "$cfg" --nodes "$nf" --app "$AUTO_APP" "${AUTO_PARAMS[@]}"
    exit ${PIPESTATUS[0]}
}

run_auto_mode() {
    case "${AUTO_ACTION:-}" in
        start) auto_start ;;
        stop) auto_stop ;;
        status) auto_status ;;
        stats) auto_stats ;;
        app) auto_run_app ;;
        *) echo "ERROR: Acción --auto no especificada (--start, --stop, --status, --stats, --app)"; exit 1 ;;
    esac
}

# ============================================================================
# SEÑALES Y LIMPIEZA
# ============================================================================

cleanup_on_exit() {
    spinner_stop
    stty sane 2>/dev/null || true
    echo
    echo "$(color_yellow 'Saliendo...')"
}

handle_ctrl_c() {
    spinner_stop
    echo
    echo "$(color_yellow 'Operación cancelada.')"
    return 130
}

setup_traps() {
    trap cleanup_on_exit EXIT
    trap 'handle_ctrl_c; return 130' INT
    trap 'handle_ctrl_c; return 143' TERM
}

# ============================================================================
# MAIN
# ============================================================================

main() {
    setup_traps
    load_state

    # Parsear argumentos para modo auto
    if [ $# -gt 0 ]; then
        parse_auto_args "$@"
        if [ "$AUTO_MODE" = true ]; then
            auto_load_config
            run_auto_mode
            return $?
        fi
    fi

    # Menú interactivo
    while true; do
        print_header
        print_main_menu

        local opt
        read_with_timeout "$(color_bold 'Seleccione una opción: ')" opt 30 "" || continue
        case "$opt" in
            1) do_start_cluster ;;
            2) do_configure_nodes ;;
            3) do_view_status ;;
            4) do_run_apps ;;
            5) do_show_stats ;;
            6) do_monitor_memory ;;
            7) do_system_info ;;
            8) do_stop_cluster ;;
            9) do_help ;;
            0) echo "¡Hasta luego!"; exit 0 ;;
            *) echo "Opción inválida: $opt" ;;
        esac
        echo
        read_with_timeout "Enter para volver al menú... " _ 5 "" || true
    done
}

# Solo ejecutar main si el script se ejecuta directamente (no con source)
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi