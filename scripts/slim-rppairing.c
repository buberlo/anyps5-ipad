/* Slim stand-in for libmadeira_rppairing.a.
 *
 * The real archive is build/rppairing-ios: Rust (idevice) that performs
 * iOS 27 on-device remote pairing so Madeira can start JIT without
 * StikDebug. That is not the AnyPS5 Vulkan path, and this unsigned build
 * already omits MadeiraJITHelper. Konrad's device boot still uses
 * StikDebug. These symbols exist so the app links; madeira_rppairing_new
 * fails and does not pair.
 */
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

typedef struct MadeiraRPPairing MadeiraRPPairing;
typedef void (*MadeiraRPPairingPinCallback)(const char *pin, void *context);

static char *slim_msg(void)
{
    return strdup("slim build: on-device remote pairing is not linked");
}

MadeiraRPPairing *madeira_rppairing_new(const char *name, char **error)
{
    (void)name;
    if (error)
        *error = slim_msg();
    return NULL;
}

uint16_t madeira_rppairing_port(const MadeiraRPPairing *session)
{
    (void)session;
    return 0;
}

const char *madeira_rppairing_service_name(const MadeiraRPPairing *session)
{
    (void)session;
    return "";
}

size_t madeira_rppairing_txt_count(const MadeiraRPPairing *session)
{
    (void)session;
    return 0;
}

const char *madeira_rppairing_txt_key(const MadeiraRPPairing *session, size_t index)
{
    (void)session;
    (void)index;
    return "";
}

const char *madeira_rppairing_txt_value(const MadeiraRPPairing *session, size_t index)
{
    (void)session;
    (void)index;
    return "";
}

int32_t madeira_rppairing_accept(const MadeiraRPPairing *session,
                                 MadeiraRPPairingPinCallback pin_callback, void *pin_context,
                                 uint8_t **out_plist, size_t *out_len,
                                 char **out_device_name, char **error)
{
    (void)session;
    (void)pin_callback;
    (void)pin_context;
    if (out_plist)
        *out_plist = NULL;
    if (out_len)
        *out_len = 0;
    if (out_device_name)
        *out_device_name = NULL;
    if (error)
        *error = slim_msg();
    return 1;
}

void madeira_rppairing_cancel(const MadeiraRPPairing *session)
{
    (void)session;
}

void madeira_rppairing_free(MadeiraRPPairing *session)
{
    free(session);
}

void madeira_rppairing_bytes_free(uint8_t *bytes, size_t len)
{
    (void)len;
    free(bytes);
}

void madeira_rppairing_string_free(char *string)
{
    free(string);
}
