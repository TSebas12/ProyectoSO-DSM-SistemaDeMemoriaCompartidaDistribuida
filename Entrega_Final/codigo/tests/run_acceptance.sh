#!/bin/bash
# run_acceptance.sh — Pruebas de aceptación del DSM (Entrega Final)
# Ejecuta todas las apps en modo loopback (1 PC) y valida resultados.
# Uso: ./tests/run_acceptance.sh [2|4]   (default: 4)

set -euo pipefail

cd "$(dirname "$0")/.."

NODES="${1:-4}"
echo "=== DSM Acceptance Tests (${NODES} nodos) ==="

if [ "${NODES}" = "2" ]; then
    NODES_CONF="config/nodes_2.conf"
else
    NODES_CONF="config/nodes_4.conf"
fi

PASSED=0
FAILED=0
FAILED_APPS=()

check_result() {
    local app="$1"
    local log0="$2"
    local result="FALLO"

    case "${app}" in
        demo)
            # demo prints stats on success, "fallo" on barrier failure
            if grep -q "demo: fallos=" "${log0}" && ! grep -q "la barrera.*fallo" "${log0}"; then
                result="OK"
            fi
            ;;
        matrices)
            if grep -q "verificacion: OK" "${log0}"; then
                result="OK"
            fi
            ;;
        file)
            if grep -q "checksum.*OK" "${log0}"; then
                result="OK"
            fi
            ;;
        counter)
            if grep -q "suma.*esperado.*OK" "${log0}"; then
                result="OK"
            fi
            ;;
        evict)
            if grep -q "verificacion OK" "${log0}"; then
                result="OK"
            fi
            ;;
        bench)
            if grep -q "rpc totales" "${log0}" && ! grep -q "reporto un fallo" "${log0}"; then
                result="OK"
            fi
            ;;
        task)
            if grep -q "total paralelo.*OK" "${log0}"; then
                result="OK"
            fi
            ;;
        *)
            result="UNKNOWN"
            ;;
    esac

    echo "${result}"
}

run_app() {
    local app="$1"
    local size="$2"
    local extra_args="${3:-}"
    local log0="/tmp/accept_${app}_0.log"
    local log1="/tmp/accept_${app}_1.log"
    local log2="/tmp/accept_${app}_2.log"
    local log3="/tmp/accept_${app}_3.log"

    echo -n "  ${app}... "
    pkill -x dsm_node 2>/dev/null || true; sleep 1

    ./dsm_node --config config/node0.conf --nodes "${NODES_CONF}" --app "${app}" --size "${size}" ${extra_args} --serve > "${log0}" 2>&1 &
    local N0=$!
    sleep 5

    ./dsm_node --config config/node1.conf --nodes "${NODES_CONF}" --app "${app}" --size "${size}" ${extra_args} > "${log1}" 2>&1 &
    local N1=$!

    local N2=0 N3=0
    if [ "${NODES}" = "4" ]; then
        ./dsm_node --config config/node2.conf --nodes "${NODES_CONF}" --app "${app}" --size "${size}" ${extra_args} > "${log2}" 2>&1 &
        N2=$!
        ./dsm_node --config config/node3.conf --nodes "${NODES_CONF}" --app "${app}" --size "${size}" ${extra_args} > "${log3}" 2>&1 &
        N3=$!
    fi

    local timeout=45
    if [ "${app}" = "bench" ]; then timeout=45; fi
    if [ "${app}" = "evict" ]; then timeout=30; fi

    sleep "${timeout}"

    kill ${N3} ${N2} ${N1} ${N0} 2>/dev/null
    wait 2>/dev/null

    local result
    result=$(check_result "${app}" "${log0}")

    if [ "${result}" = "OK" ]; then
        echo "OK"
        PASSED=$((PASSED + 1))
    else
        echo "FALLO (${result})"
        FAILED=$((FAILED + 1))
        FAILED_APPS+=("${app}")
    fi
}

# Compile
echo "Compilando..."
make clean >/dev/null && make >/dev/null
echo "Compilacion: OK"

# Run apps
run_app demo 1000
run_app matrices 64
run_app file 1000
run_app counter 2000
run_app evict 1000
run_app bench 1000
run_app task 10000

# chat no se prueba automatizado (interactivo)

echo ""
echo "=== RESUMEN ==="
echo "Pasaron: ${PASSED}"
echo "Fallaron: ${FAILED}"
if [ ${FAILED} -gt 0 ]; then
    echo "Apps fallidas: ${FAILED_APPS[*]}"
    exit 1
fi
echo "TODAS LAS PRUEBAS PASARON"
exit 0
