/*
 * dsm_types.h — Tipos base, códigos de estado y constantes del DSM.
 *
 * Proyecto : Sistema de Memoria Compartida Distribuida (DSM)
 * Curso    : IC-6600 Principios de Sistemas Operativos
 * Entregable: 3 — Prototipo 2
 *
 * Este archivo no depende de ningún otro módulo del proyecto: solo de la
 * biblioteca estándar de C11 y de POSIX. Todos los módulos del runtime lo
 * incluyen, por eso aquí únicamente viven tipos, enums y constantes.
 */
#ifndef DSM_TYPES_H
#define DSM_TYPES_H

#include <signal.h>
#include <stddef.h>
#include <stdint.h>

/* --- Identidad del protocolo ------------------------------------------- */
#define DSM_MAGIC             0x44534D32u   /* "DSM2": cambió el protocolo */
#define DSM_PROTOCOL_VERSION  2u

/* --- Límites del sistema ------------------------------------------------ */
#define DSM_PAGE_SIZE_DEFAULT  4096u
#define DSM_PAGE_SIZE_MIN      4096u
#define DSM_PAGE_SIZE_MAX      65536u
#define DSM_MAX_NODES          16u
#define DSM_MAX_PEERS          DSM_MAX_NODES

/* --- Valores centinela -------------------------------------------------- */
#define DSM_INVALID_PAGE_ID  UINT64_MAX
#define DSM_INVALID_FRAME    UINT32_MAX
#define DSM_INVALID_NODE     UINT32_MAX
#define DSM_INVALID_OBJECT   UINT64_MAX

/*
 * Señal con la que el resolutor despierta a un hilo que quedó detenido en su
 * manejador de SIGSEGV. Se eligió SIGUSR1 porque no la usa el runtime para
 * nada más y se puede dirigir a un hilo concreto con pthread_kill.
 */
#define DSM_SIG_RESUME  SIGUSR1

/* --- Códigos de estado del API ------------------------------------------ */
/* Los valores negativos son errores locales; 0 es éxito.                  */
typedef enum dsm_status {
    DSM_OK              = 0,
    DSM_ERR_MMAP        = -1,
    DSM_ERR_MPROTECT    = -2,
    DSM_ERR_SIGACTION   = -3,
    DSM_ERR_NOMEM       = -4,
    DSM_ERR_NOT_MAPPED  = -5,
    DSM_ERR_PAGE_FAULT  = -6,
    DSM_ERR_TIMEOUT     = -7,
    DSM_ERR_INVAL       = -8,
    DSM_ERR_BUSY        = -9,
    DSM_ERR_STATE       = -10,
    DSM_ERR_NOENT       = -11,
    DSM_ERR_EXIST       = -12,
    DSM_ERR_PERM        = -13,
    DSM_ERR_AGAIN       = -14,
    DSM_ERR_OVERFLOW    = -15,
    DSM_ERR_NOTSUP      = -16,
    DSM_ERR_NET         = -17,
    DSM_ERR_PROTOCOL    = -18,
    DSM_ERR_CHECKSUM    = -19,
    DSM_ERR_SHUTDOWN    = -20,
    DSM_ERR_NO_SPACE    = -21,
    DSM_ERR_PEER_DOWN   = -22
} dsm_status_t;

static inline const char *dsm_status_name(dsm_status_t st)
{
    switch (st) {
    case DSM_OK:             return "DSM_OK";
    case DSM_ERR_MMAP:       return "DSM_ERR_MMAP";
    case DSM_ERR_MPROTECT:   return "DSM_ERR_MPROTECT";
    case DSM_ERR_SIGACTION:  return "DSM_ERR_SIGACTION";
    case DSM_ERR_NOMEM:      return "DSM_ERR_NOMEM";
    case DSM_ERR_NOT_MAPPED: return "DSM_ERR_NOT_MAPPED";
    case DSM_ERR_PAGE_FAULT: return "DSM_ERR_PAGE_FAULT";
    case DSM_ERR_TIMEOUT:    return "DSM_ERR_TIMEOUT";
    case DSM_ERR_INVAL:      return "DSM_ERR_INVAL";
    case DSM_ERR_BUSY:       return "DSM_ERR_BUSY";
    case DSM_ERR_STATE:      return "DSM_ERR_STATE";
    case DSM_ERR_NOENT:      return "DSM_ERR_NOENT";
    case DSM_ERR_EXIST:      return "DSM_ERR_EXIST";
    case DSM_ERR_PERM:       return "DSM_ERR_PERM";
    case DSM_ERR_AGAIN:      return "DSM_ERR_AGAIN";
    case DSM_ERR_OVERFLOW:   return "DSM_ERR_OVERFLOW";
    case DSM_ERR_NOTSUP:     return "DSM_ERR_NOTSUP";
    case DSM_ERR_NET:        return "DSM_ERR_NET";
    case DSM_ERR_PROTOCOL:   return "DSM_ERR_PROTOCOL";
    case DSM_ERR_CHECKSUM:   return "DSM_ERR_CHECKSUM";
    case DSM_ERR_SHUTDOWN:   return "DSM_ERR_SHUTDOWN";
    case DSM_ERR_NO_SPACE:   return "DSM_ERR_NO_SPACE";
    case DSM_ERR_PEER_DOWN:  return "DSM_ERR_PEER_DOWN";
    default:                 return "DSM_ERR_DESCONOCIDO";
    }
}

/* --- Estados del protocolo MSI ------------------------------------------ */
/*
 * Se fijan los valores numéricos para que coincidan con los que viajan por la
 * red. Nunca se compara un estado contra un literal: siempre contra el enum.
 */
typedef enum dsm_msi_state {
    DSM_PAGE_I = 0,   /* Inválida: no hay copia utilizable                  */
    DSM_PAGE_S = 1,   /* Compartida: copia de solo lectura                  */
    DSM_PAGE_M = 2    /* Modificada: copia exclusiva y sucia                */
} dsm_msi_state_t;

static inline const char *dsm_msi_state_name(dsm_msi_state_t st)
{
    switch (st) {
    case DSM_PAGE_I: return "I";
    case DSM_PAGE_S: return "S";
    case DSM_PAGE_M: return "M";
    default:         return "?";
    }
}

/* --- Residencia local de una página ------------------------------------- */
typedef enum dsm_residency {
    DSM_RES_NOT_RESIDENT = 0,  /* Sin datos: la ventana está en PROT_NONE    */
    DSM_RES_FETCHING     = 1,  /* Otro hilo ya está trayendo esta página     */
    DSM_RES_RESIDENT     = 2,  /* Datos presentes y permisos publicados      */
    DSM_RES_EVICTING     = 3,  /* Un hilo la está expulsando                 */
    DSM_RES_ERROR        = 4   /* La última resolución falló                 */
} dsm_residency_t;

static inline const char *dsm_residency_name(dsm_residency_t r)
{
    switch (r) {
    case DSM_RES_NOT_RESIDENT: return "NO_RESIDENTE";
    case DSM_RES_FETCHING:     return "TRAYENDO";
    case DSM_RES_RESIDENT:     return "RESIDENTE";
    case DSM_RES_EVICTING:     return "EXPULSANDO";
    case DSM_RES_ERROR:        return "ERROR";
    default:                   return "?";
    }
}

/* --- Tipo de fallo de página -------------------------------------------- */
typedef enum dsm_fault_kind {
    DSM_FAULT_READ    = 0,
    DSM_FAULT_WRITE   = 1,
    DSM_FAULT_EXEC    = 2,
    DSM_FAULT_UNKNOWN = 3
} dsm_fault_kind_t;

static inline const char *dsm_fault_kind_name(dsm_fault_kind_t k)
{
    switch (k) {
    case DSM_FAULT_READ:    return "LECTURA";
    case DSM_FAULT_WRITE:   return "ESCRITURA";
    case DSM_FAULT_EXEC:    return "EJECUCION";
    case DSM_FAULT_UNKNOWN: return "DESCONOCIDO";
    default:                return "?";
    }
}

/* --- Eventos que disparan una transición MSI ---------------------------- */
typedef enum dsm_msi_event {
    DSM_EV_LOCAL_READ     = 0,  /* El hilo local leyó una página ausente     */
    DSM_EV_LOCAL_WRITE    = 1,  /* El hilo local escribió una página ausente */
    DSM_EV_INVALIDATE     = 2,  /* Llegó INVALIDATE del nodo home            */
    DSM_EV_REMOTE_READ    = 3,  /* Otro nodo pidió la página para lectura    */
    DSM_EV_GRANT          = 4,  /* Se concedió la propiedad a otro nodo      */
    DSM_EV_WRITEBACK      = 5   /* La página sucia volvió al home            */
} dsm_msi_event_t;

/* --- Tipos de mensaje ---------------------------------------------------- */
/*
 * Se conservan los códigos del Entregable 1 y 2. Los mensajes nuevos del
 * prototipo 2 usan rangos que no chocan con los anteriores.
 */
typedef enum dsm_msg_type {
    /* Objetos y barreras (nuevo en E3) */
    DSM_MSG_OBJECT_ALLOC       = 0x0010,
    DSM_MSG_OBJECT_ALLOC_REPLY = 0x0011,
    DSM_MSG_BARRIER            = 0x0012,
    DSM_MSG_BARRIER_REPLY      = 0x0013,

    /* Páginas */
    DSM_MSG_PAGE_REQUEST       = 0x0020,
    DSM_MSG_PAGE_DATA          = 0x0021,
    DSM_MSG_PAGE_FORWARD       = 0x0022,
    DSM_MSG_PAGE_WRITEBACK     = 0x0023,
    DSM_MSG_PAGE_WRITEBACK_ACK = 0x0024,
    DSM_MSG_PAGE_RELEASE       = 0x0025,
    DSM_MSG_PAGE_LIST          = 0x0026,
    DSM_MSG_PAGE_LIST_REPLY    = 0x0027,

    /* Propiedad */
    DSM_MSG_OWNERSHIP_REQUEST  = 0x0030,
    DSM_MSG_OWNERSHIP_GRANT    = 0x0031,
    DSM_MSG_OWNERSHIP_DENY     = 0x0032,

    /* Coherencia */
    DSM_MSG_INVALIDATE         = 0x0040,
    DSM_MSG_INVALIDATE_ACK     = 0x0041,

    /* Observabilidad */
    DSM_MSG_STATS_REQUEST      = 0x0050,
    DSM_MSG_STATS_REPLY        = 0x0051,
    DSM_MSG_PING               = 0x0060,
    DSM_MSG_PONG               = 0x0061,

    /* Genéricos */
    DSM_MSG_ERROR              = 0x0080,
    DSM_MSG_ACK                = 0x0081
} dsm_msg_type_t;

/* --- Banderas de la cabecera -------------------------------------------- */
#define DSM_FLAG_RESPONSE   (1u << 0)
#define DSM_FLAG_URGENT     (1u << 1)
#define DSM_FLAG_HAS_DATA   (1u << 2)
#define DSM_FLAG_RETRY      (1u << 3)
#define DSM_FLAG_LAST       (1u << 4)   /* El emisor suelta su copia        */
#define DSM_FLAG_NO_CACHE   (1u << 5)
#define DSM_FLAG_NEED_DATA  (1u << 6)   /* INVALIDATE que pide los datos    */

/*
 * Mensaje lógico.
 *
 * Es el contrato entre la capa de coherencia y la capa de red: la coherencia
 * razona con esta estructura en memoria y no sabe nada de bytes, cabeceras de
 * 64 bytes ni sockets. protocol.c la convierte a/desde el formato de red.
 */
typedef struct dsm_msg {
    uint16_t       type;         /* dsm_msg_type_t                        */
    uint16_t       flags;        /* DSM_FLAG_*                            */
    uint32_t       src_node;     /* Nodo emisor                           */
    uint32_t       dst_node;     /* Nodo destino                          */
    uint64_t       request_id;   /* Idempotencia                          */
    uint64_t       page_id;      /* DSM_INVALID_PAGE_ID si no aplica      */
    int32_t        status;       /* dsm_status_t en respuestas            */
    uint32_t       payload_len;  /* Bytes válidos en payload              */
    const void    *payload;      /* Payload estructurado, puede ser NULL  */
    const uint8_t *data;         /* Datos de página, puede ser NULL       */
    uint32_t       data_len;     /* Bytes válidos en data                 */
} dsm_msg_t;

#endif /* DSM_TYPES_H */
